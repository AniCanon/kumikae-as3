# Stage 0 speed spike (throwaway)

Measures whether hand-written C++ in the shape the AS3 native backend would emit beats Godot C# on gameplay
code. Nothing here is meant to be kept. Plan: `docs/superpowers/plans/2026-11-11-stage-0-speed-spike.md`.

## Build

```sh
git submodule update --init
cmake -S spike -B spike/build -DCMAKE_BUILD_TYPE=Release
cmake --build spike/build -j
```

godot-cpp is pinned at tag `10.0.0-stable` (commit 507ed9d) and built for the Godot 4.7 API it bundles
(`GODOTCPP_API_VERSION=4.7`). It is used for class registration only; measured code calls the engine directly.

`spike/api/` (ignored) holds the API dumped from the local Godot, used to look up method hashes:

```sh
cd spike/api && godot --headless --dump-extension-api && godot --headless --dump-gdextension-interface
```

## Test

```sh
python3 spike/tests/test_load.py
```

`KAS3_GODOT` selects the Godot binary; otherwise `godot` on PATH is used. It must be 4.7.
