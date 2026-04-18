cmake_minimum_required(VERSION 3.30)

project(
  typst_xcc_wrapper
  VERSION 0.1.0
  LANGUAGES C
)

include(FetchContent)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# resolve xcc source
option(USE_LOCAL_XCC "Use a local xcc checkout instead of fetching from Git" OFF)
set(LOCAL_XCC_DIR "${CMAKE_CURRENT_LIST_DIR}/xcc" CACHE PATH "Path to a local xcc checkout")

set(XCC_GIT_REPOSITORY "https://github.com/tyfkda/xcc.git" CACHE STRING "xcc git repository")
set(XCC_GIT_TAG "661fc8be0404a50a5c3d4305a18ae965adc90af7" CACHE STRING "xcc git revision")

if(USE_LOCAL_XCC)
  get_filename_component(XCC_SOURCE_DIR "${LOCAL_XCC_DIR}" ABSOLUTE)
else()
  FetchContent_Declare(
    xcc
    GIT_REPOSITORY "${XCC_GIT_REPOSITORY}"
    GIT_TAG "${XCC_GIT_TAG}"
    GIT_SHALLOW FALSE
  )
  FetchContent_MakeAvailable(xcc)
  set(XCC_SOURCE_DIR "${xcc_SOURCE_DIR}")
endif()

message(STATUS "Using xcc source tree: ${XCC_SOURCE_DIR}")

FetchContent_Declare(
  tinycbor
  GIT_REPOSITORY "https://github.com/intel/tinycbor.git"
  GIT_TAG "v0.6.1"
  GIT_SHALLOW FALSE
)
FetchContent_MakeAvailable(tinycbor)
set(TINYCBOR_SOURCE_DIR "${tinycbor_SOURCE_DIR}")

message(STATUS "Using TinyCBOR source tree: ${TINYCBOR_SOURCE_DIR}")


set(XCC_SOURCE_DIRS
  "${XCC_SOURCE_DIR}/src/wcc"
  "${XCC_SOURCE_DIR}/src/cc/frontend"
  "${XCC_SOURCE_DIR}/src/cpp"
  "${XCC_SOURCE_DIR}/src/util"
)

# collect the xcc source files
function(collect_c_sources out_var)
  set(result)
  foreach(dir IN LISTS ARGN)
    file(GLOB dir_sources CONFIGURE_DEPENDS "${dir}/*.c")
    list(APPEND result ${dir_sources})
  endforeach()
  set(${out_var} "${result}" PARENT_SCOPE)
endfunction()

collect_c_sources(
  XCC_EMBED_SOURCES
  ${XCC_SOURCE_DIRS}
)

list(REMOVE_ITEM XCC_EMBED_SOURCES
  "${XCC_SOURCE_DIR}/src/wcc/wcc.c"
  "${XCC_SOURCE_DIR}/src/cpp/preprocessor.c"
  "${XCC_SOURCE_DIR}/src/cpp/cpp.c"
  "${XCC_SOURCE_DIR}/src/util/elfutil.c"
)

list(APPEND XCC_EMBED_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/src/vfs/preprocessor_vfs.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/vfs/vfs.c"
)

set(XCC_PLUGIN_SUPPORT_SOURCES
  ${XCC_EMBED_SOURCES}
  "${CMAKE_CURRENT_SOURCE_DIR}/src/wasi_stub.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/cbor/tinycbor_encoder.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/cbor/tinycbor_parser.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/cbor/tinycbor_dup_string.c"
)

list(REMOVE_ITEM XCC_PLUGIN_SUPPORT_SOURCES "${XCC_SOURCE_DIR}/src/wcc/wasm_linker.c")

set(TYPST_XCC_COMPILER "${CMAKE_CURRENT_BINARY_DIR}/typst_xcc_compiler.wasm")
set(TYPST_XCC_WLIBC "${XCC_SOURCE_DIR}/lib/wlibc.a")

set(TYPST_XCC_PLUGIN_INCLUDE_DIRS
  "${XCC_SOURCE_DIR}/src/util"
  "${XCC_SOURCE_DIR}/src/cpp"
  "${XCC_SOURCE_DIR}/src/cc/frontend"
  "${XCC_SOURCE_DIR}/src/wcc"
  "${XCC_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/vfs"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/cbor"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/cbor/shims"
  "${TINYCBOR_SOURCE_DIR}/src"
)
set(TYPST_XCC_PLUGIN_COMPILE_DEFINITIONS
  _GNU_SOURCE
  XCC_TARGET_ARCH=XCC_ARCH_WASM
)
set(TYPST_XCC_PLUGIN_COMPILE_OPTIONS
  -Wall
  -Wextra
  -Wno-missing-field-initializers
  -Wno-empty-body
  -Wno-unused-function
)
set(TYPST_XCC_PLUGIN_INCLUDE_FLAGS)
foreach(dir IN LISTS TYPST_XCC_PLUGIN_INCLUDE_DIRS)
  list(APPEND TYPST_XCC_PLUGIN_INCLUDE_FLAGS "-I${dir}")
