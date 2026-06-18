fn main() {
    tauri_build::build();

    // TODO: C++ (src-cpp) 编译暂时跳过，后续修复 Visual Studio 生成器问题后再启用
    //
    // Build C++ static libraries from src-cpp
    // let cpp_dir = std::path::PathBuf::from(env!("CARGO_MANIFEST_DIR"))
    //     .parent()
    //     .unwrap()
    //     .join("src-cpp");
    //
    // let mut config = cmake::Config::new(cpp_dir);
    //
    // // Map Rust profile to CMake build type
    // let profile = std::env::var("PROFILE").unwrap();
    // let cmake_profile = if profile == "release" { "Release" } else { "Debug" };
    // config.profile(cmake_profile);
    //
    // // Pass vcpkg toolchain if VCPKG_ROOT is set
    // if let Ok(vcpkg_root) = std::env::var("VCPKG_ROOT") {
    //     let toolchain = std::path::PathBuf::from(vcpkg_root)
    //         .join("scripts\\buildsystems\\vcpkg.cmake");
    //     if toolchain.exists() {
    //         config.define("CMAKE_TOOLCHAIN_FILE", toolchain.to_str().unwrap());
    //     }
    // }
    //
    // // Disable all tests for cargo builds
    // config.define("BUILD_TESTS", "OFF");
    // config.define("BUILD_MEDIA_TESTS", "OFF");
    // config.define("BUILD_PULLER_TESTS", "OFF");
    // config.define("BUILD_DECODER_TESTS", "OFF");
    //
    // let dst = config.build();
    //
    // // Point cargo to the library output directory
    // let lib_dir = dst.join("build").join("lib");
    // println!("cargo:rustc-link-search=native={}", lib_dir.display());
    //
    // // Link each C++ static library
    // println!("cargo:rustc-link-lib=static=common_lib");
    // println!("cargo:rustc-link-lib=static=media_defines_lib");
    // println!("cargo:rustc-link-lib=static=media_puller_lib");
    // println!("cargo:rustc-link-lib=static=media_decoder_lib");
    //
    // // C++ runtime
    // #[cfg(target_os = "linux")]
    // println!("cargo:rustc-link-lib=dylib=stdc++");
    // #[cfg(target_os = "macos")]
    // println!("cargo:rustc-link-lib=dylib=c++");
    //
    // // Rerun if any C++ source changes
    // println!("cargo:rerun-if-changed=../src-cpp");
}