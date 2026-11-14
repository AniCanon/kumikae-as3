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

## Not yet done

The app has not run on iOS: not in the Simulator, for the reason above, and not on a device, which waits
on the owner's signing team and go-ahead. Until it has, "runs on iOS" is not claimed anywhere.

## To repeat it

```sh
compiler/tests/build_ios.sh <godot-cpp checkout>
godot --headless --path compiler/tests/godot_project --export-debug "iOS" <folder>/kas3test.ipa
```

The export preset holds a placeholder team id. With a real one, open the Xcode project the export writes
and run it on a device.
