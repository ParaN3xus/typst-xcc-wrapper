#import "wrapper.typ": code-file, emit-export-header, export, file

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

#let compile-result(source, compiler-path: "../build/typst_xcc_compiler.wasm") = {
  let compiler = plugin(compiler-path)
  cbor(compiler.compile(source))
}

#let compile-project(
  files,
  entry: none,
  exports: (),
  export-header-path: "typst/export.typst_plugin.h",
  compiler-path: "../build/typst_xcc_compiler.wasm",
) = {
  let all-files = if exports.len() == 0 {
    files
  } else {
    (
      ..files,
      file(export-header-path, emit-export-header(exports)),
    )
  }

  compile-result(package(all-files, entry: entry), compiler-path: compiler-path)
}
