#define WIN32_LEAN_AND_MEAN

#include "ffpack.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <string.h>

#define ARG_STR_BUFSIZE 288

static char arg_in_path[ARG_STR_BUFSIZE];
static char arg_out_path[ARG_STR_BUFSIZE];
static char arg_exceptions_path[ARG_STR_BUFSIZE];
bool arg_verbose = false;
bool arg_test_dates = false;
bool arg_recursive = false;

char *exceptions_list = NULL;

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

    pak_process_files(arg_out_path, arg_in_path);

    return EXIT_SUCCESS;
}
