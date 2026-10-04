# Changelog

All notable changes to the **seng** programming language are documented here.

Format: `[Version] — YYYY-MM-DD`

## [1.2.0] — 2026-10-04

### ✨ Major Features & Functional Primitives

#### 🔀 Pattern Matching (`match ... with`)
- **Natural Logic Branching** — Added structured `match <expr> with` statements with `case <pattern> then` branches and optional `else` fallbacks.

#### ⚡ Lambdas & Closures (`action`)
- **Anonymous Actions** — Create first-class anonymous functions using the `action [with params] then ... end` keyword syntax.
- **First-Class Functions** — Pass actions directly into variables or higher-order functions.

#### 🔄 Higher-Order Collection Routines
- **`map`** — Transform list items with `map(list, action)`.
- **`filter`** — Filter lists based on boolean predicate actions with `filter(list, action)`.
- **`reduce`** — Accumulate list elements into a single aggregate with `reduce(list, action, initial)`.

---

## [1.0.2] — 2026-05-03

### ✨ Major Features & Improvements

#### 🏗️ Object-Oriented Programming (OOPS)
- **Blueprints** — Define structured objects using the `blueprint` keyword.
- **Encapsulation** — Use the `hidden` keyword for private members.
- **Methods** — Define behaviors within blueprints.
- **Inheritance** — Blueprints can inherit from others using `from`.
- **Initialization** — Support for `init` constructor methods.

#### 🛡️ Error Handling
- **Try/Catch Blocks** — Structured exception handling using `try`, `catch`, and `throw`.
- **Automatic Unwinding** — Safe environment and stack unwinding during exceptions in both interpreter and VM.

#### 📚 Standard Library Expansion
- **math** — Advanced math functions (sqrt, sin, cos, random, etc.).
- **sys** — System interaction, including command-line arguments via `args()`.
- **json** — Parse and stringify JSON data.
- **string** — Rich set of string manipulation functions (upper, lower, replace, split, join, etc.).
- **type** — Type checking and conversion utilities.
- **io** — File system access (`read_file`, `write_file`, `file_exists`).

#### 🛠️ Developer Tooling
- **REPL** — Interactive shell accessible via `seng repl`.
- **Disassembler** — Inspect compiled bytecode with `seng disasm <file.sec>`.
- **Arguments** — Pass CLI arguments to SENG scripts.

#### 🐧 Platform Support
- **Linux** — Full official support for Linux systems.
- **Windows** — Maintained stable support.

### 🐛 Bug Fixes & Stability
- **Circular References** — Implemented cycle-aware stringification and circularity detection for lists and instances.
- **Parser Ambiguity** — Resolved `item ... of` ambiguity with property access.
- **VM/Bytecode** — Added `OP_IMPORT`, file inlining during compilation, and a Python-like `_secache` folder for bytecode caching.
- **Memory Safety** — Fixed uninitialized recursion stacks in value stringification.
- **OOP Ref-Counting** — Proper reference counting for instances and their members.

---

## [1.0.1] — 2026-05-01

### 🔧 Minor Updates
- Resolved various memory leaks in the Bytecode VM.
- Improved error messages for invalid syntax.
- Optimized list resizing logic.

---

### 🎉 Initial Release

This is the first official public release of **seng — Simple English Programming Language**.

#### Language Features
- **Variables** — `set name to "Alice"`, `set age to 25`, `set active to true`
- **Arithmetic** — `plus`, `minus`, `times` (`*`), `divided by`, `mod`
- **Comparison** — `is equal to`, `is greater than`, `is less than`, `is not equal to`, and combined forms
- **Logical** — `and`, `or`, `not`
- **Output** — `say "Hello, World!"`
- **Input** — `ask name for "Enter your name: "`
- **Conditions** — `if … then … else if … else … end`
- **Loops** — `repeat N times … end` and `while condition … end`
- **Loop control** — `stop` (break), `skip` (continue)
- **Functions** — `define greet with name … end`, `call greet with "Alice"`, `give back value`, `result of func with args`
- **Lists** — `make list fruits`, `add "Apple" to fruits`, `item 1 of fruits`, `length of fruits`
- **Imports** — `import "utils.se"`
- **Comments** — `# this is a comment`
- **Types** — numbers (int/float), strings, booleans (`true`/`false`), `nothing` (null)

#### Tooling
- **Interpreter** — `seng file.se` — run source directly
- **Compiler** — `seng compile file.se` — compile to `.sec` bytecode
- **VM Runner** — `seng run file.sec` — execute bytecode
- **Help** — `seng help`

#### Compiler & Runtime
- Written in **C99** with zero runtime dependencies (only libc + libm)
- Recursive-descent **parser** with line-number error tracking
- **Tree-walk interpreter** for direct execution
- **Stack-based VM** executing custom `.sec` bytecode format
- Reference-counting memory management for strings, lists, and functions
- Lexical scoping with transparent variable mutation across blocks

#### Windows Installer
- Inno Setup-based `.exe` installer (`seng-setup-1.0.0-windows-x64.exe`)
- License acceptance screen (full EULA)
- Selectable install directory
- Automatic system/user PATH configuration
- `.se` and `.sec` file association
- Start Menu shortcuts
- Clean uninstaller

#### Website
- Official site: **nocorps.org/seng**
- Dark-mode landing page with interactive syntax explorer
- Licence-acceptance download modal (scroll → accept → download)
- Platform availability banner (Windows ✓, Linux 🔜, macOS 🔜)

#### Known Limitations (v1.0.0)
- Windows only (Linux and macOS support planned)
- No closures — functions capture their definition scope but do not form true closures
- No string escape sequences beyond basic `\n`, `\t`
- No standard library (file I/O, networking, etc.) — planned for v1.1.0
- Error messages show line numbers but not column positions

---

## [Upcoming] — v1.1.0

> These features are planned and subject to change.

- [ ] macOS official support
- [ ] `for each item in list` loop syntax
- [ ] Column-level error messages
- [ ] VS Code syntax highlighting extension

---

*Maintained by NoCorps.org — [nocorps.org/seng](https://nocorps.org/seng)*
