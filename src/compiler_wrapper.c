#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define main xcc_embedded_main
#include "wcc.c"
#undef main

static int slurp_stdin(char **out, size_t *out_len) {
  size_t capacity = 4096;
  size_t len = 0;
  char *buffer = malloc(capacity);
  if (buffer == NULL) {
    fprintf(stderr, "failed to allocate stdin buffer\n");
    return 1;
  }

  for (;;) {
    if (len == capacity) {
      capacity *= 2;
      char *next = realloc(buffer, capacity);
      if (next == NULL) {
        free(buffer);
        fprintf(stderr, "failed to grow stdin buffer\n");
        return 1;
      }
      buffer = next;
    }

    size_t n = fread(buffer + len, 1, capacity - len, stdin);
    len += n;
    if (n == 0) {
      if (ferror(stdin)) {
        free(buffer);
        perror("failed to read stdin");
        return 1;
      }
      break;
    }
  }

  *out = buffer;
  *out_len = len;
  return 0;
}

static int write_temp_source(const char *suffix, const char *content, size_t len, char **out_path) {
  char *path = strdup("/tmp/typst-xcc-wrapper-XXXXXX.c");
  if (path == NULL) {
    fprintf(stderr, "failed to allocate temp path\n");
    return 1;
  }

  size_t suffix_len = strlen(suffix);
  memcpy(path + strlen(path) - suffix_len, suffix, suffix_len + 1);
  int fd = mkstemps(path, (int)suffix_len);
  if (fd < 0) {
    perror("mkstemps");
    free(path);
    return 1;
  }

  FILE *file = fdopen(fd, "wb");
  if (file == NULL) {
    perror("fdopen");
    close(fd);
    unlink(path);
    free(path);
    return 1;
  }

  if (fwrite(content, 1, len, file) != len) {
    perror("fwrite");
    fclose(file);
    unlink(path);
    free(path);
    return 1;
  }

  if (fclose(file) != 0) {
    perror("fclose");
    unlink(path);
    free(path);
    return 1;
  }

  *out_path = path;
  return 0;
}

static void print_usage(const char *exe) {
  fprintf(
      stderr,
      "Usage:\n"
      "  %s <xcc arguments...>\n"
      "  %s -- <xcc arguments...>\n"
      "  %s --compile-stdin <output.wasm> [extra xcc args...]\n",
      exe, exe, exe);
}

static int build_forward_argv_with_root(
    int argc, char **argv, int *out_argc, char ***out_argv, char **out_include_arg, char **out_lib_arg) {
  const char *root = getenv("TYPST_XCC_ROOT");
  int extra = root != NULL && *root != '\0' ? 2 : 0;
  char **forward = calloc((size_t)argc + (size_t)extra + 1, sizeof(char*));
  if (forward == NULL) {
    fprintf(stderr, "failed to allocate argv\n");
    return 1;
  }

  int i = 0;
  forward[i++] = argv[0];
  char *include_arg = NULL;
  char *lib_arg = NULL;
  if (extra != 0) {
    size_t root_len = strlen(root);
    include_arg = malloc(root_len + sizeof("-I/include"));
    lib_arg = malloc(root_len + sizeof("-L/lib"));
    if (include_arg == NULL || lib_arg == NULL) {
      free(include_arg);
      free(lib_arg);
      free(forward);
      fprintf(stderr, "failed to allocate root argv\n");
      return 1;
    }
    sprintf(include_arg, "-I%s/include", root);
    sprintf(lib_arg, "-L%s/lib", root);
    forward[i++] = include_arg;
    forward[i++] = lib_arg;
  }

  for (int j = 1; j < argc; ++j)
    forward[i++] = argv[j];
  forward[i] = NULL;
  *out_argc = i;
  *out_argv = forward;
  *out_include_arg = include_arg;
  *out_lib_arg = lib_arg;
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    print_usage(argv[0]);
    return 1;
  }

  if (strcmp(argv[1], "--") == 0) {
    return xcc_embedded_main(argc - 1, argv + 1);
  }

  if (strcmp(argv[1], "--compile-stdin") == 0) {
    if (argc < 3) {
      print_usage(argv[0]);
      return 1;
    }

    char *source = NULL;
    size_t source_len = 0;
    if (slurp_stdin(&source, &source_len) != 0)
      return 1;

    char *temp_path = NULL;
    if (write_temp_source(".c", source, source_len, &temp_path) != 0) {
      free(source);
      return 1;
    }
    free(source);

    int xcc_argc = argc + 1;
    char **xcc_argv = calloc((size_t)xcc_argc + 1, sizeof(char *));
    if (xcc_argv == NULL) {
      fprintf(stderr, "failed to allocate argv\n");
      unlink(temp_path);
      free(temp_path);
      return 1;
    }

    int i = 0;
    xcc_argv[i++] = "xcc";
    xcc_argv[i++] = "-o";
    xcc_argv[i++] = argv[2];
    for (int j = 3; j < argc; ++j)
      xcc_argv[i++] = argv[j];
    xcc_argv[i++] = temp_path;
    xcc_argv[i] = NULL;

    int result = xcc_embedded_main(i, xcc_argv);
    unlink(temp_path);
    free(temp_path);
    free(xcc_argv);
    return result;
  }

  char **forward_argv = NULL;
  char *include_arg = NULL;
  char *lib_arg = NULL;
  int forward_argc = 0;
  if (build_forward_argv_with_root(argc, argv, &forward_argc, &forward_argv, &include_arg, &lib_arg) != 0)
    return 1;
  int result = xcc_embedded_main(forward_argc, forward_argv);
  free(include_arg);
  free(lib_arg);
  free(forward_argv);
  return result;
}
