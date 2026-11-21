use std::env;
use std::path::Path;
use std::process::Command;

fn ensure_highway(cpp_root: &str, out_dir: &str, is_windows: bool) -> (String, String) {
    let vendored_hwy = format!("{}/third_party/highway", cpp_root);
    let monorepo_hwy = format!("{}/build/_deps/highway-src", cpp_root);
    let hwy_src = if Path::new(&vendored_hwy).exists() {
        vendored_hwy
    } else {
        monorepo_hwy
    };

    // In monorepo dev/CI, reuse already-built highway or configure/build via ../impulse-cpp CMake
    if cpp_root == "../impulse-cpp" {
        let monorepo_hwy_build = format!("{}/build/_deps/highway-build", cpp_root);
        let monorepo_hwy_lib = if is_windows {
            format!("{}/Release/hwy.lib", monorepo_hwy_build)
        } else {
            format!("{}/libhwy.a", monorepo_hwy_build)
        };
        if !Path::new(&monorepo_hwy_lib).exists()
            && !Path::new(&format!("{}/libhwy.a", monorepo_hwy_build)).exists()
        {
            let mut cmake_cfg = Command::new("cmake");
            cmake_cfg.args([
                "-B",
                "../impulse-cpp/build",
                "-S",
                "../impulse-cpp",
                "-DCMAKE_BUILD_TYPE=Release",
                "-DHWY_ENABLE_CONTRIB=OFF",
                "-DHWY_ENABLE_TESTS=OFF",
                "-DHWY_ENABLE_EXAMPLES=OFF",
            ]);
            if is_windows {
                cmake_cfg.args(["-A", "x64", "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL"]);
            }
            let _ = cmake_cfg.status();
            let _ = Command::new("cmake")
                .args([
                    "--build",
                    "../impulse-cpp/build",
                    "--config",
                    "Release",
                    "--target",
                    "hwy",
                ])
                .status();
        }
        return (hwy_src, monorepo_hwy_build);
    }

    let hwy_build = format!("{}/hwy-build", out_dir);
    let hwy_lib_file = if is_windows {
        format!("{}/Release/hwy.lib", hwy_build)
    } else {
        format!("{}/libhwy.a", hwy_build)
    };

    if !Path::new(&hwy_lib_file).exists() && Path::new(&hwy_src).exists() {
        let mut cmake_cfg = Command::new("cmake");
        cmake_cfg.args([
            "-B",
            &hwy_build,
            "-S",
            &hwy_src,
            "-DCMAKE_BUILD_TYPE=Release",
            "-DHWY_ENABLE_CONTRIB=OFF",
            "-DHWY_ENABLE_TESTS=OFF",
            "-DHWY_ENABLE_EXAMPLES=OFF",
        ]);
        if is_windows {
            cmake_cfg.args(["-A", "x64", "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL"]);
        }
        let cfg_ok = cmake_cfg.status().map(|s| s.success()).unwrap_or(false);
        assert!(
            cfg_ok,
            "Failed to configure Highway via CMake from {}",
            hwy_src
        );

        let build_ok = Command::new("cmake")
            .args([
                "--build", &hwy_build, "--config", "Release", "--target", "hwy",
            ])
            .status()
            .map(|s| s.success())
            .unwrap_or(false);
        assert!(build_ok, "Failed to build Highway static library");
    }

    (hwy_src, hwy_build)
}

