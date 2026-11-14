# C++ guidelines

How the C++ in `compiler/` is written. The owner asked for this on 2026-11-14, after the files had grown
too large to work in. A check in the test suite (`compiler/tools/check_style.py`) enforces the rules that
a script can judge; a reviewer holds a change to the rest.

The language is C++17, built with `-fno-exceptions -fno-rtti -Wall -Wextra -Werror`.

## The principles behind the rules

The rules below are SOLID and DRY, applied to this code base. Where a rule does not settle a case, the
principle does.

| Principle | What it means here | Where |
|---|---|---|
| **S**ingle responsibility | A file, a class and a function each have one job, and one reason to change. | Sections 1 and 2 |
| **O**pen for extension, closed for change | A new Godot feature or a new backend is added beside what is there, not by editing every layer. The interpreter knows the engine only as `vm::Host`; a new IR operation is one line in one table plus its case. | Section 2 |
| **L**iskov substitution | Anything that implements `vm::Host` can stand in for any other: Godot, the stand-alone host, the recorder the tests use. A host never needs to know which interpreter called it. | Section 2 |
| **I**nterface segregation | A function asks for the narrowest thing it needs. An interface that grows past what one caller uses is split: a host that only prints implements `Output`, not the engine's entries. | Section 2 |
| **D**ependency inversion | Layers depend on an interface the lower layer owns, never on the layer above: `vm/` and `ir/` include no Godot header. | Section 2 |
| **DRY** | Every rule, constant and message is written once. Two layers that must agree read the same source. | Section 3 |

## 1. Size

There is no standard number. Style guides that give one sit between 300 and 1,000 lines for a file and
between 40 and 100 for a function. These are ours:

| What | Aim for | The check fails above |
|---|---|---|
| A `.cpp` file | 500 lines | 800 |
| A header | 300 lines | 500 |
| A function | 60 lines | 120 |
| Nesting inside a function | 3 levels | not checked |

- A file has one job, and its name says what the job is. When a file needs a heading inside it to find
  your way, it is two files.
- The one exception to the function limit is a dispatch `switch` (the interpreter's loop, the lowerer's
  `evaluate`). Each of its cases is then a few lines that call a named function. Such functions are
  listed by name in the check.
- Generated files (`godot_data.cpp` and its relatives) are not checked.

## 2. One job each

- **A class that is split across files keeps one header.** The lowerer is one class; its methods live in
  `lower_expressions.cpp`, `lower_calls.cpp` and so on, by what they lower.
- **A function does one thing at one level.** If it needs a comment that says "now do the second part",
  the second part is a function with that name.
- **Layers do not reach around each other.** `vm/` and `ir/` include no Godot header: everything goes
  through `vm/host.h`. The front end (`src/`) knows nothing of the IR.
- **Depend on the narrow thing.** A function that needs a name takes a `std::string_view`, not the
  `Symbol` it came from.

## 3. Say it once

- A rule that two layers must agree on is written once and read by both. Example: whether an expression
  is a whole number is decided by the checker and recorded on the expression; the lowerer reads it.
- A literal that appears twice is a named constant. An error message that appears twice is a function.
- Three lines that appear three times are a function. Two similar functions that differ by a type are a
  template; by a value, a parameter.
- Before writing a helper, search for it. Helpers shared by a layer live in that layer's `*_util.h`.

## 4. Comments

- **A comment says why, or what a reader could not know from the code:** the rule being followed, the
  case that makes the odd line necessary, the cost of the other choice.
- **No banners.** `// ---- expressions ----` is a sign that the file should be split (section 1).
- **No comment that repeats the code.** `// The owner.` above `owner = ...` goes.
- **No commented-out code.** Git keeps it.
- Every declaration in a header that is not obvious from its name and types has one line saying what it
  is for. A comment in a `.cpp` file belongs to the line below it.
- Plain words, whole sentences, present tense, as in the rest of this repository.

## 5. The language

Use what C++17 gives, where it makes the code say what it means:

| Use | In place of |
|---|---|
| `std::optional<T>` as a result | `bool` with an out-parameter, or a sentinel such as `NONE` in new code |
| `std::string_view` for text that is only read | `const std::string &`, `const char *` |
| Structured bindings: `for (auto &[name, symbol] : members)` | `.first` and `.second` |
| `if (auto found = map.find(key); found != map.end())` | A variable that outlives its use |
| `[[nodiscard]]` on a function whose result is an error or a handle | A result that can be dropped by accident |
| `constexpr` and `inline constexpr` tables | `static const` computed at start-up, macros |
| `enum class` | Plain `enum`, integer flags |
| `std::unique_ptr`, or a value | `std::shared_ptr` where there is one owner; raw `new` and `delete` |
| `override` and `final` on every virtual that has them | Leaving the compiler to guess |
| `noexcept` on moves and on functions that cannot fail | Nothing |
| A template or a function reference for a callback in a hot path | `std::function`, which allocates |
| `std::array` and `std::size` | C arrays and `sizeof` arithmetic |
| `static_cast` and the other named casts | C casts on pointers and classes |

Not used: exceptions, RTTI, `dynamic_cast`, `std::any`, iostreams in the libraries, `using namespace` in
a header, macros where a function or a constant does the job.

Names follow what is there: `snake_case` functions and variables, `PascalCase` types, `p_` for a
parameter, `r_` for a result written through a pointer, a trailing `_` for a member.

## 6. Speed

The compiler is used in an editor on every keystroke, and the interpreter runs a game's frame.

- **Nothing is allocated for each instruction the interpreter runs,** or for each call into Godot that
  succeeds. A name Godot needs (`StringName`) is made once and kept.
- **No text is built on the path that succeeds.** An error message is built where the error is raised.
- **A table that is searched is a hash map or a sorted array,** not a vector walked from the start, once
  it can hold more than a few dozen entries.
- **Reserve** a vector whose size is known. **Move** what is not used again. Pass anything larger than
  two words by `const &`.
- **Measure.** A change made for speed comes with its numbers from `compiler/tests/bench/`, before and
  after. A clean-up must not make any of them worse by more than the noise between runs.

## 7. Changing code

- A clean-up changes no behaviour: no test changes with it, and the IR golden files stay byte for byte.
- One kind of change per commit: a move, a rename, or a rewrite. A move is done with no edits, so that
  the diff shows it as a move.
- The suite is green after every commit, in the normal and the sanitizer builds before a merge.
