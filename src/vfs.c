#include "vfs.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

#define TYPST_VFS_ROOT "/__typst_vfs"

static const TypstVfsFile *g_typst_vfs_files;
static size_t g_typst_vfs_count;

void typst_vfs_reset(void) {
  g_typst_vfs_files = NULL;
  g_typst_vfs_count = 0;
}

void typst_vfs_set_files(const TypstVfsFile *files, size_t count) {
  g_typst_vfs_files = files;
  g_typst_vfs_count = count;
}

bool typst_vfs_is_active(void) {
  return g_typst_vfs_files != NULL && g_typst_vfs_count > 0;
}

char *typst_vfs_normalize_path(const char *path) {
  if (path == NULL)
    return NULL;
  if (*path == '/')
    return JOIN_PATHS(path);
  return JOIN_PATHS(TYPST_VFS_ROOT, path);
}

const TypstVfsFile *typst_vfs_find(const char *path) {
  if (!typst_vfs_is_active() || path == NULL)
    return NULL;

  char *normalized = typst_vfs_normalize_path(path);
  if (normalized == NULL)
    return NULL;

  const TypstVfsFile *found = NULL;
  for (size_t i = 0; i < g_typst_vfs_count; ++i) {
    if (strcmp(g_typst_vfs_files[i].path, normalized) == 0) {
      found = &g_typst_vfs_files[i];
      break;
    }
  }

  free(normalized);
  return found;
}

FILE *typst_vfs_fopen(const char *path, const char *mode) {
  const TypstVfsFile *file = typst_vfs_find(path);
  if (file == NULL)
    return fopen(path, mode);

  if (mode == NULL || mode[0] != 'r')
    return NULL;

  return fmemopen((void*)file->content, file->len, "r");
}

bool typst_vfs_is_file(const char *path) {
  if (typst_vfs_find(path) != NULL)
    return true;
  return is_file(path);
}

char *typst_vfs_getcwd(char *buf, size_t size) {
  static const char cwd[] = "/";
  if (!typst_vfs_is_active())
    return getcwd(buf, size);

  if (buf == NULL)
    return strdup(cwd);
  if (size < sizeof(cwd))
    return NULL;

  memcpy(buf, cwd, sizeof(cwd));
  return buf;
}
