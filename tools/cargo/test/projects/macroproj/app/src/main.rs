use hello_derive::Hello;

mod generated {
    include!(concat!(env!("OUT_DIR"), "/gen.rs"));
}

#[derive(Hello)]
struct Point {
    x: i32,
    y: i32,
}

fn main() {
    let p = Point { x: 3, y: 4 };
    println!("{} ({}, {})", p.hello(), p.x, p.y);
    println!("build.rs pour {} : {:?}", generated::TARGET, generated::SQUARES);
}
