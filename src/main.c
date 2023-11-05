#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS

#include "ffpack.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <string.h>

#include <io.h>
#include <windows.h>

#define ARG_STR_BUFSIZE 288

static char arg_in_path[ARG_STR_BUFSIZE];
static char arg_out_path[ARG_STR_BUFSIZE];
static char arg_exceptions_path[ARG_STR_BUFSIZE];
static bool arg_verbose = false;
static bool arg_test_dates = false;
static bool arg_recursive = false;

static char *exceptions_list = NULL;
static bool output_to_dir = false;

static const char program_help[] =
    "\nPacks files and prepends them with \"Pak\\33\"\nFiles that do not compress are simply copied.\npack -i "
    "<input file / folder> -o <output file / folder> [<options>]\nWildcards might work for the "
    "input.\nOptions:\n-o           Specify output file / folder. No wildcards.\n-r           Recurse "
    "subfolders. Folders are created but root must exist.\n-t           Test file dates, don\'t pack if output "
    "newer.\n-e           Specify exceptions file to contain files to exclude from packing.\n             "
    "Excluded files should not have a prepended path.\n";

static noreturn void print_help_and_exit(void)
{
    // FIXME: print to stderr
    printf(program_help);
    exit(EXIT_FAILURE);
}

static noreturn void exit_error(const char *message)
{
    // FIXME: print to stderr
    printf(message);
    exit(EXIT_FAILURE);
}

static int copy_option_arg(int argc, char **argv, int index, char *dest)
{
    size_t len = strlen(argv[index]);
    if (len < 3)
    {
        if (index == argc)
            exit_error("Bad command line.");

        strcpy(dest, argv[index + 1]);
        return 1;
    }

    strcpy(dest, argv[index] + 2);
    return 0;
}

static void parse_program_options(int argc, char **argv)
{
    if (argc < 2)
    {
        print_help_and_exit();
    }

    for (int i = 0; i < argc; ++i)
    {
        const char *arg = argv[i];

        if (strncmp(arg, "-i", 2) == 0)
            i += copy_option_arg(argc, argv, i, arg_in_path);

        else if (strncmp(arg, "-o", 2) == 0)
            i += copy_option_arg(argc, argv, i, arg_out_path);

        else if (strcmp(arg, "-v") == 0)
            arg_verbose = true;

        else if (strcmp(arg, "-t") == 0)
            arg_test_dates = true;

        else if (strcmp(arg, "-r") == 0)
            arg_recursive = true;

        else if (strncmp(arg, "-e", 2) == 0)
            i += copy_option_arg(argc, argv, i, arg_exceptions_path);

        else
            print_help_and_exit();
    }

    if (arg_in_path[0] == '\0')
        exit_error("No source directory specified.");

    if (arg_out_path[0] == '\0')
        exit_error("No output name specified.");
}

static long stream_length(FILE *stream)
{
    long start = ftell(stream);
    if (start != -1L)
    {
        if (fseek(stream, 0, SEEK_END) == 0)
        {
            long end = ftell(stream);
            if (fseek(stream, start, SEEK_SET) == 0)
                return end;
        }
    }

    return -1;
}

static void logf(const char *fmt, ...)
{
    // FIXME: print to stderr
    va_list argptr;
    va_start(argptr, fmt);
    vprintf(fmt, argptr);
    va_end(argptr);
}

static bool has_wildcard(const char *str)
{
    return strchr(str, '?') || strchr(str, '*');
}

