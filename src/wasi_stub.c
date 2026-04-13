#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef uint8_t Preopentype;
typedef struct {
  size_t pr_name_len;
} PrestatDir;
typedef struct {
  PrestatDir dir;
} PrestatU;
typedef struct {
  Preopentype pr_type;
  PrestatU u;
} Prestat;

typedef uint64_t Timestamp;
typedef struct Filestat {
  uint64_t dev;
  uint64_t ino;
  uint8_t filetype;
  uint64_t nlink;
  uint64_t size;
  Timestamp atim;
  Timestamp mtim;
  Timestamp ctim;
} Filestat;

int __main_argc_argv(int argc, char **argv) {
  (void)argc;
  (void)argv;
  return 0;
}

int args_sizes_get(int *pargc, int *plen) {
  if (pargc != NULL)
    *pargc = 0;
  if (plen != NULL)
    *plen = 0;
  return 0;
}

int args_get(char **pargv, char *pstr) {
  (void)pargv;
  (void)pstr;
  return 0;
}

_Noreturn void proc_exit(int code) {
  (void)code;
  for (;;)
    ;
}

int fd_prestat_get(int fd, Prestat *prestat) {
  (void)fd;
  if (prestat != NULL)
    memset(prestat, 0, sizeof(*prestat));
  return 1;
}

int fd_prestat_dir_name(int fd, char *out, size_t size) {
  (void)fd;
  if (out != NULL && size > 0)
    out[0] = '\0';
  return 1;
}

int path_open(int fd, int dirflags, const char *path, size_t path_len, int oflags,
              uint64_t fs_rights_base, uint64_t fs_rights_inheriting, uint16_t fdflags,
              uint32_t *opened_fd) {
  (void)fd;
  (void)dirflags;
  (void)path;
  (void)path_len;
  (void)oflags;
  (void)fs_rights_base;
  (void)fs_rights_inheriting;
  (void)fdflags;
  if (opened_fd != NULL)
    *opened_fd = 0;
  return 1;
}

int path_unlink_file(int fd, const char *path, size_t path_len) {
  (void)fd;
  (void)path;
  (void)path_len;
  return 1;
}

int path_filestat_get(int fd, int flags, const char *path, size_t path_len, Filestat *out) {
  (void)fd;
  (void)flags;
  (void)path;
  (void)path_len;
  if (out != NULL)
    memset(out, 0, sizeof(*out));
  return 1;
}

int fd_read(int fd, const void *iov, int count, size_t *out) {
  (void)fd;
  (void)iov;
  (void)count;
  if (out != NULL)
    *out = 0;
  return 1;
}

int fd_write(int fd, const void *iov, int count, size_t *out) {
  (void)fd;
  (void)iov;
  (void)count;
  if (out != NULL)
    *out = 0;
  return 1;
}

int fd_close(int fd) {
  (void)fd;
  return 0;
}

int fd_seek(int fd, int64_t offset, int whence, size_t *psize) {
  (void)fd;
  (void)offset;
  (void)whence;
  if (psize != NULL)
    *psize = 0;
  return 1;
}

int fd_filestat_get(int fd, Filestat *out) {
  (void)fd;
  if (out != NULL)
    memset(out, 0, sizeof(*out));
  return 1;
}

int environ_sizes_get(size_t *count, size_t *buf_size) {
  if (count != NULL)
    *count = 0;
  if (buf_size != NULL)
    *buf_size = 0;
  return 0;
}

int environ_get(char **environ, char *environ_buf) {
  (void)environ;
  (void)environ_buf;
  return 0;
}

int random_get(void *buf, size_t buf_len) {
  if (buf != NULL && buf_len > 0)
    memset(buf, 0, buf_len);
  return 0;
}
