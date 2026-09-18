# SparkJS

SparkJS is a small JavaScript-inspired bytecode engine written in C++17. It has
a lexer, Pratt-style expression compiler, stack virtual machine, call frames,
and a tiny command-line REPL. It is deliberately compact enough to study and
extend.

## Supported language

- `let`, `const`, and `var` declarations
- Numbers, strings, booleans, `null`, and `undefined`
- `+ - * / %`, comparisons, equality, `!`, `&&`, and `||`
- Assignment, blocks, `if`/`else`, and `while`
- Function declarations, calls, recursion, and `return`
- `print(...)` with any number of arguments
- `//` and `/* ... */` comments

`const` bindings are enforced. `let` and `var` currently share function/global
scope because block environments are not implemented yet.

This is not yet an ECMAScript-compliant engine. Objects, arrays, nested
functions/closures, full automatic-semicolon-insertion rules, exceptions, and
the browser DOM are future work.

## Build in Git Bash

```bash
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
./build/sparkjs.exe examples/fibonacci.js
```

If CMake selects Ninja automatically, omit the `-G` option. On Linux/macOS:

```bash
cmake -S . -B build
cmake --build build
./build/sparkjs examples/fibonacci.js
```

Run without a filename for the REPL. Use `--tokens` or `--bytecode` before a
filename to inspect the engine pipeline.

## Example

```javascript
function fib(n) {
  if (n < 2) return n;
  return fib(n - 1) + fib(n - 2);
}

let answer = fib(10);
print("fib(10) =", answer);
```
