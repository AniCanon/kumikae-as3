---
name: test
description: Verify a Kumikae AS3 gameplay fixture inside Godot on one or all supported targets.
argument-hint: "<desktop|ios-simulator|android|all> [--case TARGET::SUITE::METHOD]"
---

From the game directory, invoke `"${CLAUDE_PLUGIN_ROOT}/bin/kumikae" test` with the requested arguments as separate, quoted values. Use all when the developer requests cross-platform verification. Report the actual case result for each destination, including failed or unavailable targets.
