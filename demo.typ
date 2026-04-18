#import "typ/lib.typ": code-file, compile-project, export

#set page(height: auto)

#let compiled = compile-project(
  (
    code-file("main.c", ```c
      #include "typst/export.typst_plugin.h"

      static int add_impl(int a, int b) {
        return a + b;
      }

      static int measure_impl(const char *text) {
        int len = 0;
        while (text[len] != '\0') {
          ++len;
        }
        return len;
      }

      static const char *hello_impl(void) {
        return "hello";
      }

      static double half_impl(double value) {
        return value / 2.0;
      }
    ```),
  ),
  entry: "main.c",
  exports: (
    export("add", args: ("int", "int"), ret: "int"),
    export("measure", args: ("const char*",), ret: "int"),
    export("hello", ret: "const char*"),
    export("half", args: ("double",), ret: "double"),
  ),
)

Compile OK: #compiled.ok

#let compiled = plugin(compiled.artifact)

#let answer = compiled.add(20.to-bytes(size: 4), 22.to-bytes(size: 4))
#let measured = compiled.measure(bytes("typst"))
#let hello = compiled.hello()
#let halved = compiled.half(3.5.to-bytes())

- #int.from-bytes(answer)
- #int.from-bytes(measured)
- #str(hello)
- #float.from-bytes(halved)

Typst compiled the C source through `typst_xcc_compiler.wasm`, loaded the returned
wasm bytes as a plugin, and executed it.
