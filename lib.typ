#let default-compiler-path = "build/typst_xcc_compiler.wasm"
#let default-export-header-path = "typst/export.typst_plugin.h"

#let file(path, content) = (
  path: path,
  content: content,
)

#let code-file(path, body) = file(path, body.text)

#let export(name, args: (), ret: none, impl: none) = {
  if ret == none {
    panic("export `" + name + "` must specify `ret` explicitly")
  }

  (
    name: name,
    args: args,
    ret: ret,
    impl: if impl == none { name + "_impl" } else { impl },
  )
}

#let package(files, entry: none) = {
  if entry == none {
    cbor.encode(files)
  } else {
    cbor.encode((
      entry: entry,
      files: files,
    ))
  }
}

#let compile-bytes(source, compiler-path: default-compiler-path) = {
  let compiler = plugin(compiler-path)
  compiler.compile(source)
}

#let _canonical-type(type) = type.replace(" ", "")

#let _c-type(type) = {
  let canonical = _canonical-type(type)
  if canonical == "void" {
    "void"
  } else if canonical == "char" {
    "char"
  } else if canonical == "signedchar" {
    "signed char"
  } else if canonical == "unsignedchar" {
    "unsigned char"
  } else if canonical == "short" {
    "short"
  } else if canonical == "unsignedshort" {
    "unsigned short"
  } else if canonical == "int" {
    "int"
  } else if canonical == "unsignedint" {
    "unsigned int"
  } else if canonical == "long" {
    "long"
  } else if canonical == "unsignedlong" {
    "unsigned long"
  } else if canonical == "longlong" {
    "long long"
  } else if canonical == "unsignedlonglong" {
    "unsigned long long"
  } else if canonical == "float" {
    "float"
  } else if canonical == "double" {
    "double"
  } else if canonical == "char*" {
    "char *"
  } else if canonical == "constchar*" {
    "const char *"
  } else if canonical == "unsignedchar*" {
    "unsigned char *"
  } else if canonical == "constunsignedchar*" {
    "const unsigned char *"
  } else {
    panic("unsupported C type: " + repr(type))
  }
}

#let _is-c-string(type) = {
  let canonical = _canonical-type(type)
  canonical == "char*" or canonical == "constchar*"
}

#let _is-byte-pointer(type) = {
  let canonical = _canonical-type(type)
  canonical == "unsignedchar*" or canonical == "constunsignedchar*"
}

#let _is-pointer-like(type) = _is-c-string(type) or _is-byte-pointer(type)

#let _storage-element-type(type) = {
  if _is-c-string(type) {
    "char"
  } else if _is-byte-pointer(type) {
    "unsigned char"
  } else {
    panic("type does not need pointer storage: " + repr(type))
  }
}

#let _byte-width(type) = {
  let canonical = _canonical-type(type)
  if canonical == "char" or canonical == "signedchar" or canonical == "unsignedchar" {
    1
  } else if canonical == "short" or canonical == "unsignedshort" {
    2
  } else if canonical == "int" or canonical == "unsignedint" or canonical == "long" or canonical == "unsignedlong" {
    4
  } else if canonical == "float" {
    4
  } else if canonical == "longlong" or canonical == "unsignedlonglong" {
    8
  } else if canonical == "double" {
    8
  } else {
    panic("type does not have a fixed wasm byte width: " + repr(type))
  }
}

#let _read-fn(type) = {
  let canonical = _canonical-type(type)
  if canonical == "char" or canonical == "signedchar" {
    "typst_read_i8"
  } else if canonical == "unsignedchar" {
    "typst_read_u8"
  } else if canonical == "short" {
    "typst_read_i16_le"
  } else if canonical == "unsignedshort" {
    "typst_read_u16_le"
  } else if canonical == "int" or canonical == "long" {
    "typst_read_i32_le"
  } else if canonical == "unsignedint" or canonical == "unsignedlong" {
    "typst_read_u32_le"
  } else if canonical == "float" {
    "typst_read_f32_le"
  } else if canonical == "longlong" {
    "typst_read_i64_le"
  } else if canonical == "unsignedlonglong" {
    "typst_read_u64_le"
  } else if canonical == "double" {
    "typst_read_f64_le"
  } else {
    panic("unsupported fixed-width read type: " + repr(type))
  }
}

