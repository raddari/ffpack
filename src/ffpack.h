#pragma once

#include <stdbool.h>
#include <stdnoreturn.h>

extern bool arg_verbose;
extern bool arg_test_dates;
extern bool arg_recursive;
extern char *exceptions_list;

noreturn void exit_error(const char *message);
bool pak_process_files(const char *dest_path, char *src_path);
bool pak_compress_files(const char *dest_path, const char *src_dir, const char *src_filename);
bool pak_compress_file(const char *dest_path, const char *src_path);
