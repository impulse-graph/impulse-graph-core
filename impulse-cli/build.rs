use std::env;
use std::fs;
use std::path::PathBuf;

fn main() {
    #[cfg(target_env = "msvc")]
    {
        println!("cargo:rustc-link-arg-bins=/STACK:8388608");
    }

    // Determine target directory where executable lives
    if let Ok(out_dir) = env::var("OUT_DIR") {
        let out_path = PathBuf::from(&out_dir);
        // Navigate up to target/debug or target/release directory
        if let Some(target_dir) = out_path.ancestors().nth(3) {
            #[cfg(unix)]
            {
                let symlink_bin = target_dir.join("impulse");
                if symlink_bin.exists() || symlink_bin.is_symlink() {
                    let _ = fs::remove_file(&symlink_bin);
                }
                let _ = std::os::unix::fs::symlink("impulse-graph", &symlink_bin);
            }
            #[cfg(windows)]
            {
                let target_exe = target_dir.join("impulse-graph.exe");
                let symlink_exe = target_dir.join("impulse.exe");
                if symlink_exe.exists() {
                    let _ = fs::remove_file(&symlink_exe);
                }
                let _ = fs::hard_link(&target_exe, &symlink_exe);
            }
        }
    }
}
