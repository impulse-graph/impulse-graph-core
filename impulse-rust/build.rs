use std::process::Command;
use std::env;

fn main() {
    let out_dir = env::var("OUT_DIR").unwrap();
    let cpp_src = "../impulse-cpp/src/impulse_simd.cpp";
    let cpp_include = "../impulse-cpp/include";
    let obj_file = format!("{}/impulse_simd.o", out_dir);
    let lib_file = format!("{}/libimpulse_simd_native.a", out_dir);

    // Track changes to C++ source and header files
    println!("cargo:rerun-if-changed={}", cpp_src);
    println!("cargo:rerun-if-changed=../impulse-cpp/include/impulse_simd.h");

    // Compile impulse_simd.cpp to object file
    let status_compile = Command::new("clang++")
        .args(&[
            "-std=c++20",
            "-O3",
            "-c", cpp_src,
            "-I", cpp_include,
            "-o", &obj_file
        ])
        .status();

    if let Ok(status) = status_compile {
        if status.success() {
            // Archive object file into static library
            let status_ar = Command::new("ar")
                .args(&["rcs", &lib_file, &obj_file])
                .status();

            if let Ok(ar_stat) = status_ar {
                if ar_stat.success() {
                    println!("cargo:rustc-link-search=native={}", out_dir);
                    println!("cargo:rustc-link-lib=static=impulse_simd_native");
                    println!("cargo:rustc-link-lib=dylib=c++");
                    return;
                }
            }
        }
    }

    println!("cargo:warning=Failed to compile impulse_simd.cpp native library via build.rs");
}
