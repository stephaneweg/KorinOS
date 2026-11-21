// build.rs : compilé et lancé par cargo dans Korin ; écrit un fichier dans OUT_DIR, que main.rs inclut
use std::{env, fs, path::Path};

fn main() {
    let out = env::var("OUT_DIR").unwrap();
    let target = env::var("TARGET").unwrap();
    let squares: Vec<String> = (1..=10).map(|i| (i * i).to_string()).collect();
    fs::write(
        Path::new(&out).join("gen.rs"),
        format!("pub const TARGET: &str = \"{target}\";\npub const SQUARES: [u32; 10] = [{}];\n", squares.join(", ")),
    )
    .unwrap();
    println!("cargo:rerun-if-changed=build.rs");
}
