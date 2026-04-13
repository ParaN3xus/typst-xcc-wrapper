# typst-xcc-wrapper

Build the compiler plugin:

```sh
pixi run build
```

Then use [lib.typ](/home/paran3xus/projects/rust/wwaassmm/lib.typ:1) from Typst:

```typ
#import "lib.typ": code-file, compile-project, export

#let compiled = compile-project(
  (
    code-file("src/main.c", ```c
      #include "typst/export.typst_plugin.h"

      static int add_impl(int a, int b) {
        return a + b;
      }
    ```),
  ),
  entry: "src/main.c",
  exports: (
    export("add", args: ("int", "int"), ret: "int"),
    export("hello", ret: "const char*"),
  ),
)
```

`compile-project(...)` now does four things:

1. Generates `typst/export.typst_plugin.h` from the `exports` metadata.
2. Packages the project files as CBOR.
3. Sends that CBOR payload to `build/typst_xcc_compiler.wasm`.
4. Immediately loads the returned wasm bytes as a Typst plugin.

Current export generation supports standard C type names instead of `i32`-style
tags. The useful subset right now is:

- Fixed-width scalar-like wasm32 values:
  `char`, `signed char`, `unsigned char`, `short`, `unsigned short`,
  `int`, `unsigned int`, `long`, `unsigned long`, `long long`,
  `unsigned long long`, `float`, `double`
- Variable-length pointer arguments:
  `char*`, `const char*`, `unsigned char*`, `const unsigned char*`
- Variable-length string return values:
  `char*`, `const char*`
- `void` return values