endforeach()
set(TYPST_XCC_PLUGIN_OUTPUT_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/src/typst_xcc_compiler.c"
  ${XCC_PLUGIN_SUPPORT_SOURCES}
)
set(TYPST_XCC_PLUGIN_WRAPPER_ARGS
  ${TYPST_XCC_PLUGIN_INCLUDE_FLAGS}
  -o "${TYPST_XCC_COMPILER}"
  --entry-point=
  -e compile
  -Wl,--allow-undefined
  -nostdlib
  ${TYPST_XCC_PLUGIN_OUTPUT_SOURCES}
  "${TYPST_XCC_WLIBC}"
)

add_library(typst_xcc_plugin_context INTERFACE)
target_include_directories(
  typst_xcc_plugin_context
  INTERFACE
    ${TYPST_XCC_PLUGIN_INCLUDE_DIRS}
)
target_compile_definitions(
  typst_xcc_plugin_context
  INTERFACE
    ${TYPST_XCC_PLUGIN_COMPILE_DEFINITIONS}
)
target_compile_options(
  typst_xcc_plugin_context
  INTERFACE
    ${TYPST_XCC_PLUGIN_COMPILE_OPTIONS}
)

# xcc as a library
add_library(xcc_embed STATIC ${XCC_EMBED_SOURCES})
target_include_directories(
  xcc_embed
  PUBLIC
    ${XCC_SOURCE_DIRS}
)
target_compile_definitions(
  xcc_embed
  PRIVATE
    ${TYPST_XCC_PLUGIN_COMPILE_DEFINITIONS}
)
target_compile_options(
  xcc_embed
  PRIVATE
    ${TYPST_XCC_PLUGIN_COMPILE_OPTIONS}
)

add_executable(compiler_wrapper
  src/compiler_wrapper.c
)
target_include_directories(
  compiler_wrapper
  PRIVATE
    ${XCC_SOURCE_DIRS}
)
target_link_libraries(compiler_wrapper PRIVATE xcc_embed)
target_compile_options(
  compiler_wrapper
  PRIVATE
    -Wall
    -Wextra
)
target_compile_definitions(
  compiler_wrapper
  PRIVATE
    ${TYPST_XCC_PLUGIN_COMPILE_DEFINITIONS}
)

add_custom_target(show-xcc-source
  COMMAND ${CMAKE_COMMAND} -E echo "xcc source: ${XCC_SOURCE_DIR}"
  VERBATIM
)

# Mirror the custom wrapper compile context so clangd can discover headers for
# src/typst_xcc_compiler.c via compile_commands.json.
add_library(typst_xcc_compiler_ide OBJECT EXCLUDE_FROM_ALL
  src/typst_xcc_compiler.c
)
target_link_libraries(typst_xcc_compiler_ide PRIVATE typst_xcc_plugin_context)

# produce the final typst plugin
# 1. build xcc's wasm libc with the embedded wrapper as the compiler driver
# 2. compile src/typst_xcc_compiler.c into the wasm plugin artifact
add_custom_command(
  OUTPUT "${TYPST_XCC_WLIBC}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${XCC_SOURCE_DIR}/lib"
  COMMAND ${CMAKE_COMMAND} -E env
          TYPST_XCC_ROOT=${XCC_SOURCE_DIR}
          make -C "${XCC_SOURCE_DIR}/libsrc" CC=$<TARGET_FILE:compiler_wrapper> AR=llvm-ar wcc-libs
  DEPENDS
    compiler_wrapper
  VERBATIM
)

add_custom_command(
  OUTPUT "${TYPST_XCC_COMPILER}"
  COMMAND "$<TARGET_FILE:compiler_wrapper>"
          ${TYPST_XCC_PLUGIN_WRAPPER_ARGS}
  DEPENDS
    "${TYPST_XCC_WLIBC}"
    ${TYPST_XCC_PLUGIN_OUTPUT_SOURCES}
  COMMAND_EXPAND_LISTS
  VERBATIM
)

add_custom_target(typst_xcc_compiler ALL DEPENDS "${TYPST_XCC_COMPILER}")
