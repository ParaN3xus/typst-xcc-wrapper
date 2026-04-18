#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cbor.h"
#include "vfs/vfs.h"
#include "wasi_stub.h"

#define main xcc_embedded_main
#include "wcc.c"
#undef main
#include "wasm_linker.c"

#if defined(__clang__)
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Wunknown-attributes"
#endif

__attribute__((import_module("typst_env"), import_name("wasm_minimal_protocol_write_args_to_buffer")))
extern void wasm_minimal_protocol_write_args_to_buffer(uint8_t *ptr);

__attribute__((import_module("typst_env"), import_name("wasm_minimal_protocol_send_result_to_host")))
extern void wasm_minimal_protocol_send_result_to_host(uint8_t *ptr, size_t len);

#define TYPST_VFS_INCLUDE_ROOT "/__typst_vfs"

typedef struct ObjectBuffer {
  unsigned char *buffer;
  size_t size;
  const char *name;
} ObjectBuffer;

typedef struct SourcePackage {
  TypstVfsFile *files;
  size_t file_count;
  char *entry_path;
} SourcePackage;

typedef struct DiagnosticEntry {
  const char *level;
  const char *message;
  size_t message_len;
} DiagnosticEntry;

static void append_predefined_macros(Vector *defines) {
  static const char *kPredefinedMacros[] = {
    "__XCC",
    "__ILP32__",
    "__wasm",
    "__wasm32",
    "__STDC__",
    "__STDC_VERSION__=199901L",
    "__SIZEOF_POINTER__=4",
    "__SIZEOF_INT__=4",
    "__SIZEOF_LONG__=4",
    "__SIZEOF_LONG_LONG__=8",
    "__SIZEOF_SIZE_T__=4",
#if defined(__NO_FLONUM)
    "__NO_FLONUM",
#endif
#if defined(__NO_BITFIELD)
    "__NO_BITFIELD",
#endif
#if defined(__NO_VLA)
    "__NO_VLA",
    "__STDC_NO_VLA__",
#endif
#if defined(__NO_WCHAR)
    "__NO_WCHAR",
#endif
  };

  for (size_t i = 0; i < ARRAY_SIZE(kPredefinedMacros); ++i)
    vec_push(defines, (void*)kPredefinedMacros[i]);
}

static bool has_suffix(const char *str, const char *suffix) {
  size_t str_len = strlen(str);
  size_t suffix_len = strlen(suffix);
  if (str_len < suffix_len)
    return false;
  return strcmp(str + str_len - suffix_len, suffix) == 0;
}

static void free_source_package(SourcePackage *package) {
  if (package == NULL)
    return;

  if (package->files != NULL) {
    for (size_t i = 0; i < package->file_count; ++i) {
      free((char*)package->files[i].path);
      free((unsigned char*)package->files[i].content);
    }
  }
  free(package->files);
  free(package->entry_path);
  package->files = NULL;
  package->file_count = 0;
  package->entry_path = NULL;
}

static bool cbor_dup_text_string_value(CborValue *value, char **out) {
  size_t len = 0;
  CborError err = cbor_value_calculate_string_length(value, &len);
  if (err != CborNoError)
    return false;

  char *buffer = calloc_or_die(len + 1);
  err = cbor_value_copy_text_string(value, buffer, &len, value);
  if (err != CborNoError) {
    free(buffer);
    return false;
  }

  buffer[len] = '\0';
  *out = buffer;
  return true;
}

static bool cbor_dup_content_value(CborValue *value, unsigned char **out_content, size_t *out_len) {
  if (cbor_value_is_byte_string(value)) {
    size_t len = 0;
    CborError err = cbor_value_calculate_string_length(value, &len);
    if (err != CborNoError)
      return false;

    unsigned char *buffer = calloc_or_die(len + 1);
    err = cbor_value_copy_byte_string(value, buffer, &len, value);
    if (err != CborNoError) {
      free(buffer);
      return false;
    }

    *out_content = buffer;
    *out_len = len;
    return true;
  }

  if (cbor_value_is_text_string(value)) {
    char *text = NULL;
    if (!cbor_dup_text_string_value(value, &text))
      return false;
    *out_content = (unsigned char*)text;
    *out_len = strlen(text);
    return true;
  }

  return false;
}

