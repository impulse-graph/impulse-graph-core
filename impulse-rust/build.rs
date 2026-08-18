use std::env;
use std::path::Path;
use std::process::Command;

fn compile_cpp(src: &str, include_dir: &str, out_dir: &str, has_omp: bool) -> String {
    let filename = Path::new(src).file_stem().unwrap().to_str().unwrap();
    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    let is_macos = target_os == "macos";
    let is_windows = target_os == "windows";
    let obj_file = if is_windows {
        format!("{}/{}.obj", out_dir, filename)
    } else {
        format!("{}/{}.o", out_dir, filename)
    };

    let hwy_inc = "../impulse-cpp/build/_deps/highway-src";
    let hwy_lib_file = if is_windows {
        "../impulse-cpp/build/_deps/highway-build/Release/hwy.lib"
    } else {
        "../impulse-cpp/build/_deps/highway-build/libhwy.a"
    };

    if !Path::new(hwy_lib_file).exists() && !Path::new("../impulse-cpp/build/_deps/highway-build/libhwy.a").exists() {
        let mut cmake_cfg = Command::new("cmake");
        cmake_cfg.args(["-B", "../impulse-cpp/build", "-S", "../impulse-cpp", "-DCMAKE_BUILD_TYPE=Release", "-DHWY_ENABLE_CONTRIB=OFF", "-DHWY_ENABLE_TESTS=OFF", "-DHWY_ENABLE_EXAMPLES=OFF"]);
        if is_windows {
            cmake_cfg.args(["-A", "x64", "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL"]);
        }
        let _ = cmake_cfg.status();
        let _ = Command::new("cmake")
            .args(["--build", "../impulse-cpp/build", "--config", "Release", "--target", "hwy"])
            .status();
    }

    if is_windows {
        let compiler = env::var("CXX").unwrap_or_else(|_| "cl.exe".to_string());
        let mut args = vec![
            "/nologo".to_string(),
            "/std:c++20".to_string(),
            "/O2".to_string(),
            "/MD".to_string(),
            "/EHsc".to_string(),
            "/DNOMINMAX".to_string(),
            "/DWIN32_LEAN_AND_MEAN".to_string(),
            "/D_CRT_SECURE_NO_WARNINGS".to_string(),
            "/utf-8".to_string(),
            "/permissive-".to_string(),
            "/c".to_string(),
            src.to_string(),
            format!("/I{}", include_dir),
            format!("/I{}", "../impulse-cpp/src"),
            format!("/Fo:{}", obj_file),
        ];
        if Path::new(hwy_inc).exists() {
            args.push(format!("/I{}", hwy_inc));
        }

        let output = Command::new(&compiler)
            .args(&args)
            .output()
            .expect("Failed to execute C++ compiler");

        if !output.status.success() {
            eprintln!(
                "C++ Compiler Error for {}:\nSTDOUT:\n{}\nSTDERR:\n{}",
                src,
                String::from_utf8_lossy(&output.stdout),
                String::from_utf8_lossy(&output.stderr)
            );
            panic!("Failed to compile C++ source file: {} using {}", src, compiler);
        }
    } else {
        let mut args = vec![
            "-std=c++20",
            "-O3",
            "-fPIC",
            "-c", src,
            "-I", include_dir,
            "-I", "../impulse-cpp/src",
            "-o", &obj_file
        ];

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
        
        let output = Command::new(&compiler)
            .args(&args)
            .output()
            .expect("Failed to execute C++ compiler");

        if !output.status.success() {
            eprintln!(
                "C++ Compiler Error for {}:\nSTDOUT:\n{}\nSTDERR:\n{}",
                src,
                String::from_utf8_lossy(&output.stdout),
                String::from_utf8_lossy(&output.stderr)
            );
            panic!("Failed to compile C++ source file: {} using {}", src, compiler);
        }
    }

    obj_file
}

