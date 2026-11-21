// hello.rs : compilé par rustc dans Korin (docs/RUST.md, « rustc dans Korin »)
use std::collections::HashMap;

fn fib(n: u32) -> u64 {
    if n < 2 { n as u64 } else { fib(n - 1) + fib(n - 2) }
}

fn main() {
    let mut m = HashMap::new();
    for w in "le rustc de Korin compile ce programme dans Korin".split(' ') {
        *m.entry(w.len()).or_insert(0) += 1;
    }
    let mut v: Vec<_> = m.into_iter().collect();
    v.sort();
    println!("hello from rustc on Korin: fib(30) = {}, longueurs {:?}", fib(30), v);
}