static bool parse_cbor_file_entry(CborValue *value, TypstVfsFile *out_file) {
  if (!cbor_value_is_map(value))
    return false;

  CborValue path_value;
  CborValue content_value;
  if (cbor_value_map_find_value(value, "path", &path_value) != CborNoError ||
      cbor_value_map_find_value(value, "content", &content_value) != CborNoError)
    return false;
  if (!cbor_value_is_text_string(&path_value))
    return false;

  char *raw_path = NULL;
  if (!cbor_dup_text_string_value(&path_value, &raw_path))
    return false;

  unsigned char *content = NULL;
  size_t content_len = 0;
  if (!cbor_dup_content_value(&content_value, &content, &content_len)) {
    free(raw_path);
    return false;
  }

  char *normalized_path = typst_vfs_normalize_path(raw_path);
  free(raw_path);
  if (normalized_path == NULL) {
    free(content);
    return false;
  }

  out_file->path = normalized_path;
  out_file->content = content;
  out_file->len = content_len;
  return true;
}

static bool parse_cbor_file_array(CborValue *array_value, SourcePackage *out_package) {
  CborValue item;
  CborError err = cbor_value_enter_container(array_value, &item);
  if (err != CborNoError)
    return false;

  size_t count = 0;
  CborValue counter = item;
  while (!cbor_value_at_end(&counter)) {
    ++count;
    err = cbor_value_advance(&counter);
    if (err != CborNoError)
      return false;
  }

  out_package->files = calloc_or_die(sizeof(*out_package->files) * count);
  out_package->file_count = count;

  size_t index = 0;
  while (!cbor_value_at_end(&item)) {
    if (!parse_cbor_file_entry(&item, &out_package->files[index])) {
      free_source_package(out_package);
      return false;
    }
    if (out_package->entry_path == NULL && has_suffix(out_package->files[index].path, ".c")) {
      out_package->entry_path = strdup(out_package->files[index].path);
      if (out_package->entry_path == NULL) {
        free_source_package(out_package);
        return false;
      }
    }

    err = cbor_value_advance(&item);
    if (err != CborNoError) {
      free_source_package(out_package);
      return false;
    }
    ++index;
  }

  return true;
}

static bool parse_source_package(const uint8_t *data, size_t size, SourcePackage *out_package) {
  memset(out_package, 0, sizeof(*out_package));

  CborParser parser;
  CborValue root;
  CborError err = cbor_parser_init(data, size, 0, &parser, &root);
  if (err != CborNoError)
    return false;

  if (cbor_value_is_array(&root)) {
    if (!parse_cbor_file_array(&root, out_package))
      return false;
  } else if (cbor_value_is_map(&root)) {
    CborValue files_value;
    CborValue entry_value;
    if (cbor_value_map_find_value(&root, "files", &files_value) != CborNoError ||
        !cbor_value_is_array(&files_value))
      return false;
    if (!parse_cbor_file_array(&files_value, out_package))
      return false;

    if (cbor_value_map_find_value(&root, "entry", &entry_value) == CborNoError &&
        !cbor_value_is_undefined(&entry_value)) {
      if (!cbor_value_is_text_string(&entry_value)) {
        free_source_package(out_package);
        return false;
      }

      char *raw_entry = NULL;
      if (!cbor_dup_text_string_value(&entry_value, &raw_entry)) {
        free_source_package(out_package);
        return false;
      }

      char *normalized_entry = typst_vfs_normalize_path(raw_entry);
      free(raw_entry);
      if (normalized_entry == NULL) {
        free_source_package(out_package);
        return false;
      }

      free(out_package->entry_path);
      out_package->entry_path = normalized_entry;
    }
  } else {
    return false;
  }

  if (out_package->file_count == 0 || out_package->entry_path == NULL) {
    free_source_package(out_package);
    return false;
  }

  return true;
}