fn main() {
    let out_dir = env::var("OUT_DIR").unwrap();
    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    let target_env = env::var("CARGO_CFG_TARGET_ENV").unwrap_or_default();
    let is_macos = target_os == "macos";
    let is_windows = target_os == "windows";
    let is_musl = target_env == "musl";
    let cpp_include = "../impulse-cpp/include";

    // Track changes to C++ source and header files
    println!("cargo:rerun-if-changed=../impulse-cpp/src/impulse_simd.cpp");
    println!("cargo:rerun-if-changed=../impulse-cpp/src/impulse_vm.cpp");
    println!("cargo:rerun-if-changed=../impulse-cpp/src/impulse_vm_fluent.cpp");
    println!("cargo:rerun-if-changed=../impulse-cpp/src/impulse_graph.cpp");
    println!("cargo:rerun-if-changed=../impulse-cpp/include/impulse_simd.h");
    println!("cargo:rerun-if-changed=../impulse-cpp/include/impulse_vm.h");
    println!("cargo:rerun-if-changed=../impulse-cpp/include/impulse_graph.h");
    println!("cargo:rerun-if-changed=../impulse-cpp/include/impulse_index.h");
    println!("cargo:rerun-if-changed=../impulse-cpp/include/impulse_compiler.hpp");
    println!("cargo:rerun-if-changed=../impulse-cpp/include/impulse_cypher.hpp");
    println!("cargo:rerun-if-changed=../impulse-cpp/include/impulse_datalog.hpp");

    let has_omp = if is_macos {
        Path::new("/opt/homebrew/opt/libomp/include").exists()
    } else if is_windows || is_musl {
        false
    } else {
        Path::new("/usr/include/omp.h").exists() || Path::new("/usr/lib/gcc").exists()
    };

    // Compile C++ objects
    let obj_simd = compile_cpp("../impulse-cpp/src/impulse_simd.cpp", cpp_include, &out_dir, has_omp);
    let obj_vm = compile_cpp("../impulse-cpp/src/impulse_vm.cpp", cpp_include, &out_dir, has_omp);
    let obj_fluent = compile_cpp("../impulse-cpp/src/impulse_vm_fluent.cpp", cpp_include, &out_dir, has_omp);
    let obj_graph = compile_cpp("../impulse-cpp/src/impulse_graph.cpp", cpp_include, &out_dir, has_omp);
    let obj_index = compile_cpp("../impulse-cpp/src/impulse_index.cpp", cpp_include, &out_dir, has_omp);

    // Archive into a single static library
    if is_windows {
        let lib_file = format!("{}/impulse_native.lib", out_dir);
        let status_lib = Command::new("lib")
            .args(["/nologo", &format!("/OUT:{}", lib_file), &obj_simd, &obj_vm, &obj_fluent, &obj_graph, &obj_index])
            .status();
            
        assert!(
            status_lib.is_ok() && status_lib.unwrap().success(),
            "Failed to archive objects into static library using lib.exe"
        );
    } else {
        let lib_file = format!("{}/libimpulse_native.a", out_dir);
        let status_ar = Command::new("ar")
            .args(["rcs", &lib_file, &obj_simd, &obj_vm, &obj_fluent, &obj_graph, &obj_index])
            .status();
            
        assert!(
            status_ar.is_ok() && status_ar.unwrap().success(),
            "Failed to archive objects into static library"
        );
    }

    println!("cargo:rustc-link-search=native={}", out_dir);
    let hwy_lib = "../impulse-cpp/build/_deps/highway-build";
    if Path::new(hwy_lib).exists() {
        println!("cargo:rustc-link-search=native={}", hwy_lib);
    }
    let hwy_lib_rel = "../impulse-cpp/build/_deps/highway-build/Release";
    if Path::new(hwy_lib_rel).exists() {
        println!("cargo:rustc-link-search=native={}", hwy_lib_rel);
    }

    if is_windows {
        println!("cargo:rustc-link-lib=static=impulse_native");
        println!("cargo:rustc-link-lib=static=hwy");
    } else if is_macos {
        println!("cargo:rustc-link-search=native=/opt/homebrew/lib");
        println!("cargo:rustc-link-lib=static=impulse_native");
        println!("cargo:rustc-link-lib=static=hwy");
        println!("cargo:rustc-link-lib=dylib=c++");
        if has_omp {
            println!("cargo:rustc-link-search=native=/opt/homebrew/opt/libomp/lib");
            println!("cargo:rustc-link-lib=dylib=omp");
        }
    } else if is_musl {
        println!("cargo:rustc-link-lib=static=impulse_native");
        println!("cargo:rustc-link-lib=static=hwy");
        if let Ok(entries) = std::fs::read_dir("/usr/lib/gcc/x86_64-linux-gnu") {
            for entry in entries.flatten() {
                if entry.path().join("libstdc++.a").exists() {
                    println!("cargo:rustc-link-search=native={}", entry.path().display());
                    break;
                }
            }
        }
        println!("cargo:rustc-link-lib=static=stdc++");
    } else {
        println!("cargo:rustc-link-lib=static=impulse_native");
        println!("cargo:rustc-link-lib=static=hwy");
        println!("cargo:rustc-link-lib=dylib=stdc++");
        if has_omp {
            println!("cargo:rustc-link-lib=dylib=gomp");
        }
    }
}
