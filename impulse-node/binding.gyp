{
  "targets": [
    {
      "target_name": "impulse_node_native",
      "sources": [
        "src/impulse_node.cpp",
        "../impulse-cpp/src/impulse_graph.cpp",
        "../impulse-cpp/src/impulse_vm.cpp",
        "../impulse-cpp/src/impulse_vm_fluent.cpp",
        "../impulse-cpp/src/impulse_simd.cpp"
      ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "../impulse-cpp/include",
        "../impulse-cpp/src",
        "../impulse-cpp/build/_deps/highway-src"
      ],
      "cflags!": [ "-fno-exceptions" ],
      "cflags_cc!": [ "-fno-exceptions" ],
      "cflags_cc": [ "-std=c++20" ],
      "defines": [ "NAPI_DISABLE_CPP_EXCEPTIONS" ],
      "xcode_settings": {
        "CLANG_CXX_LANGUAGE_STANDARD": "c++20",
        "GCC_ENABLE_CPP_EXCEPTIONS": "YES"
      }
    }
  ]
}
