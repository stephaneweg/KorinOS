//! libproj : une bibliothèque avec dépendances, construite par cargo et rustc dans Korin (docs/CARGO.md)
use either::Either;
use heck::ToSnakeCase;

cfg_if::cfg_if! {
    if #[cfg(target_os = "korin")] {
        pub const OS: &str = "korin";
    } else {
        pub const OS: &str = "autre";
    }
}

pub fn describe(n: i64, name: &str) -> String {
    let mut buf = itoa::Buffer::new();
    let v: Either<&str, &str> = if n % 2 == 0 { Either::Left("pair") } else { Either::Right("impair") };
    format!("{} {} ({}) sur {}", name.to_snake_case(), buf.format(n), v, OS)
}
