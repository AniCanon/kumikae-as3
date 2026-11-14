# First run on iOS: what works and what is in the way

On 2026-11-14, ahead of stage 4, the test project was built for iOS to learn early what stands between
the desktop and a phone. Godot 4.7.2, its official iOS export template, Xcode 27.

## What works

| Step | Result |
|---|---|
| The compiler, the interpreter and the extension built for iOS | Works, for a device (arm64) and for the Simulator (arm64 and Intel). No source change was needed. `compiler/tests/build_ios.sh` does it and makes an `.xcframework`, 3.9 MB a slice |
| Godot's iOS export of a project with AS3 scripts | Works: the scripts go into the pack and the extension into the app |
| The app built by Xcode for a device | Works, unsigned: a 103 MB debug app |
| The test scene (`ios_test.tscn`) on desktop | 14 of 14 checks pass: a script on a `CharacterBody2D`, singletons, constants, static methods, operators, a signal, `Int64`, an AS3 error reported |

## What it found

**A game exported as a pack could not find its scripts, on any platform.** The extension looked for the
project's `.as` files by walking a folder on disk. An exported game keeps its files inside a pack. It now
reads the project through Godot's own file access, and a test runs the test project from a pack on
desktop. This was the most useful result of the exercise: every exported game needed it.

**The Simulator can't be used with Godot's official template on this Mac.** The template's Simulator
library is built for Intel processors only. An Apple-silicon Simulator app does not link against it, and
the iOS 26.4 Simulator refuses to run an Intel app. The ways round it:

| Way | Cost |
|---|---|
| A real device | Needs the owner's signing team and a connected iPhone. The device build already works |
| An older Simulator runtime that still runs Intel apps | A download of several gigabytes |
| A Godot iOS template built from source for the Apple-silicon Simulator | Godot's source and a long build |

## It runs on an iPhone

On 2026-11-14 the app was signed with the owner's development team, installed on an iPhone 17 Pro Max
and launched. The test scene reported, from the phone:

```
iOS
14 passed, 0 failed
PASS  an AS3 script is attached to a CharacterBody2D
PASS  Godot called its _physicsProcess 30 times
PASS  it moved the body 100 units
PASS  singleton: Input.isActionPressed
PASS  singleton: OS.getName
PASS  constant: Vector2.UP
PASS  constant: Color.RED
PASS  static method: Vector2.fromAngle
PASS  operators: Vector2.UP * 2 + Vector2.ONE
PASS  signal: a listener on Input.joyConnectionChanged
PASS  Int64: an object's id survives the trip
PASS  Int64: the largest value
PASS  Int64: a wide value for a Vector2i is refused
PASS  an AS3 error is reported and the engine carries on
```

These are the same scripts, unchanged, that the desktop tests run. They run in the interpreter, which is
what a debug build uses on every platform. A release build as native code, and the speed gate on a
device, are stage 4.

The report is read back from the phone as a file the scene writes in the app's own folder
(`user://as3test.txt`): the app's console did not carry Godot's printed lines.

## Not yet done

- The Simulator, for the reason above. The fix is a Godot export template built from Godot's source with
  an Apple-silicon Simulator library, set as a custom template in the export preset.
- Android, and anything beyond the 14 checks: the full Godot test drivers run from the command line
  with `-s`, which an installed app does not take.

## To repeat it

```sh
compiler/tests/build_ios.sh <godot-cpp checkout>
godot --headless --path compiler/tests/godot_project --export-debug "iOS" <folder>/kas3test.ipa
```

The export preset holds a placeholder team id. Build the Xcode project the export writes with a real
one (`xcodebuild ... DEVELOPMENT_TEAM=<team> -allowProvisioningUpdates`), install it with
`xcrun devicectl device install app`, launch it, and copy `as3test.txt` back with
`xcrun devicectl device copy from --domain-type appDataContainer`.
