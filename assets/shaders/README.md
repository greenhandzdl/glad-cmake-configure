# Shader reference mirrors

These `.glsl` files are **browsable mirrors** of the GLSL that the engine
actually compiles. They are **not** read at build or run time — nothing here is
loaded.

The single source of truth is the embedded raw strings in
[`src/gfx/shader/*Shaders.h`](../../src/gfx/shader) (`gfx::shaders::k*`).
Keeping GLSL in headers gives us:

- zero runtime file-path / working-directory dependence (CI-safe on all three
  platforms),
- the shader source shipped inside the compiled binary,
- no async asset plumbing for something that is fixed at compile time.

Each file below maps to the header constant(s) noted. If you change one, change
its header twin too (the header wins at runtime).

| Mirror               | Source of truth (`src/gfx/shader/…`)                          |
| -------------------- | ------------------------------------------------------------- |
| `pbr.glsl`           | `ShaderLib.h` — `kPbrVertex`, `kPbrFragment`                  |
| `depth.glsl`         | `ShaderLib.h` — `kDepthVertex`, `kDepthFragment`              |
| `skybox.glsl`        | `ShaderLib.h` — `kSkyboxVertex`, `kSkyboxFragment`            |
| `instanced.glsl`     | `ShaderLib.h` — `kInstancedVertex`, `kInstancedFragment`      |
| `postprocess.glsl`   | `PostProcessShaders.h` — `kPostVertex`, `kBrightPassFragment`, `kBlurFragment`, `kCompositeFragment` |
| `ibl.glsl`           | `IblShaders.h` — `kIblVertex`, `kSkyGenFragment`, `kIrradianceFragment`, `kPrefilterFragment`, `kBrdfFragment` |
| `sprite.glsl`        | `SpriteShaders.h` — `kSpriteVertex`, `kSpriteFragment`        |
| `debug.glsl`         | `DebugShaders.h` — `kDebugVertex`, `kDebugFragment`           |

All programs target **GLSL `#version 410 core`** (OpenGL 4.1 Core baseline).
