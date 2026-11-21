// La liaison avec UIKit : les relais des fonctions du kit (libuikit.a de GCC). uikit_kit_open, dedans, ouvre le kit
// au démarrage ; libposix (liée d'office par gcc) fournit le reste.
fn main() {
    let lib = std::env::var("KORIN_GCC_LIB").unwrap_or_else(|_| "/devtools/gcc/lib".to_owned());
    println!("cargo:rustc-link-search=native={lib}");
    println!("cargo:rustc-link-lib=static=uikit");
    println!("cargo:rerun-if-env-changed=KORIN_GCC_LIB");
}