#let _write-fn(type) = {
  let canonical = _canonical-type(type)
  if canonical == "char" or canonical == "signedchar" {
    "typst_write_i8"
  } else if canonical == "unsignedchar" {
    "typst_write_u8"
  } else if canonical == "short" {
    "typst_write_i16_le"
  } else if canonical == "unsignedshort" {
    "typst_write_u16_le"
  } else if canonical == "int" or canonical == "long" {
    "typst_write_i32_le"
  } else if canonical == "unsignedint" or canonical == "unsignedlong" {
    "typst_write_u32_le"
  } else if canonical == "float" {
    "typst_write_f32_le"
  } else if canonical == "longlong" {
    "typst_write_i64_le"
  } else if canonical == "unsignedlonglong" {
    "typst_write_u64_le"
  } else if canonical == "double" {
    "typst_write_f64_le"
  } else {
    panic("unsupported fixed-width write type: " + repr(type))
  }
}

#let _offset-expr(index) = {
  if index == 0 {
    "0"
  } else {
    range(0, index).map(i => "arg" + str(i) + "_len").join(" + ")
  }
}

#let _impl-param-list(spec) = {
  if spec.args.len() == 0 {
    "void"
  } else {
    spec.args.map(_c-type).join(", ")
  }
}

#let _impl-call-arg(spec, index) = {
  let ty = spec.args.at(index)
  if _is-pointer-like(ty) {
    "arg" + str(index) + "_storage"
  } else {
    _read-fn(ty) + "(args_buf + " + _offset-expr(index) + ")"
  }
}

#let _emit-arg-setup(spec, index) = {
  let ty = spec.args.at(index)
  let offset = _offset-expr(index)
  let storage = "arg" + str(index) + "_storage"
  let len = "arg" + str(index) + "_len"
  let loop = "arg" + str(index) + "_i"

  if _is-c-string(ty) {
    (
      "  " + _storage-element-type(ty) + " " + storage + "[" + len + " + 1];\n",
      "  for (int " + loop + " = 0; " + loop + " < " + len + "; ++" + loop + ") {\n",
      "    " + storage + "[" + loop + "] = (char)args_buf[" + offset + " + " + loop + "];\n",
      "  }\n",
      "  " + storage + "[" + len + "] = '\\0';\n",
    ).join("")
  } else if _is-byte-pointer(ty) {
    (
      "  " + _storage-element-type(ty) + " " + storage + "[" + len + " + 1];\n",
      "  for (int " + loop + " = 0; " + loop + " < " + len + "; ++" + loop + ") {\n",
      "    " + storage + "[" + loop + "] = args_buf[" + offset + " + " + loop + "];\n",
      "  }\n",
      "  " + storage + "[" + len + "] = 0;\n",
    ).join("")
  } else {
    ""
  }
}

#let _emit-impl-prototype(spec) = {
  _c-type(spec.ret) + " " + spec.impl + "(" + _impl-param-list(spec) + ");"
}

