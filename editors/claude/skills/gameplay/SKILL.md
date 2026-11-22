---
name: gameplay
description: Write and check Kumikae ActionScript 3 gameplay scripts and Godot scene tests.
---

Use the AS3 LSP on the project's .as files to look up definitions and references and to check diagnostics
after editing. Use explicit imports and the typed Godot declarations for method signatures. A script extends
the node's Godot class. Engine methods use lowerCamelCase, including _physicsProcess. Wire child nodes and
script collaborators through typed [Export] fields (including your own Node and Resource script classes) and
declare events with [Signal], listening through the signal property's add method instead of using getNode()
or stringly-typed .call(). Keep UI in a CanvasLayer styled by a shared Theme resource, cache Colors and
input action names as static constants, and dirty-check HUD updates to avoid per-frame allocations.

For a behavior that depends on the scene tree, define a public [Test] method and run it inside Godot.
Use TestScene.instantiate with the project's scene, cast it to your root script class, advance frames or
wait for a condition, then assert the observable behavior. Use the test skill to run the same case ID on the
requested targets.
