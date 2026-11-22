# The AS3 language

This page says which ActionScript 3 the compiler accepts and runs, where it differs from Adobe's compiler
and from Flash Player, and what it adds. It's for someone who writes AS3. For Godot's API, nodes and
signals, read [Using AS3 in Godot](godot.md).

## Try it

You can check and run AS3 from the command line, without Godot. Build `kas3c` first, as the
[README](../README.md#get-started) describes.

```actionscript
package {
    public class Main {
        public function Main() {
            var scores:Vector.<int> = new <int>[3, 1, 2];
            scores.sort(function(a:int, b:int):int { return a - b; });
            trace("Sorted: " + scores.join(", "));

            var total:int = 0;
            for each (var score:int in scores) {
                total += score;
            }
            trace("Total: " + total);
        }
    }
}
```

`kas3c run` constructs the class in the first file, as Flash constructs a document class:

```sh
compiler/build/kas3c run Main.as
```

```text
Sorted: 1, 2, 3
Total: 6
```

`kas3c check` reports errors without running anything. Each error has a file, a line and a column:

```text
Bad.as:4:29: error: can't use a value of type 'String' where 'int' is expected
Bad.as:5:19: error: undefined name 'cuont'
```

A program is every file you pass, checked together. Each file holds one package block with one public
definition, named after the file. Inside Godot, the program is every `.as` file in the project.

## What runs

| Area | Supported |
|---|---|
| Classes | Fields, constructors, methods, static members, getters and setters, inheritance with `override` and `super`, `final`, and the four access levels |
| Interfaces | Methods, getters and setters. A class can implement several, and an interface can extend several |
| Functions | Default parameters, `...rest`, `arguments`, function expressions and functions declared inside a function, which capture their variables, methods as values, `call` and `apply` |
| Statements | `if`, `switch`, `while`, `do`, `for`, `for in`, `for each`, labeled `break` and `continue`, `try`/`catch`/`finally`, `throw` |
| Types | `int`, `uint`, `Number`, `Boolean`, `String`, `Object`, `*`, `Class`, `Function`, and your own classes |
| Operators | All of AS3's, including `is`, `as`, `instanceof`, `in`, `delete` and `typeof` |
| Casts and conversions | `Enemy(value)` casts to a class or an interface: it gives the value when it is one, `null` for `null`, and TypeError #1034 for anything else. `Dictionary(x)` is a cast too. `int(x)`, `uint(x)`, `Number(x)`, `String(x)` and `Boolean(x)` convert. The other library classes do what they do in Flash: `Array(x)`, `Object(x)` and `Vector.<T>(x)`; `Error(x)` and its subclasses create an error; `RegExp(x)` gives `x` itself when it's a `RegExp`, and a new one otherwise; `Date(x)` gives the current time as text |
| Collections | `Array`, `Vector.<T>`, `flash.utils.Dictionary`, and object literals |
| Untyped values | Properties, methods and library calls by name on a `*` or `Object` value. A name that isn't there is Flash's ReferenceError #1069 |
| Standard library | `String`, `Number`, `Math`, `Date`, `RegExp`, `JSON`, `Error` and its subclasses, and the top-level functions such as `parseInt`, `isNaN` and `encodeURIComponent` |
| Packages | Several files, packages, imports, and package-level functions and variables |

The [built-in library](standard-library.md) is AS3's top level and `flash.utils.Dictionary`. The rest of the Flash API, such as
`flash.display.Sprite`, isn't there. In Godot you use Godot's classes instead.

## What isn't supported

The compiler reports each of these as an error with its own message.

**Outside the language as accepted here:**

- E4X (XML literals and operators), namespaces, the `with` statement, and `include`.
- `dynamic` classes. The built-in dynamic classes, such as `Object` and `Array`, still take new properties.
- `prototype`. Classes are sealed, and a class has only its own static members.
- `this` or `super` inside a function expression or a function declared inside a function. In AS3, `this`
  there is the global object, not the object of the surrounding method. Read the object into a variable
  first, or use a method.

**Not built yet:**

- **Weak keys:** `new Dictionary(true)`.
- **A property added to a `Date`, a `RegExp`, an `Error` or a `Function`.** Reading a name that one of
  these doesn't have gives `undefined`, as in Flash.
- **A static library function as a value:** `Math.max`, `String.fromCharCode`, `trace`. A method of a
  value works: `list.push`, `text.charAt`.
- **`is` and `as` with `Int64`.** Store the value in an `Int64` variable instead: it's converted, and
  checked, as it is stored.
- **A method of an interface that a class gets only from the built-in library or from Godot's API,** and a
  class that extends `RegExp` or `Dictionary`.

`kas3c check` accepts some of these. `kas3c run` and Godot then refuse the program, or stop it where it
reaches the construct, with an error that says `is not supported by the interpreter yet`.

## Additions to AS3

- **`float`.** Use `var x:float` or `float(value)` for IEEE 754 single precision. Assignment, arguments,
  returns and vector writes round to binary32, including when the input is an `Int64`. Two float operands
  produce a float for `+`, `-`, `*`, `/` and `%`. With an `int`, `uint`, `Int64` or `Number` operand,
  these operations use the existing `Number` rules. Literals keep their ordinary types. For example,
  `float(16777216) + float(1)` gives 16777216, while `float(16777216) + 1` gives 16777217.
  Scalar variables start at NaN. `float()` and `float(null)` give positive zero. Conversions preserve
  signed zero, infinities and NaN. At a `*` or `Object` boundary a float becomes a Number. Use
  `is Number` to test that value and `float(value)` to recover single precision. Formatting uses
  Number's methods on the rounded value. Existing AS3 declarations named `float` keep their meaning.
- **`Vector.<float>`.** Each element occupies four contiguous bytes and starts at positive zero.
  `Vector.<Number>` uses eight bytes per element. Assign a float vector to a Godot
  `PackedFloat32Array` to copy the packed values.
- **`Int64`.** A 64-bit integer type, because Godot's integers have 64 bits. Arithmetic on it wraps at 64
  bits, `/` gives a `Number`, and a comparison with a `Number` is exact. An `Int64` converts to `int`,
  `uint` or `Number` where one is expected, and raises a `RangeError` if the value doesn't fit. Writing
  `int(x)` truncates instead, as in Flash.
- **`[Export]` and `[Signal]`.** Metadata tags on fields of a class that extends a Godot class. See
  [Using AS3 in Godot](godot.md#export-a-field-to-the-inspector).
- **Operators on Godot's value types.** `position + velocity * delta` works on `Vector2` and the other
  math types. See [Using AS3 in Godot](godot.md#godots-values-and-operators).
- **Values held in place.** A `Vector.<Vector2>` or `Vector.<Vector2i>` stores the values themselves and
  can't hold `null`.

## How the compiler checks types

`kas3c check`, the editor and Godot all report the same errors.

### What is checked

- **Names.** Every name must resolve: locals, parameters, members, inherited members, imports and
  package-level definitions. An ambiguous name is an error.
- **Members.** A member reached through `.` must exist and be visible from where you use it.
- **Values.** A value you store, pass, return, or give as a parameter default must fit the declared type.
- **Calls.** The number of arguments must match the function, method or constructor.
- **Returns.** A function with a declared type must return a value, and a `void` function can't.
- **Overrides.** `override` is present exactly when a base class has the method, and the override repeats
  its parameters and return type. Methods that `Object` declares, such as `toString`, need no `override`.
- **Interfaces.** A class must have a public method or accessor, with the same signature, for every member
  of every interface it names.
- **Writes and reads.** You can't write to a `const`, a method, or a property with no setter, or read a
  property with no getter.

### What fits where

A value of type A fits where B is expected when one of these holds:

- Either is `*`.
- A is B, or B is `Object` or `Boolean`.
- Both are number types: `int`, `uint`, `Number`.
- A is `null`.
- A is a class and B is one of its base classes, or an interface it implements.
- Both are `Vector` with the same element type. `Vector.<Sub>` doesn't fit `Vector.<Base>`.
- A is a class itself, such as `Player`, and B is `Class`.
- B is one of Godot's collections and A is the AS3 collection that converts to it. See
  [Using AS3 in Godot](godot.md#collections).

When B is a subclass of A, the error asks for a cast:
`a value of type 'Base' may not be a 'Sub'; use 'as' or a cast`. Where calling the type isn't a cast, as
with `Array` or `Date`, the error says `use 'as'` alone.

### How expressions are typed

| Expression | Type |
|---|---|
| Arithmetic on two numbers | `Number`. `int + int` doesn't wrap until you store it in an `int` |
| Bitwise operators | `int`, and `uint` for `>>>` |
| `+` with a `String` on either side | `String` |
| A whole-number literal | `int` if it fits, then `uint`, then `Number` |
| An assignment | The type of what is assigned to |
| `x as T` | `T`. The value is `null` when `x` isn't a `T`, for `int`, `uint`, `Number` and `Boolean` too: stored in a variable of such a type, the `null` becomes `0` or `false` |
| `v[i]` on a vector, with a numeric `i` | The element type. Any other index is `*` |
| An unknown member of `Object`, `Array`, `Dictionary`, `Date`, `RegExp` or `Error` | `*` |
| A call through a `Function`, `Class` or `Object` value | `*` |

### What isn't checked

The checker reports an error only where AS3's rule is certain. It doesn't check operands of operators and
comparisons (except on Godot's value types), compound assignment such as `s += 1`, conditions, calls
through a `Function` value, or `null` stored in a number. It reports a missing member only when it knows
every member of the type, so nothing is reported on a `*` value.

## Runtime errors

A failure at run time is an AS3 error with a class, a message and a stack trace. An error that nothing
catches is printed with the `.as` file and line of each call:

```text
TypeError: Error #1009: Cannot access a property or method of a null object reference.
	at Crash/hit()[Crash.as:10]
	at Crash()[Crash.as:6]
```

The error classes and numbers are Flash's where Flash has them: #1009 for `null`, #1034 for a failed
conversion, #1125 for a vector index out of range, #1023 for a stack overflow.

| Situation | What happens |
|---|---|
| Recursion without end | Error #1023 at the 10,001st nested call. The program doesn't crash |
| A script that runs too long | Error #1502 after 15 seconds, which no `catch` stops and no `finally` runs after |
| A regular expression that backtracks without end | An `Error` after ten million steps, plus a thousand for each character of the text |
| An `Array` or `Vector` that contains itself, converted to text | Error #1023 |
| A pattern that can't be parsed | A `SyntaxError` where the `RegExp` is created |

Memory is reference counted. A collector frees objects that refer to each other in a cycle.

## Differences from Adobe's compiler and Flash Player

### Names

- **Nothing shadows anything at package level.** A simple name is looked up in the file, its package, the
  top-level package and every import at once. Two different matches are an error. Write the name with its
  package to choose one.
- **A function can declare a variable twice only with the same type.** Adobe's compiler warns. This one is
  silent for the same type, and reports an error for different types.
- **A built-in member can be redefined with or without `override`.** A subclass of `Array` can define
  `push` either way. Adobe's compiler is stricter.
- **A class value has only its own static members.** Static members aren't inherited: reach one through
  the class that declares it.
- **`this` inside a function expression in a static method is an error.**
- **Static code doesn't see instance members.** A name that matches only an instance member is an error. A
  name that also matches something further out, such as a base class's static member, means that.
- **A `protected` member is reachable through any object,** from code in the declaring class or a
  subclass.
- **A dotted name prefers the longest package.** If package `a` defines `b` and package `a.b` defines `C`,
  then `a.b.C` is the class.
- **A helper class outside the package block counts as part of the file's package** when the compiler
  decides whether an `internal` class is visible. Adobe's compiler is stricter.
- **A built-in name can't be told apart from an imported one with the same spelling.** A class named
  `Object` in an imported package would make a bare `Object` ambiguous. That's why Godot's `Object` is
  `GodotObject`.
- **Inside a class whose base class is unknown, undefined names aren't reported,** because the missing
  class could declare them.

### Values and collections

- **Property order is fixed.** `for in`, `for each` and `JSON.stringify` visit the keys of an `Object`, a
  `Dictionary` or an `Array`'s extra properties in the order each was first set. AS3 leaves the order
  unspecified.
- **A number or Boolean used as a key is its text.** `o[1]` and `o["1"]` are the same property, on a
  `Dictionary` too. An object used as a key on a `Dictionary` is the object itself.
- **A loop over properties visits the keys that existed when it began.** A key deleted on the way is
  skipped, and a key added isn't reached.
- **Every `Array` is dense.** An index never written is a hole, as in Flash: `for in` skips it, reading it
  gives `undefined`, and `join` writes nothing for it. But an array with one element at a far index
  allocates every place before it.
- **Strings are counted in UTF-16 units,** as in Flash. A character outside the Basic Multilingual Plane
  has a `length` of 2. `toUpperCase` and `toLowerCase` map one character to one character, so
  `"ß".toUpperCase()` stays `"ß"`.
- **AS3 has no wrapper objects.** `new String(x)` and `new Number(x)` give the value itself.

### Static fields and missing objects

Both follow Flash. A class's static fields are set the first time the class is used, a base class before
its subclasses. A call on `null` evaluates its arguments first, and then raises the error.

### Regular expressions

The syntax is ECMAScript 3's, with what AS3 adds: the `s` and `x` flags, named groups
`(?P<name>...)`, fixed-length lookbehind, atomic groups and possessive quantifiers. A pattern that can't
be parsed raises a `SyntaxError`. Flash Player raises nothing and creates a `RegExp` that never matches.

### JSON

`JSON.parse` and `JSON.stringify` follow ECMA-262, fifth edition. An object of your own class is written
by its public fields and then its public getters. A `Dictionary` is written as `"Dictionary"`. Nesting
deeper than 1,000 levels is Error #1023.

### What hasn't been compared with Flash Player

The library's behavior was written from the AS3 language reference and from the source of avmplus, the
open-source virtual machine that Flash Player is built on. The texts `Date` produces, the error numbers
JSON raises, and the errors of the URI functions haven't been compared with a running Flash Player.

## Documentation in the editor

Select a completion to read what the name does. The entries for `Array.push`, `Node.getNode` and
`Node.queueFree` also show short AS3 examples. Hover over a name already in your script to see its declaration and description.
Signature help shows a function's description while you type its arguments. You can also browse the
documented [top-level AS3 library](../compiler/builtins/toplevel.as) and
[`flash.utils` declarations](../compiler/builtins/flash_utils.as).

Document your own classes, methods, properties and package definitions with an ASDoc comment immediately
before the declaration. When a declaration has metadata such as `[Export]`, put the comment above the
metadata. For a property with a getter and setter, write its description on the getter. The editor uses
the setter's comment when only the setter has one.

```actionscript
/**
 * Adds points to the player's score and returns the new total.
 * @param points Number of points earned this turn.
 * @return The updated score.
 */
public function addScore(points:int):int {
    score += points;
    return score;
}
```

The editor displays the comment text, including the parameter and return notes. The same format works in
a declaration file passed with `--declarations`.

## Declaration files

A declaration file tells the compiler which names exist, without their code. It's ordinary AS3 in which
classes and functions are `native` and have no body.

```actionscript
package physics {
    public native class Body {
        public native function applyForce(force:Force):void;
    }
}
```

Pass one with `--declarations`:

```sh
compiler/build/kas3c check --declarations Body.as --declarations Force.as Game.as
```

The built-in library and Godot's API are declaration files too, built into `kas3c`. Inside a declaration
file, names resolve only to other declarations, so your program can't change what they mean.

A declaration file can give a class operators with metadata tags. This is how Godot's `Vector2` gets its
arithmetic.

```actionscript
package physics {
    [Operators]
    [Operator(op="+", right="Force", returns="Force")]
    [Operator(op="*", right="Number", returns="Force")]
    public final native class Force {
        public native function Force(x:Number = 0, y:Number = 0);
        public native function get length():Number;
    }
}
```

`[Operators]` says the list that follows is complete, so that `force - other` is an error. In your own
source files these tags have no effect.

## Limits

- **Nesting:** brackets, blocks and `else if` chains nest at most 256 deep. A file nested deeper than
  about 512 levels in total is rejected with `code is nested too deeply`.
- **Chains:** a chain of operators, member accesses or calls can have 448 links. A longer one is reported
  as `expression is too long`.
- **Type arguments:** `Vector.<Vector.<...>>` nests at most 32 deep.
- **File size:** a source file can be at most 4 GB.
