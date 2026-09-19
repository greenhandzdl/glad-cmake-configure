# assets/

Runtime data and content for the engine, kept separate from code. Two kinds of
content live here, each with its own conventions:

| Folder                         | Contents                                                                 |
| ------------------------------ | ------------------------------------------------------------------------ |
| [`shaders/`](shaders/README.md) | Read-only **reference mirrors** of the embedded GLSL (not loaded).        |
| [`models/`](models/README.md)   | Drop-in mesh assets (FBX / OBJ / glTF) loaded asynchronously at runtime.  |

## Source-isolation convention

The whole tree is deliberately split so that *engine code*, *application code*,
*third-party implementation*, and *content* never blur:

```
src/
  gfx/**            engine subsystems (core, geometry, texture, material,
                    shader, camera, light, shadow, scene, render, text,
                    assets, debug) — no application logic; the whole set is
                    delivered as the C++20 named module `gfx` (gfx.cppm)
  gfx/core/Platform.h  the demo's plain-text include (GLFW + window constants);
                       deliberately NOT part of the module
  main.cpp          the demo application entry point — `import gfx;` wires it up
  gfx/third_party/stb_image_impl.cpp  single third-party implementation TU
                       (STB image / truetype); never leaks onto the module interface
  gfx/shader/*Shaders.h   GLSL lives here as embedded raw strings — the ONLY
                          source of truth for shader code
assets/
  shaders/          browsable copies of that GLSL (never compiled/loaded)
  models/           content loaded through gfx::AssetManager
```

Key rules:

1. **GLSL is embedded, not loaded.** The engine compiles shader source from the
   `gfx::shaders::k*` string constants; `assets/shaders/` is documentation only.
   This keeps runs free of working-directory / path assumptions, which matters
   for the three-platform CI.
2. **Models are the only thing loaded from disk at runtime**, and always through
   `gfx::AssetManager` (background decode + render-thread upload). Large binaries
   are kept out of the repo by default — see the note in
   [`models/README.md`](models/README.md).
3. **`src/main.cpp` is an application, not part of the engine.** It may reach
   into `gfx/**`, but nothing in `gfx/**` may reach back into `main.cpp`.
