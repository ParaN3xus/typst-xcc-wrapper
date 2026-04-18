#include "vfs.h"

#define fopen typst_vfs_fopen
#define is_file typst_vfs_is_file
#define getcwd typst_vfs_getcwd
#include "preprocessor.c"
#undef getcwd
#undef is_file
#undef fopen
