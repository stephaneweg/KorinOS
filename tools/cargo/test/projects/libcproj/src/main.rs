use rand::Rng;
use std::io::{Read, Seek, SeekFrom, Write};

fn main() {
    let mut rng = rand::rng();
    let n: u32 = rng.random_range(1..=100);
    let mut f = tempfile::tempfile().expect("tempfile");
    write!(f, "tirage {n}").unwrap();
    f.seek(SeekFrom::Start(0)).unwrap();
    let mut s = String::new();
    f.read_to_string(&mut s).unwrap();
    println!("rand et tempfile dans Korin : {s} (fichier temporaire relu)");
}
