---
name: gameplay
description: Write and check Kumikae ActionScript 3 gameplay scripts and Godot scene tests.
---

Use the AS3 LSP on the project's .as files to look up definitions and references and to check diagnostics
after editing. Use the typed Godot declarations for method signatures. A script extends the node's Godot
class. Engine methods use lowerCamelCase, including _physicsProcess. Export fields with [Export], declare
signals with [Signal], and listen through the signal property's add method.

For a behavior that depends on the scene tree, define a public [Test] method and run it inside Godot.
Use TestScene.instantiate with the project's scene, advance frames or wait for a condition, then assert
the observable behavior. Use the test skill to run the same case ID on the requested targets.
