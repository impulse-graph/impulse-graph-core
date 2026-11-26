{
  "targets": [
    {
      "target_name": "impulse_node_native",
      "sources": [
        "src/impulse_node.cpp",
        "../impulse-cpp/src/impulse_graph.cpp",
        "../impulse-cpp/src/impulse_snapshot_writer.cpp",
        "../impulse-cpp/src/impulse_index.cpp",
        "../impulse-cpp/src/impulse_test_control.cpp",
        "../impulse-cpp/src/impulse_statement_c_api.cpp",
        "../impulse-cpp/src/impulse_simd.cpp",
        "../impulse-cpp/src/vm/vm_bytecode_emitter.cpp",
        "../impulse-cpp/src/vm/vm_impscm_compiler.cpp",
        "../impulse-cpp/src/vm/vm_ictx_v2.cpp",
        "../impulse-cpp/src/vm/vm_validator.cpp",
        "../impulse-cpp/src/vm/vm_memory_planner.cpp",
        "../impulse-cpp/src/vm/vm_dispatch.cpp",
        "../impulse-cpp/src/vm/vm_simd_hwy.cpp"
      ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "../impulse-cpp/include",
        "../impulse-cpp/src",
        "../impulse-cpp/build/_deps/highway-src"
      ],
      "cflags!": [ "-fno-exceptions", "-fno-rtti" ],
      "cflags_cc!": [ "-fno-exceptions", "-fno-rtti" ],
      "cflags_cc": [ "-std=c++20", "-frtti" ],
      "defines": [ "NAPI_DISABLE_CPP_EXCEPTIONS" ],
      "xcode_settings": {
        "CLANG_CXX_LANGUAGE_STANDARD": "c++20",
        "GCC_ENABLE_CPP_EXCEPTIONS": "YES",
        "GCC_ENABLE_CPP_RTTI": "YES"
      }
    }
  ]
}