static bool is_directory(const struct _finddata_t *fileinfo)
{
    return fileinfo && (fileinfo->attrib & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static bool exceptions_list_contains(const char *filename)
{
    if (arg_exceptions_path[0] == '\0')
        return false;

    size_t len = strlen(filename);
    const char *pc = filename + len;

    while (pc >= filename && *pc != '\\')
        --pc; // strrchr

    if (*pc == '\\')
        ++pc;

    len = strlen(pc);

    char *pexcept = exceptions_list;
    while (*pexcept++ != '\0')
    {
        if (strnicmp(pc, pexcept, len) == 0)
        {
            printf("Ignoring %s\n", pexcept);
            return true;
        }
    }

    return false;
}

/// @brief determines which file was modified last
/// @param lhs first filename
/// @param rhs second filename
/// @return `-1` if lhs is newer, `1` if rhs is newer, `0` otherwise
static int latest_file(const char *lhs, const char *rhs)
{
    HANDLE lhs_file = CreateFile(lhs, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    HANDLE rhs_file = CreateFile(rhs, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);

    if (lhs_file == INVALID_HANDLE_VALUE)
        return 1;

    BY_HANDLE_FILE_INFORMATION lhs_info;
    BY_HANDLE_FILE_INFORMATION rhs_info;

    GetFileInformationByHandle(lhs_file, &lhs_info);
    GetFileInformationByHandle(rhs_file, &rhs_info);

    CloseHandle(lhs_file);
    CloseHandle(rhs_file);

    FILETIME lhs_last_write = lhs_info.ftLastWriteTime;
    FILETIME rhs_last_write = rhs_info.ftLastWriteTime;

    if (rhs_last_write.dwHighDateTime < lhs_last_write.dwHighDateTime)
        return 1;

    if (lhs_last_write.dwHighDateTime < rhs_last_write.dwHighDateTime)
        return -1;

    if (rhs_last_write.dwLowDateTime < lhs_last_write.dwLowDateTime)
        return 1;

    if (lhs_last_write.dwLowDateTime < rhs_last_write.dwLowDateTime)
        return -1;

    return 0;
}

static bool compress_file(const char *dest_path, const char *src_path)
{
    if (exceptions_list_contains(src_path))
        return false;

    if (arg_test_dates && latest_file(src_path, dest_path) == -1)
    {
        printf("%s : Packed file is newer.\n", src_path);
        return false;
    }

    char *err = NULL;

    FILE *src_file = fopen(src_path, "rb");
    if (!src_file)
        exit_error("Cannot open the input file.");

    FILE *dest_file = fopen(dest_path, "wb");
    if (!dest_file)
        exit_error("Cannot open the output file.");

    size_t src_len = stream_length(src_file);
    char *src_data = malloc(src_len + 500);
    if (!src_data)
        exit_error("Cannot allocate enough memory (input)");

    char *compressed_data = malloc(src_len + 500);
    if (!compressed_data)
    {
        err = "Cannot allocate enough memory (output)";
        goto cleanup_1;
    }

    if (fread(src_data, 1, src_len, src_file) != src_len)
    {
        err = "Cannot read input file";
        goto cleanup_2;
    }

    if (strncmp(src_data, "Pak\x1b", 4) == 0)
    {
        printf("%s : Already packed\r\n", src_path);
        if (fwrite(src_data, 1, src_len, dest_file) != src_len)
            err = "Cannot write output file.";
    }
    else
    {
        printf("Packing %s ... ", src_path);
        size_t compressed_len = pak_compress(src_data, compressed_data, src_len);

        if (compressed_len != src_len)
        {
            logf("Data will not compress. Copying.\r\n");
            if (fwrite(src_data, 1, src_len, dest_file) != src_len)
                err = "Cannot write output file.";
        }
        else
        {
            if (fwrite(compressed_data, 1, compressed_len, dest_file) != compressed_len)
                err = "Cannot write output file.";
        }
    }

cleanup_2:
    free(compressed_data);
cleanup_1:
    free(src_data);
    fcloseall();

    if (err)
        exit_error(err);

    return true;
}

static bool compress_files(const char *dest_path, const char *src_dir, const char *src_filename)
{
    char src_path[_MAX_PATH];
    strcpy(src_path, src_dir);
    strcat(src_path, "\\");
    strcat(src_path, src_filename);

    char src_final[_MAX_PATH];
    char dest_final[_MAX_PATH];

    struct _finddata_t fileinfo; // FIXME: portability
    intptr_t find_handle = _findfirst(src_path, &fileinfo);
    if (find_handle == -1)
    {
        printf("No matching files in current directory!\n");
    }
    else
    {
        if (is_directory(&fileinfo))
        {
            sprintf(src_final, "%s\\%s", src_dir, fileinfo.name);

            if (output_to_dir)
                sprintf(dest_final, "%s\\%s", dest_path, fileinfo.name);
            else
                sprintf(dest_final, "%s", dest_path);

            compress_file(dest_final, src_final);
        }

        while (_findnext(find_handle, &fileinfo) == 0)
        {
            sprintf(src_final, "%s\\%s", src_dir, fileinfo.name);

            if (output_to_dir)
                sprintf(dest_final, "%s\\%s", dest_path, fileinfo.name);
            else
                sprintf(dest_final, "%s\\%s", dest_path); // FIXME: bug in format string? (UB)

            compress_file(dest_final, src_final);
        }

        _findclose(find_handle);
    }

    return true;
}

static bool process_files(const char *dest_path, char *src_path)
{
    if (has_wildcard(dest_path))
        exit_error("Wildcards are not allowed in the output name.");

    bool src_wildcard = has_wildcard(src_path);

    // FIXME: portability
    struct _finddata_t fileinfo;
    intptr_t find_handle = _findfirst(src_path, &fileinfo);

    if (find_handle == -1)
        exit_error("Cannot open input file(s)");

    bool src_matches_dir = is_directory(&fileinfo);
    bool src_matches_file = !src_matches_dir;

    while (_findnext(find_handle, &fileinfo) == 0)
    {
        if (is_directory(&fileinfo))
            src_matches_dir = true;
        else
            src_matches_file = true;
    }
    _findclose(find_handle);

    bool output_to_file = 0;
    char c;

    find_handle = _findfirst(dest_path, &fileinfo);
    if (find_handle == -1)
    {
        c = '\0'; // User input fallback?
        if (!(src_wildcard || src_matches_dir))
        {
            output_to_file = true;
            output_to_dir = false;
        }
        else
        {
            output_to_file = false;
            output_to_dir = true;
        }
    }
    else
    {
        if (is_directory(&fileinfo))
            output_to_dir = true;
        else
            output_to_file = true;

        while (_findnext(find_handle, &fileinfo) == 0)
        {
            if (is_directory(&fileinfo))
                output_to_dir = true;
            else
                output_to_file = true;
        }
    }

    if (src_wildcard)
        src_matches_file = true;

    if (output_to_file && output_to_dir)
        exit_error("something really bad.");

    if (output_to_file && (src_wildcard || arg_recursive))
        exit_error("Cannot write multiple inputs to one output file.\r\nSpecify an output folder or one input file.");

    _findclose(find_handle);

    if (src_matches_file && src_matches_dir)
    {
        if (!src_wildcard)
            logf("Please tell Keith something odd has happened.\r\n");

        c = '\0'; // User input fallback?
        src_matches_dir = false;
        printf("\n");

        src_matches_file = c == 'F';
        src_matches_dir = !src_matches_file;
    }

    logf("It's a %s\r\n", src_matches_dir ? "DIRECTORY" : "FILE");

    char filedir[_MAX_PATH];
    char filename[_MAX_PATH];

    if (!src_matches_dir)
    {
        size_t i = strlen(src_path);
        // FIXME: strrchr
        do
        {
            i -= 1;
            if (i == 0)
                break;
        } while (src_path[i] != '\\');

        src_path[i] = '\\'; // TODO: what is this doing?

        strncpy(filedir, src_path, i);
        filedir[i] = '\0';
        strcpy(filename, src_path + i + 1);

        if (filename[0] == '\0')
            strcpy(filename, "*.*");
    }
    else
    {
        strcpy(filedir, src_path);
        strcpy(filename, "*.*");
    }
    logf("Path:%s\tFile:%s\r\n", filedir, filename);

    compress_files(dest_path, filedir, filename);

    return true;
}

int main(int argc, char **argv)
{
    parse_program_options(argc, argv);
    if (arg_exceptions_path[0] != '\0')
    {
        FILE *exceptions_file = fopen(arg_exceptions_path, "rb");

        if (!exceptions_file)
            exit_error("Cannot open the exceptions file.");

        long len = stream_length(exceptions_file);
        exceptions_list = malloc(len + 50);

        if (!exceptions_list)
            exit_error("Cannot allocate enough memory for the exception file.");

        int c = fgetc(exceptions_file);
        char *pc = exceptions_list;
        bool skip = false;

        while (c != EOF)
        {
            if (c < ' ')
            {
                if (!skip)
                {
                    *pc++ = '\0';
                    skip = true; // Ignore any non-printable chars until a printable char is found
                }
            }
            else
            {
                *pc++ = (char)c;
                skip = false;
            }
            c = fgetc(exceptions_file);
        }
        memset(pc, '\0', 4);
    }

    process_files(arg_out_path, arg_in_path);

    return EXIT_SUCCESS;
}
