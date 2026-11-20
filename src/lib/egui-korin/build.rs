// build.rs : le relais C (csrc/host.c) compilé par le gcc de Korin (cc-rs le trouve par l'éditeur de liens de la
// cible, RUSTC_LINKER), lié à libuikit.a (les relais de UIKit, tools/pc/apps.sh).
//
// KORIN_KITS : le dossier qui a include/kits/uikit_abi.h et lib/libuikit.a (défaut : build/pc/gcc du dépôt).
use std::env;
use std::path::PathBuf;

fn main() {
    println!("cargo:rerun-if-changed=csrc/host.c");
    println!("cargo:rerun-if-env-changed=KORIN_KITS");
    if env::var("CARGO_CFG_TARGET_OS").as_deref() != Ok("korin") {
        return; // (cargo check sur l'hôte : rien à lier)
    }
    let man = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let root = man.join("../../..");
    let kits = env::var("KORIN_KITS").map(PathBuf::from).unwrap_or_else(|_| root.join("build/pc/gcc"));
    let abi = kits.join("include/kits");
    if !abi.join("uikit_abi.h").is_file() {
        panic!("egui-korin : {} absent (wsl sh tools/pc/apps.sh, ou KORIN_KITS)", abi.join("uikit_abi.h").display());
    }
    cc::Build::new()
        .file("csrc/host.c")
        .include(&abi)
        .include(root.join("src/kits/uikit/include"))
        .flag("-O2")
        .warnings(false)
        .compile("egui_korin_host");
    println!("cargo:rustc-link-search=native={}", kits.join("lib").display());
    println!("cargo:rustc-link-lib=static=uikit");
}