static int wrapper_compile_csource_string_to_object_buffer(
    const char *src_name,
    const unsigned char *source,
    size_t source_len,
    unsigned char **out,
    size_t *out_size,
    Options *opts) {
  char *ppbuf = NULL;
  size_t ppsize = 0;
  FILE *ppout = open_memstream(&ppbuf, &ppsize);
  if (ppout == NULL)
    error("cannot open preprocess memstream");

  init_preprocessor(ppout);
  if (typst_vfs_is_active())
    add_inc_path(INC_NORMAL, TYPST_VFS_INCLUDE_ROOT);

  for (int i = 0; i < opts->defines->len; ++i) {
    const char *def = opts->defines->data[i];
    define_macro(def);
  }

  FILE *input = fmemopen((void*)source, source_len, "r");
  if (input == NULL)
    error("cannot open source memstream");
  preprocess(input, src_name);
  fclose(input);
  fclose(ppout);

  init_compiler();
  Vector *toplevel = new_vector();
  FILE *ppin = fmemopen(ppbuf, ppsize, "r");
  if (ppin == NULL)
    error("cannot reopen preprocessed memstream");
  compilec(ppin, src_name, toplevel);
  fclose(ppin);
  free(ppbuf);

  traverse_ast(toplevel);
  if (compile_error_count != 0)
    return 1;

  gen(toplevel);
  if (compile_error_count != 0)
    return 1;
  if (cc_flags.warn_as_error && compile_warning_count != 0)
    return 2;

  char *objbuf = NULL;
  size_t objsize = 0;
  FILE *ofp = open_memstream(&objbuf, &objsize);
  if (ofp == NULL)
    error("cannot open object memstream");
  emit_wasm(ofp, opts->linker_opts.import_module_name, NULL);
  fclose(ofp);

  *out = (unsigned char*)objbuf;
  *out_size = objsize;
  return 0;
}

static bool read_wasm_obj_buffer(WasmLinker *linker, const char *name, const void *data, size_t size) {
  FILE *fp = fmemopen((void*)data, size, "rb");
  if (fp == NULL)
    return false;

  WasmObj *wasmobj = read_wasm(fp, name, size);
  fclose(fp);
  if (wasmobj == NULL)
    return false;

  File *file = calloc_or_die(sizeof(*file));
  file->filename = name;
  file->kind = FK_WASMOBJ;
  file->wasmobj = wasmobj;
  vec_push(linker->files, file);
  return true;
}

static bool linker_emit_wasm_to_file(WasmLinker *linker, FILE *ofp, Table *exports) {
  linker->ofp = ofp;

  write_wasm_header(ofp);

  EmitWasm ew_body = {
    .ofp = ofp,
  };
  EmitWasm *ew = &ew_body;

  emit_type_section(ew);
  out_import_section(linker);
  out_function_section(linker);
  out_table_section(linker);
  out_memory_section(linker);
  emit_tag_section(ew);
  out_global_section(linker);
  out_export_section(linker, exports);
  out_elems_section(linker);
  out_code_section(linker);
  out_data_section(linker);
  return true;
}

static int link_object_buffers_to_wasm(
    const ObjectBuffer *objects, size_t object_count, unsigned char **out, size_t *out_size, Options *opts) {
  functypes = new_vector();
  tags = new_vector();
  tables = new_vector();
  table_init(&indirect_function_table);

  WasmLinker linker_body;
  WasmLinker *linker = &linker_body;
  linker_init(linker);
  linker->options = opts->linker_opts;

  for (size_t i = 0; i < object_count; ++i) {
    if (!read_wasm_obj_buffer(linker, objects[i].name, objects[i].buffer, objects[i].size))
      return 1;
  }

  if (!link_wasm_objs(linker, opts->exports, opts->stack_size))
    return 2;

  char *wasm_buf = NULL;
  size_t wasm_size = 0;
  FILE *wasm_out = open_memstream(&wasm_buf, &wasm_size);
  if (wasm_out == NULL)
    error("cannot open wasm memstream");
  if (!linker_emit_wasm_to_file(linker, wasm_out, opts->exports)) {
    fclose(wasm_out);
    free(wasm_buf);
    return 2;
  }
  fclose(wasm_out);

  *out = (unsigned char*)wasm_buf;
  *out_size = wasm_size;
  return 0;
}

static void init_wrapper_options(Options *opts) {
  Vector *defines = new_vector();
  append_predefined_macros(defines);

  *opts = (Options){
    .exports = alloc_table(),
    .lib_paths = new_vector(),
    .defines = defines,
    .sources = new_vector(),
    .root = ".",
    .ofn = NULL,
    .entry_point = "",
    .out_type = OutExecutable,
    .src_type = Clanguage,
    .stack_size = DEFAULT_STACK_SIZE,
    .nodefaultlibs = true,
    .nostdlib = true,
    .nostdinc = true,
    .linker_opts = {
      .import_module_name = DEFAULT_IMPORT_MODULE_NAME,
      .allow_undefined = true,
      .export_all = true,
    },
  };
}

