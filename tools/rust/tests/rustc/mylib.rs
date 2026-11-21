//! mylib.rs : une bibliothèque (rlib) compilée par rustc dans Korin, avec génériques, traits et macros
pub trait Shape {
    fn area(&self) -> f64;
}
pub struct Circle(pub f64);
pub struct Rect(pub f64, pub f64);
impl Shape for Circle {
    fn area(&self) -> f64 { std::f64::consts::PI * self.0 * self.0 }
}
impl Shape for Rect {
    fn area(&self) -> f64 { self.0 * self.1 }
}
pub fn total<S: Shape + ?Sized>(shapes: &[Box<S>]) -> f64 {
    shapes.iter().map(|s| s.area()).sum()
}
#[macro_export]
macro_rules! shapes {
    ($($e:expr),*) => { vec![$(Box::new($e) as Box<dyn $crate::Shape>),*] };
}