fn compile_cpp(
    src: &str,
    cpp_root: &str,
    include_dir: &str,
    hwy_inc: &str,
    out_dir: &str,
    has_omp: bool,
) -> String {
    let filename = Path::new(src).file_stem().unwrap().to_str().unwrap();
    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    let is_windows = target_os == "windows";
    let is_macos = target_os == "macos";
    let obj_file = if is_windows {
        format!("{}/{}.obj", out_dir, filename)
    } else {
        format!("{}/{}.o", out_dir, filename)
    };
    let src_dir = format!("{}/src", cpp_root);

    if is_windows {
        let compiler = env::var("CXX").unwrap_or_else(|_| "cl".to_string());
        let mut args = vec![
            "/nologo".to_string(),
            "/std:c++20".to_string(),
            "/O2".to_string(),
            "/MD".to_string(),
            "/EHsc".to_string(),
            "/DNOMINMAX".to_string(),
            "/DWIN32_LEAN_AND_MEAN".to_string(),
            "/c".to_string(),
            src.to_string(),
            format!("/I{}", include_dir),
            format!("/I{}", src_dir),
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
            panic!(
                "Failed to compile C++ source file: {} using {}",
                src, compiler
            );
        }
    } else {
        let mut args = vec![
            "-std=c++20",
            "-O3",
            "-fPIC",
            "-c",
            src,
            "-I",
            include_dir,
            "-I",
            &src_dir,
            "-o",
            &obj_file,
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

        let compiler = env::var("CXX").unwrap_or_else(|_| {
            if is_macos {
                "clang++".to_string()
            } else {
                "c++".to_string()
            }
        });

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
            panic!(
                "Failed to compile C++ source file: {} using {}",
                src, compiler
            );
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

    println!("cargo:rerun-if-env-changed=IMPULSE_SDK_DIR");
    if let Ok(sdk_dir) = env::var("IMPULSE_SDK_DIR") {
        println!("cargo:rustc-link-search=native={}/lib", sdk_dir);
        println!("cargo:rustc-link-lib=impulse_graph");
        if Path::new(&format!("{}/lib/libhwy.a", sdk_dir)).exists()
            || Path::new(&format!("{}/lib/hwy.lib", sdk_dir)).exists()
        {
            println!("cargo:rustc-link-lib=static=hwy");
        }
        return;
    }

    let cpp_root = if Path::new("cpp/src").exists() {
        "cpp"
    } else {
        "../impulse-cpp"
    };
    let cpp_include = format!("{}/include", cpp_root);
    let (hwy_inc, hwy_build) = ensure_highway(cpp_root, &out_dir, is_windows);

    // Track changes to C++ source and header files
    println!("cargo:rerun-if-changed={}/src/impulse_simd.cpp", cpp_root);
    println!("cargo:rerun-if-changed={}/src/impulse_vm.cpp", cpp_root);
    println!(
        "cargo:rerun-if-changed={}/src/impulse_vm_context.cpp",
        cpp_root
    );
    println!(
        "cargo:rerun-if-changed={}/src/impulse_vm_internal.hpp",
        cpp_root
    );
    println!("cargo:rerun-if-changed={}/src/opcodes", cpp_root);
    println!(
        "cargo:rerun-if-changed={}/src/impulse_vm_fluent.cpp",
        cpp_root
    );
    println!(
        "cargo:rerun-if-changed={}/src/impulse_vm_fluent_traversal.cpp",
        cpp_root
    );
    println!(
        "cargo:rerun-if-changed={}/src/impulse_vm_fluent_multilane.cpp",
        cpp_root
    );
    println!(
        "cargo:rerun-if-changed={}/src/impulse_statement_c_api.cpp",
        cpp_root
    );
    println!(
        "cargo:rerun-if-changed={}/src/impulse_vm_fluent_internal.hpp",
        cpp_root
    );
    println!("cargo:rerun-if-changed={}/src/impulse_graph.cpp", cpp_root);
    println!(
        "cargo:rerun-if-changed={}/src/impulse_snapshot_writer.cpp",
        cpp_root
    );
    println!(
        "cargo:rerun-if-changed={}/src/impulse_graph_internal.hpp",
        cpp_root
    );
    println!(
        "cargo:rerun-if-changed={}/src/impulse_bytecode_emitter.cpp",
        cpp_root
    );
    println!("cargo:rerun-if-changed={}/include/impulse_simd.h", cpp_root);
    println!("cargo:rerun-if-changed={}/include/impulse_vm.h", cpp_root);
    println!(
        "cargo:rerun-if-changed={}/include/impulse_graph.h",
        cpp_root
    );
    println!(
        "cargo:rerun-if-changed={}/include/impulse_index.h",
        cpp_root
    );
    println!(
        "cargo:rerun-if-changed={}/include/impulse_scm_ast.hpp",
        cpp_root
    );
    println!(
        "cargo:rerun-if-changed={}/include/impulse_compiler.hpp",
        cpp_root
    );
    println!(
        "cargo:rerun-if-changed={}/include/impulse_cypher.hpp",
        cpp_root
    );
    println!(
        "cargo:rerun-if-changed={}/include/impulse_datalog.hpp",
        cpp_root
    );

    let has_omp = if is_macos {
        Path::new("/opt/homebrew/opt/libomp/include").exists()
    } else if is_windows || is_musl {
        false
    } else {
        Path::new("/usr/include/omp.h").exists() || Path::new("/usr/lib/gcc").exists()
    };

    // Compile C++ objects
    let compile = |rel: &str| {
        let src = format!("{}/src/{}", cpp_root, rel);
        compile_cpp(&src, cpp_root, &cpp_include, &hwy_inc, &out_dir, has_omp)
    };
    let obj_simd = compile("impulse_simd.cpp");
    let obj_vm = compile("impulse_vm.cpp");
    let obj_vm_ctx = compile("impulse_vm_context.cpp");
    let obj_fluent = compile("impulse_vm_fluent.cpp");
    let obj_fluent_trav = compile("impulse_vm_fluent_traversal.cpp");
    let obj_fluent_ml = compile("impulse_vm_fluent_multilane.cpp");
    let obj_stmt_c_api = compile("impulse_statement_c_api.cpp");
    let obj_graph = compile("impulse_graph.cpp");
    let obj_writer = compile("impulse_snapshot_writer.cpp");
    let obj_index = compile("impulse_index.cpp");
    let obj_emitter = compile("impulse_bytecode_emitter.cpp");

    // Archive into a single static library
    if is_windows {
        let lib_file = format!("{}/impulse_native.lib", out_dir);
        let status_lib = Command::new("lib")
            .args([
                "/nologo",
                &format!("/OUT:{}", lib_file),
                &obj_simd,
                &obj_vm,
                &obj_vm_ctx,
                &obj_fluent,
                &obj_fluent_trav,
                &obj_fluent_ml,
                &obj_stmt_c_api,
                &obj_graph,
                &obj_writer,
                &obj_index,
                &obj_emitter,
            ])
            .status();

        assert!(
            status_lib.is_ok() && status_lib.unwrap().success(),
            "Failed to archive objects into static library using lib.exe"
        );
    } else {
        let lib_file = format!("{}/libimpulse_native.a", out_dir);
        let status_ar = Command::new("ar")
            .args([
                "rcs",
                &lib_file,
                &obj_simd,
                &obj_vm,
                &obj_vm_ctx,
                &obj_fluent,
                &obj_fluent_trav,
                &obj_fluent_ml,
                &obj_stmt_c_api,
                &obj_graph,
                &obj_writer,
                &obj_index,
                &obj_emitter,
            ])
            .status();

        assert!(
            status_ar.is_ok() && status_ar.unwrap().success(),
            "Failed to archive objects into static library"
        );
    }

    println!("cargo:rustc-link-search=native={}", out_dir);
    if Path::new(&hwy_build).exists() {
        println!("cargo:rustc-link-search=native={}", hwy_build);
    }
    let hwy_lib_rel = format!("{}/Release", hwy_build);
    if Path::new(&hwy_lib_rel).exists() {
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