#let _emit-export-wrapper(spec) = {
  let arg-count = spec.args.len()
  let params = if arg-count == 0 {
    "void"
  } else {
    range(0, arg-count).map(i => "int arg" + str(i) + "_len").join(", ")
  }
  let total-len = if arg-count == 0 {
    "0"
  } else {
    range(0, arg-count).map(i => "arg" + str(i) + "_len").join(" + ")
  }
  let checks = range(0, arg-count)
    .filter(i => not _is-pointer-like(spec.args.at(i)))
    .map(i => "arg" + str(i) + "_len != " + str(_byte-width(spec.args.at(i))))
  let call = spec.impl + "(" + range(0, arg-count).map(i => _impl-call-arg(spec, i)).join(", ") + ")"
  let arg-setup = range(0, arg-count).map(i => _emit-arg-setup(spec, i)).join("")
  let ret-type = _canonical-type(spec.ret)

  (
    "int " + spec.name + "(" + params + ") {\n",
    if arg-count == 0 { "" } else { "  unsigned char args_buf[" + total-len + "];\n" },
    if ret-type == "void" {
      "  unsigned char result_buf[1];\n"
    } else if _is-c-string(spec.ret) {
      "  " + _c-type(spec.ret) + " result;\n  int result_len;\n"
    } else {
      (
        "  unsigned char result_buf[" + str(_byte-width(spec.ret)) + "];\n",
        "  " + _c-type(spec.ret) + " result;\n",
      ).join("")
    },
    "\n",
    if checks.len() == 0 {
      ""
    } else {
      (
        "  if (" + checks.join(" || ") + ") {\n",
        "    return 1;\n",
        "  }\n\n",
      ).join("")
    },
    if arg-count == 0 { "" } else { "  wasm_minimal_protocol_write_args_to_buffer(args_buf);\n" },
    if arg-setup == "" { "" } else { arg-setup + "\n" },
    if ret-type == "void" {
      (
        "  " + call + ";\n",
        "  wasm_minimal_protocol_send_result_to_host(result_buf, 0);\n",
      ).join("")
    } else if _is-c-string(spec.ret) {
      (
        "  result = " + call + ";\n",
        "  result_len = typst_c_string_len(result);\n",
        "  wasm_minimal_protocol_send_result_to_host((unsigned char*)result, result_len);\n",
      ).join("")
    } else {
      (
        "  result = " + call + ";\n",
        "  " + _write-fn(spec.ret) + "(result_buf, result);\n",
        "  wasm_minimal_protocol_send_result_to_host(result_buf, " + str(_byte-width(spec.ret)) + ");\n",
      ).join("")
    },
    "  return 0;\n",
    "}",
  ).join("")
}

