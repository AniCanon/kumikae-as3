---
name: build
description: Build a debug Kumikae AS3 game for desktop, iOS Simulator, Android emulator or Web.
argument-hint: "<desktop|ios-simulator|android|web> [--preset NAME]"
---

From the game directory, invoke `"${CLAUDE_PLUGIN_ROOT}/bin/kumikae" build` with the requested arguments as separate, quoted values. Return the artifact path or the build diagnostic. A build can run without a booted device.