static int wrapper_compile_single_source_to_wasm_buffer(
    const char *source_name, const unsigned char *source, size_t source_len, unsigned char **out, size_t *out_size) {
  Options opts;
  init_wrapper_options(&opts);

  ObjectBuffer obj = {0};
  int result = wrapper_compile_csource_string_to_object_buffer(
      source_name, source, source_len, &obj.buffer, &obj.size, &opts);
  if (result != 0)
    return result;

  obj.name = source_name;
  result = link_object_buffers_to_wasm(&obj, 1, out, out_size, &opts);
  free(obj.buffer);
  return result;
}

static int wrapper_compile_package_to_wasm_buffer(
    SourcePackage *package, unsigned char **out, size_t *out_size) {
  Options opts;
  init_wrapper_options(&opts);

  size_t source_count = 0;
  for (size_t i = 0; i < package->file_count; ++i) {
    if (has_suffix(package->files[i].path, ".c"))
      ++source_count;
  }
  if (source_count == 0)
    return 1;

  ObjectBuffer *objects = calloc_or_die(sizeof(*objects) * source_count);
  typst_vfs_set_files(package->files, package->file_count);

  size_t object_index = 0;
  int result = 0;

  for (size_t i = 0; i < package->file_count; ++i) {
    if (!has_suffix(package->files[i].path, ".c") ||
        strcmp(package->files[i].path, package->entry_path) != 0)
      continue;

    objects[object_index].name = package->files[i].path;
    result = wrapper_compile_csource_string_to_object_buffer(
        package->files[i].path,
        package->files[i].content,
        package->files[i].len,
        &objects[object_index].buffer,
        &objects[object_index].size,
        &opts);
    if (result != 0)
      break;
    ++object_index;
    break;
  }

  if (result == 0 && object_index == 0)
    result = 1;

  for (size_t i = 0; i < package->file_count; ++i) {
    if (!has_suffix(package->files[i].path, ".c"))
      continue;
    if (strcmp(package->files[i].path, package->entry_path) == 0)
      continue;

    objects[object_index].name = package->files[i].path;
    result = wrapper_compile_csource_string_to_object_buffer(
        package->files[i].path,
        package->files[i].content,
        package->files[i].len,
        &objects[object_index].buffer,
        &objects[object_index].size,
        &opts);
    if (result != 0)
      break;
    ++object_index;
  }

  if (result == 0)
    result = link_object_buffers_to_wasm(objects, object_index, out, out_size, &opts);

  for (size_t i = 0; i < object_index; ++i)
    free(objects[i].buffer);
  free(objects);
  typst_vfs_reset();
  return result;
}

static bool is_wasm_binary(const unsigned char *data, size_t len) {
  static const unsigned char kWasmMagic[] = {0x00, 0x61, 0x73, 0x6d};
  return len >= sizeof(kWasmMagic) && memcmp(data, kWasmMagic, sizeof(kWasmMagic)) == 0;
}

static bool contains_substring_n(
    const char *haystack, size_t haystack_len, const char *needle, size_t needle_len) {
  if (needle_len == 0 || haystack_len < needle_len)
    return false;

  for (size_t i = 0; i + needle_len <= haystack_len; ++i) {
    if (memcmp(haystack + i, needle, needle_len) == 0)
      return true;
  }
  return false;
}

static const char *diagnostic_level_for_message(const char *message, size_t message_len) {
  if (contains_substring_n(message, message_len, "warning", 7))
    return "warning";
  if (contains_substring_n(message, message_len, "note", 4))
    return "note";
  return "error";
}

static size_t collect_diagnostics(
    const char *diagnostics,
    size_t diagnostics_len,
    const char *fallback_message,
    DiagnosticEntry **out_entries) {
  size_t count = 0;
  if (diagnostics != NULL) {
    const char *line = diagnostics;
    const char *end = diagnostics + diagnostics_len;
    while (line < end) {
      const char *next = memchr(line, '\n', (size_t)(end - line));
      if (next == NULL)
        next = end;
      if (next > line) {
        ++count;
      }
      line = next < end ? next + 1 : end;
    }
  }

  if (count == 0) {
    DiagnosticEntry *entries = calloc_or_die(sizeof(*entries));
    entries[0] = (DiagnosticEntry){
      .level = "error",
      .message = fallback_message,
      .message_len = strlen(fallback_message),
    };
    *out_entries = entries;
    return 1;
  }

  DiagnosticEntry *entries = calloc_or_die(sizeof(*entries) * count);
  size_t index = 0;
  const char *line = diagnostics;
  const char *end = diagnostics + diagnostics_len;
  while (line < end) {
    const char *next = memchr(line, '\n', (size_t)(end - line));
    if (next == NULL)
      next = end;
    if (next > line) {
      entries[index++] = (DiagnosticEntry){
        .level = diagnostic_level_for_message(line, (size_t)(next - line)),
        .message = line,
        .message_len = (size_t)(next - line),
      };
    }
    line = next < end ? next + 1 : end;
  }

  *out_entries = entries;
  return index;
}

