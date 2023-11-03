#include "ffpack.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// FIXME: portability
#include <fileapi.h>
#include <io.h>

static bool output_to_dir = false;

noreturn void exit_error(const char *message)
{
    // FIXME: print to stderr
    printf(message);
    exit(EXIT_FAILURE);
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

bool pak_process_files(const char *dest_path, char *src_path)
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

    pak_compress_files(dest_path, filedir, filename);

    return true;
}

bool pak_compress_files(const char *dest_path, const char *src_dir, const char *src_filename)
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

            pak_compress_file(dest_final, src_final);
        }

        while (_findnext(find_handle, &fileinfo) == 0)
        {
            sprintf(src_final, "%s\\%s", src_dir, fileinfo.name);

            if (output_to_dir)
                sprintf(dest_final, "%s\\%s", dest_path, fileinfo.name);
            else
                sprintf(dest_final, "%s\\%s", dest_path); // FIXME: bug in format string? (UB)

            pak_compress_file(dest_final, src_final);
        }

        _findclose(find_handle);
    }

    return true;
}

bool pak_compress_file(const char *dest_path, const char *src_path)
{
    // TODO: implement
    return true;
}
