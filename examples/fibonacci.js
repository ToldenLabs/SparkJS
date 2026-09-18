// Recursion exercises the compiler, call frames, locals, and return values.
function fib(n) {
  if (n < 2) {
    return n;
  }
  return fib(n - 1) + fib(n - 2);
}

let answer = fib(10);
print("fib(10) =", answer);

let countdown = 3;
while (countdown > 0) {
  print("launch in", countdown);
  countdown = countdown - 1;
}
print("liftoff!");

