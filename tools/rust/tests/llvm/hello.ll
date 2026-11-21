; hello.ll : un programme en IR LLVM, compilé par llc dans Korin (docs/RUST.md, « LLVM pour Korin »)
@msg = private constant [28 x i8] c"hello from llc on Korin PC\0A\00"

declare i32 @printf(ptr, ...)

define i32 @fib(i32 %n) {
entry:
  %small = icmp slt i32 %n, 2
  br i1 %small, label %done, label %rec
rec:
  %a = sub i32 %n, 1
  %b = sub i32 %n, 2
  %fa = call i32 @fib(i32 %a)
  %fb = call i32 @fib(i32 %b)
  %s = add i32 %fa, %fb
  ret i32 %s
done:
  ret i32 %n
}

@fmt = private constant [12 x i8] c"fib(20) %d\0A\00"

define i32 @main() {
  call i32 (ptr, ...) @printf(ptr @msg)
  %f = call i32 @fib(i32 20)
  call i32 (ptr, ...) @printf(ptr @fmt, i32 %f)
  ret i32 0
}
