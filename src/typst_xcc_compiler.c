#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define main xcc_embedded_main
#include "wcc.c"
#undef main
#include "wasm_linker.c"

__attribute__((import_module("typst_env"), import_name("wasm_minimal_protocol_write_args_to_buffer")))
extern void wasm_minimal_protocol_write_args_to_buffer(uint8_t *ptr);

__attribute__((import_module("typst_env"), import_name("wasm_minimal_protocol_send_result_to_host")))
extern void wasm_minimal_protocol_send_result_to_host(uint8_t *ptr, size_t len);

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

static int wrapper_compile_csource_string_to_object_buffer(
    const char *src_name, const char *source, unsigned char **out, size_t *out_size, Options *opts) {
  char *ppbuf = NULL;
  size_t ppsize = 0;
  FILE *ppout = open_memstream(&ppbuf, &ppsize);
  if (ppout == NULL)
    error("cannot open preprocess memstream");

  init_preprocessor(ppout);
  for (int i = 0; i < opts->defines->len; ++i) {
    const char *def = opts->defines->data[i];
    define_macro(def);
  }

  FILE *input = fmemopen((void*)source, strlen(source), "r");
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

static int wrapper_compile_source_to_wasm_buffer(
    const char *source_name, const char *source, unsigned char **out, size_t *out_size) {
  Vector *defines = new_vector();
  append_predefined_macros(defines);

  Options opts = {
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

  unsigned char *objbuf = NULL;
  size_t objsize = 0;
  int result = wrapper_compile_csource_string_to_object_buffer(source_name, source, &objbuf, &objsize, &opts);
  if (result != 0)
    return result;

  functypes = new_vector();
  tags = new_vector();
  tables = new_vector();
  table_init(&indirect_function_table);

  WasmLinker linker_body;
  WasmLinker *linker = &linker_body;
  linker_init(linker);
  linker->options = opts.linker_opts;

  if (!read_wasm_obj_buffer(linker, source_name, objbuf, objsize)) {
    free(objbuf);
    return 1;
  }
  free(objbuf);

  if (!link_wasm_objs(linker, opts.exports, opts.stack_size))
    return 2;

  char *wasm_buf = NULL;
  size_t wasm_size = 0;
  FILE *wasm_out = open_memstream(&wasm_buf, &wasm_size);
  if (wasm_out == NULL)
    error("cannot open wasm memstream");
  if (!linker_emit_wasm_to_file(linker, wasm_out, opts.exports)) {
    fclose(wasm_out);
    free(wasm_buf);
    return 2;
  }
  fclose(wasm_out);

  *out = (unsigned char*)wasm_buf;
  *out_size = wasm_size;
  return 0;
}

int compile(size_t source_len) {
  uint8_t *source = malloc(source_len + 1);
  if (source == NULL)
    return 1;

  wasm_minimal_protocol_write_args_to_buffer(source);
  source[source_len] = '\0';

  unsigned char *output = NULL;
  size_t output_size = 0;
  int result = wrapper_compile_source_to_wasm_buffer("*typst*", (const char*)source, &output, &output_size);
  free(source);
  if (result != 0 || output == NULL)
    return 1;

  wasm_minimal_protocol_send_result_to_host(output, output_size);
  free(output);
  return 0;
}