#let emit-export-header(exports) = {
  let prototypes = exports.map(_emit-impl-prototype).join("\n")
  let wrappers = exports.map(_emit-export-wrapper).join("\n\n")
  (
    ```c
      __attribute__((import_module("typst_env"), import_name("wasm_minimal_protocol_write_args_to_buffer")))
      extern void wasm_minimal_protocol_write_args_to_buffer(unsigned char *ptr);

      __attribute__((import_module("typst_env"), import_name("wasm_minimal_protocol_send_result_to_host")))
      extern void wasm_minimal_protocol_send_result_to_host(unsigned char *ptr, int len);

      static signed char typst_read_i8(const unsigned char *ptr) {
        return (signed char)ptr[0];
      }

      static unsigned char typst_read_u8(const unsigned char *ptr) {
        return ptr[0];
      }

      static short typst_read_i16_le(const unsigned char *ptr) {
        return (short)(((unsigned short)ptr[0]) |
                       ((unsigned short)ptr[1] << 8));
      }

      static unsigned short typst_read_u16_le(const unsigned char *ptr) {
        return ((unsigned short)ptr[0]) |
               ((unsigned short)ptr[1] << 8);
      }

      static int typst_read_i32_le(const unsigned char *ptr) {
        return ((int)ptr[0]) |
               ((int)ptr[1] << 8) |
               ((int)ptr[2] << 16) |
               ((int)ptr[3] << 24);
      }

      static unsigned int typst_read_u32_le(const unsigned char *ptr) {
        return ((unsigned int)ptr[0]) |
               ((unsigned int)ptr[1] << 8) |
               ((unsigned int)ptr[2] << 16) |
               ((unsigned int)ptr[3] << 24);
      }

      static long long typst_read_i64_le(const unsigned char *ptr) {
        return ((long long)ptr[0]) |
               ((long long)ptr[1] << 8) |
               ((long long)ptr[2] << 16) |
               ((long long)ptr[3] << 24) |
               ((long long)ptr[4] << 32) |
               ((long long)ptr[5] << 40) |
               ((long long)ptr[6] << 48) |
               ((long long)ptr[7] << 56);
      }

      static unsigned long long typst_read_u64_le(const unsigned char *ptr) {
        return ((unsigned long long)ptr[0]) |
               ((unsigned long long)ptr[1] << 8) |
               ((unsigned long long)ptr[2] << 16) |
               ((unsigned long long)ptr[3] << 24) |
               ((unsigned long long)ptr[4] << 32) |
               ((unsigned long long)ptr[5] << 40) |
               ((unsigned long long)ptr[6] << 48) |
               ((unsigned long long)ptr[7] << 56);
      }

      static float typst_read_f32_le(const unsigned char *ptr) {
        union {
          unsigned int bits;
          float value;
        } data;
        data.bits = typst_read_u32_le(ptr);
        return data.value;
      }

      static double typst_read_f64_le(const unsigned char *ptr) {
        union {
          unsigned long long bits;
          double value;
        } data;
        data.bits = typst_read_u64_le(ptr);
        return data.value;
      }

      static void typst_write_i8(unsigned char *ptr, signed char value) {
        ptr[0] = (unsigned char)value;
      }

      static void typst_write_u8(unsigned char *ptr, unsigned char value) {
        ptr[0] = value;
      }

      static void typst_write_i16_le(unsigned char *ptr, short value) {
        ptr[0] = (unsigned char)(value & 0xff);
        ptr[1] = (unsigned char)((value >> 8) & 0xff);
      }

      static void typst_write_u16_le(unsigned char *ptr, unsigned short value) {
        ptr[0] = (unsigned char)(value & 0xff);
        ptr[1] = (unsigned char)((value >> 8) & 0xff);
      }

      static void typst_write_i32_le(unsigned char *ptr, int value) {
        ptr[0] = (unsigned char)(value & 0xff);
        ptr[1] = (unsigned char)((value >> 8) & 0xff);
        ptr[2] = (unsigned char)((value >> 16) & 0xff);
        ptr[3] = (unsigned char)((value >> 24) & 0xff);
      }

      static void typst_write_u32_le(unsigned char *ptr, unsigned int value) {
        ptr[0] = (unsigned char)(value & 0xffu);
        ptr[1] = (unsigned char)((value >> 8) & 0xffu);
        ptr[2] = (unsigned char)((value >> 16) & 0xffu);
        ptr[3] = (unsigned char)((value >> 24) & 0xffu);
      }

      static void typst_write_i64_le(unsigned char *ptr, long long value) {
        ptr[0] = (unsigned char)(value & 0xff);
        ptr[1] = (unsigned char)((value >> 8) & 0xff);
        ptr[2] = (unsigned char)((value >> 16) & 0xff);
        ptr[3] = (unsigned char)((value >> 24) & 0xff);
        ptr[4] = (unsigned char)((value >> 32) & 0xff);
        ptr[5] = (unsigned char)((value >> 40) & 0xff);
        ptr[6] = (unsigned char)((value >> 48) & 0xff);
        ptr[7] = (unsigned char)((value >> 56) & 0xff);
      }

      static void typst_write_u64_le(unsigned char *ptr, unsigned long long value) {
        ptr[0] = (unsigned char)(value & 0xffu);
        ptr[1] = (unsigned char)((value >> 8) & 0xffu);
        ptr[2] = (unsigned char)((value >> 16) & 0xffu);
        ptr[3] = (unsigned char)((value >> 24) & 0xffu);
        ptr[4] = (unsigned char)((value >> 32) & 0xffu);
        ptr[5] = (unsigned char)((value >> 40) & 0xffu);
        ptr[6] = (unsigned char)((value >> 48) & 0xffu);
        ptr[7] = (unsigned char)((value >> 56) & 0xffu);
      }

      static void typst_write_f32_le(unsigned char *ptr, float value) {
        union {
          unsigned int bits;
          float value;
        } data;
        data.value = value;
        typst_write_u32_le(ptr, data.bits);
      }

      static void typst_write_f64_le(unsigned char *ptr, double value) {
        union {
          unsigned long long bits;
          double value;
        } data;
        data.value = value;
        typst_write_u64_le(ptr, data.bits);
      }

      static int typst_c_string_len(const char *ptr) {
        int len = 0;
        while (ptr[len] != '\0') {
          ++len;
        }
        return len;
      }
    ```.text,
    prototypes,
    "\n\n",
    wrappers,
    "\n",
  ).join("")
}

#let compile-project(
  files,
  entry: none,
  exports: (),
  export-header-path: default-export-header-path,
  compiler-path: default-compiler-path,
) = {
  let all-files = if exports.len() == 0 {
    files
  } else {
    (
      ..files,
      file(export-header-path, emit-export-header(exports)),
    )
  }

  plugin(compile-bytes(package(all-files, entry: entry), compiler-path: compiler-path))
}
