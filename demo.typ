#let compiler = plugin("build/typst_xcc_compiler.wasm")

#let pkg = cbor.encode((
  entry: "src/main.c",
  files: (
    (
      path: "src/main.c",
      content: ```c
        #include "include/typst_helpers.h"

        __attribute__((import_module("typst_env"), import_name("wasm_minimal_protocol_write_args_to_buffer")))
        extern void wasm_minimal_protocol_write_args_to_buffer(unsigned char *ptr);

        __attribute__((import_module("typst_env"), import_name("wasm_minimal_protocol_send_result_to_host")))
        extern void wasm_minimal_protocol_send_result_to_host(unsigned char *ptr, int len);

        int add(int a_len, int b_len) {
          unsigned char args_buf[8];
          unsigned char result_buf[4];
          int result;

          if (a_len != 4 || b_len != 4) {
            return 1;
          }

          wasm_minimal_protocol_write_args_to_buffer(args_buf);
          result = add_impl(read_i32_le(args_buf), read_i32_le(args_buf + 4));
          write_i32_le(result_buf, result);
          wasm_minimal_protocol_send_result_to_host(result_buf, 4);
          return 0;
        }
      ```.text,
    ),
    (
      path: "include/typst_helpers.h",
      content: ```c
        static int read_i32_le(const unsigned char *ptr) {
          return ((int)ptr[0]) |
                 ((int)ptr[1] << 8) |
                 ((int)ptr[2] << 16) |
                 ((int)ptr[3] << 24);
        }

        static void write_i32_le(unsigned char *ptr, int value) {
          ptr[0] = (unsigned char)(value & 0xff);
          ptr[1] = (unsigned char)((value >> 8) & 0xff);
          ptr[2] = (unsigned char)((value >> 16) & 0xff);
          ptr[3] = (unsigned char)((value >> 24) & 0xff);
        }

        static int add_impl(int a, int b) {
          return a + b;
        }
      ```.text,
    ),
  ),
))

#let compiled = plugin(compiler.compile(pkg))
#let answer = compiled.add(20.to-bytes(size: 4), 22.to-bytes(size: 4))
#assert.eq(int.from-bytes(answer), 42)

Typst encoded a multi-file C project as CBOR, the compiler plugin resolved the
header through its VFS, compiled the wasm plugin, and Typst executed it.