static bool encode_compile_response(
    bool ok,
    const unsigned char *artifact,
    size_t artifact_len,
    const char *diagnostics,
    size_t diagnostics_len,
    const char *fallback_message,
    unsigned char **out,
    size_t *out_len) {
  DiagnosticEntry *entries = NULL;
  size_t entry_count = collect_diagnostics(diagnostics, diagnostics_len, fallback_message, &entries);
  size_t capacity = artifact_len + diagnostics_len + entry_count * 96 + 512;
  unsigned char *buffer = calloc_or_die(capacity);

  CborEncoder encoder;
  CborEncoder root;
  CborEncoder diagnostics_array;
  cbor_encoder_init(&encoder, buffer, capacity, 0);

  CborError err = cbor_encoder_create_map(&encoder, &root, 3);
  if (err == CborNoError)
    err = cbor_encode_text_stringz(&root, "ok");
  if (err == CborNoError)
    err = cbor_encode_boolean(&root, ok);
  if (err == CborNoError)
    err = cbor_encode_text_stringz(&root, "artifact");
  if (err == CborNoError) {
    if (ok && artifact != NULL) {
      err = cbor_encode_byte_string(&root, artifact, artifact_len);
    } else {
      err = cbor_encode_null(&root);
    }
  }
  if (err == CborNoError)
    err = cbor_encode_text_stringz(&root, "diagnostics");
  if (err == CborNoError)
    err = cbor_encoder_create_array(&root, &diagnostics_array, entry_count);

  for (size_t i = 0; err == CborNoError && i < entry_count; ++i) {
    CborEncoder item;
    err = cbor_encoder_create_map(&diagnostics_array, &item, 2);
    if (err == CborNoError)
      err = cbor_encode_text_stringz(&item, "level");
    if (err == CborNoError)
      err = cbor_encode_text_stringz(&item, entries[i].level);
    if (err == CborNoError)
      err = cbor_encode_text_stringz(&item, "message");
    if (err == CborNoError)
      err = cbor_encode_text_string(&item, entries[i].message, entries[i].message_len);
    if (err == CborNoError)
      err = cbor_encoder_close_container(&diagnostics_array, &item);
  }

  if (err == CborNoError)
    err = cbor_encoder_close_container(&root, &diagnostics_array);
  if (err == CborNoError)
    err = cbor_encoder_close_container(&encoder, &root);

  free(entries);
  if (err != CborNoError) {
    free(buffer);
    return false;
  }

  *out = buffer;
  *out_len = cbor_encoder_get_buffer_size(&encoder, buffer);
  return true;
}

int compile(size_t source_len) {
  uint8_t *source = malloc(source_len + 1);
  if (source == NULL)
    return 1;

  wasm_minimal_protocol_write_args_to_buffer(source);
  source[source_len] = '\0';

  unsigned char *output = NULL;
  size_t output_size = 0;
  unsigned char *response = NULL;
  size_t response_size = 0;
  int result = 0;

  SourcePackage package;
  typst_wasi_reset_diagnostics();
  if (parse_source_package(source, source_len, &package)) {
    result = wrapper_compile_package_to_wasm_buffer(&package, &output, &output_size);
    free_source_package(&package);
  } else {
    result = wrapper_compile_single_source_to_wasm_buffer("*typst*", source, source_len, &output, &output_size);
  }

  free(source);
  size_t diagnostics_len = 0;
  const char *diagnostics = typst_wasi_diagnostics(&diagnostics_len);
  bool ok = result == 0 && output != NULL && is_wasm_binary(output, output_size);
  const char *fallback_message = "C compilation failed";

  if (!encode_compile_response(
          ok,
          ok ? output : NULL,
          ok ? output_size : 0,
          diagnostics,
          diagnostics_len,
          fallback_message,
          &response,
          &response_size)) {
    free(output);
    return 1;
  }

  wasm_minimal_protocol_send_result_to_host(response, response_size);
  free(response);
  free(output);
  return 0;
}
