use std::process::Command;
use std::env;
use std::path::Path;

fn compile_cpp(src: &str, include_dir: &str, out_dir: &str, has_omp: bool) -> String {
    let filename = Path::new(src).file_stem().unwrap().to_str().unwrap();
    let obj_file = format!("{}/{}.o", out_dir, filename);
    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    let is_macos = target_os == "macos";

    let mut args = vec![
        "-std=c++20",
        "-O3",
        "-fPIC",
        "-c", src,
        "-I", include_dir,
        "-I", "../impulse-cpp/src",
        "-o", &obj_file
    ];

    let hwy_inc = "../impulse-cpp/build/_deps/highway-src";
    if Path::new(hwy_inc).exists() {
        args.push("-I");
        args.push(hwy_inc);
    }

    if is_macos {
        args.push("-I/opt/homebrew/include");
        if has_omp {
            args.push("-Xpreprocessor");
            args.push("-fopenmp");
            args.push("-I/opt/homebrew/opt/libomp/include");
        }
    } else if has_omp {
        args.push("-fopenmp");
    }

    let compiler = env::var("CXX").unwrap_or_else(|_| if is_macos { "clang++".to_string() } else { "c++".to_string() });
    
    let status = Command::new(&compiler)
        .args(&args)
        .status();
        
    assert!(
        status.is_ok() && status.unwrap().success(),
        "Failed to compile C++ source file: {} using {}", src, compiler
    );
    obj_file
}

fn main() {
    let out_dir = env::var("OUT_DIR").unwrap();
    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    let is_macos = target_os == "macos";
    let cpp_include = "../impulse-cpp/include";

    // Track changes to C++ source and header files
    println!("cargo:rerun-if-changed=../impulse-cpp/src/impulse_simd.cpp");
    println!("cargo:rerun-if-changed=../impulse-cpp/src/impulse_vm.cpp");
    println!("cargo:rerun-if-changed=../impulse-cpp/src/impulse_graph.cpp");
    println!("cargo:rerun-if-changed=../impulse-cpp/include/impulse_simd.h");
    println!("cargo:rerun-if-changed=../impulse-cpp/include/impulse_vm.h");
    println!("cargo:rerun-if-changed=../impulse-cpp/include/impulse_graph.h");

    let has_omp = if is_macos {
        Path::new("/opt/homebrew/opt/libomp/include").exists()
    } else {
        // On Linux, check standard location or compile flag
        Path::new("/usr/include/omp.h").exists() || Path::new("/usr/lib/gcc").exists()
    };

    // Compile C++ objects
    let obj_simd = compile_cpp("../impulse-cpp/src/impulse_simd.cpp", cpp_include, &out_dir, has_omp);
    let obj_vm = compile_cpp("../impulse-cpp/src/impulse_vm.cpp", cpp_include, &out_dir, has_omp);
    let obj_graph = compile_cpp("../impulse-cpp/src/impulse_graph.cpp", cpp_include, &out_dir, has_omp);

    // Archive into a single static library
    let lib_file = format!("{}/libimpulse_native.a", out_dir);
    let status_ar = Command::new("ar")
        .args(["rcs", &lib_file, &obj_simd, &obj_vm, &obj_graph])
        .status();
        
    assert!(
        status_ar.is_ok() && status_ar.unwrap().success(),
        "Failed to archive objects into static library"
    );

    println!("cargo:rustc-link-search=native={}", out_dir);
    let hwy_lib = "../impulse-cpp/build/_deps/highway-build";
    if Path::new(hwy_lib).exists() {
        println!("cargo:rustc-link-search=native={}", hwy_lib);
    }
    if is_macos {
        println!("cargo:rustc-link-search=native=/opt/homebrew/lib");
        println!("cargo:rustc-link-lib=static=impulse_native");
        println!("cargo:rustc-link-lib=static=hwy");
        println!("cargo:rustc-link-lib=dylib=c++");
        if has_omp {
            println!("cargo:rustc-link-search=native=/opt/homebrew/opt/libomp/lib");
            println!("cargo:rustc-link-lib=dylib=omp");
        }
    } else {
        println!("cargo:rustc-link-lib=static=impulse_native");
        println!("cargo:rustc-link-lib=static=hwy");
        println!("cargo:rustc-link-lib=dylib=stdc++");
        if has_omp {
            println!("cargo:rustc-link-lib=dylib=gomp");
        }
    }
}
