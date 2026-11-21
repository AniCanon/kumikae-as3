# The AS3 standard library

The top-level AS3 types and functions are available in every script. Select a name in completion to see
its declaration, parameter types, and description. You can browse those same descriptions together in the
[top-level library](../compiler/builtins/toplevel.as) and [flash.utils](../compiler/builtins/flash_utils.as).

## Choose a collection

| Type | Use it for | Example |
|---|---|---|
| `Vector.<T>` | An indexed sequence with one declared element type | `var scores:Vector.<int> = new <int>[8, 13];` |
| `Array` | An indexed sequence that can mix types | `var row:Array = ["Ada", 13];` |
| `Object` | A record whose properties have string names | `var save:Object = {level: 2, coins: 13};` |
| `flash.utils.Dictionary` | A lookup table where each object can be a distinct key | `var visits:Dictionary = new Dictionary();` after `import flash.utils.Dictionary;` |

Both arrays and vectors support `push`, `pop`, `slice`, and `splice`. Use `slice` to copy a range.
Use `splice` to edit the collection and retrieve the elements you removed.

```actionscript
var scores:Vector.<int> = new <int>[8, 13];
trace(scores.push(21));       // 3, the new length
trace(scores.slice(1, 3));    // 13,21
trace(scores.splice(1, 1));   // 13
trace(scores.join(", "));     // 8, 21
```

## Numbers and text

Use `int` for signed 32-bit counts, `uint` for unsigned 32-bit values, and `Number` for fractions and
trigonometry. `Int64` holds signed 64-bit values returned by Godot, including integer IDs that a
`Number` would round. `Math.sin`, `Math.cos`, and the angles returned by `Math.atan2` use radians.

String indices and lengths are measured in UTF-16 code units. An unsuccessful `indexOf` returns
`-1`. A failed numeric parse returns `NaN`, which you can test with `isNaN`.

```actionscript
trace("🎮".length);                  // 2
trace("level_12".substring(6));      // 12
trace(parseInt("ff", 16));           // 255
trace(isNaN(parseFloat("locked")));  // true
```

## JSON, dates, and errors

Use `JSON.stringify` to serialize an AS3 record and `JSON.parse` to read it. A malformed input raises
`SyntaxError`. Use `Date.time` when a save file needs a numeric timestamp in milliseconds since the Unix epoch.

```actionscript
var save:Object = {level: 2, coins: 13};
var loaded:Object = JSON.parse(JSON.stringify(save));
trace(loaded.level, loaded.coins);   // 2 13

try {
    JSON.parse("[1,]");
} catch (error:SyntaxError) {
    trace(error.name);               // SyntaxError
}
```

The [language guide](language.md#values-and-collections) describes collection order, string conversions,
regular expressions, and differences from Flash. Godot also has engine-side types such as `GodotArray`
and `GodotDictionary`. Their declarations and calling conventions are in [Using AS3 in Godot](godot.md#how-godots-api-is-named).
