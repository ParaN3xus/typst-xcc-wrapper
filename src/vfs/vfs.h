#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef struct {
  const char *path;
  const unsigned char *content;
  size_t len;
} TypstVfsFile;

void typst_vfs_reset(void);
void typst_vfs_set_files(const TypstVfsFile *files, size_t count);
bool typst_vfs_is_active(void);
const TypstVfsFile *typst_vfs_find(const char *path);
char *typst_vfs_normalize_path(const char *path);

FILE *typst_vfs_fopen(const char *path, const char *mode);
bool typst_vfs_is_file(const char *path);
char *typst_vfs_getcwd(char *buf, size_t size);
