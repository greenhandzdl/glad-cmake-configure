# 着色器参考镜像

这些 `.glsl` 文件是引擎**实际编译的 GLSL 的可浏览镜像**。它们在构建期和运行期
都**不被读取**——这里没有任何东西会被加载。

单一真源是 [`src/gldx/shader/*Shaders.h`](../../../src/gldx/shader) 里嵌入的
raw string（`gldx::shaders::k*`）。把 GLSL 放在头文件里换来三件事：

- 运行期零路径/工作目录依赖（三平台 CI 因此安全），
- 着色器源码随编译产物一起交付，
- 编译期就固定的东西不需要任何异步加载管线。

下表每个镜像对应注出的头文件常量。**改镜像就要同步改它的头文件孪生**——
运行时以头文件为准。（`VoxelShaders.h` 与 `ParticleShaders.h` 没有镜像文件；
体素/粒子 GLSL 只住在这两个头里。）

| 镜像文件               | 唯一真源（`src/gldx/shader/…`）                          |
| -------------------- | ------------------------------------------------------------- |
| `pbr.glsl`           | `ShaderLib.h` — `kPbrVertex`、`kPbrFragment`                  |
| `depth.glsl`         | `ShaderLib.h` — `kDepthVertex`、`kDepthFragment`              |
| `skybox.glsl`        | `ShaderLib.h` — `kSkyboxVertex`、`kSkyboxFragment`            |
| `instanced.glsl`     | `ShaderLib.h` — `kInstancedVertex`、`kInstancedFragment`      |
| `postprocess.glsl`   | `PostProcessShaders.h` — `kPostVertex`、`kBrightPassFragment`、`kBlurFragment`、`kCompositeFragment` |
| `ibl.glsl`           | `IblShaders.h` — `kIblVertex`、`kSkyGenFragment`、`kIrradianceFragment`、`kPrefilterFragment`、`kBrdfFragment` |
| `sprite.glsl`        | `SpriteShaders.h` — `kSpriteVertex`、`kSpriteFragment`        |
| `debug.glsl`         | `DebugShaders.h` — `kDebugVertex`、`kDebugFragment`           |

所有程序都面向 **GLSL `#version 410 core`**（OpenGL 4.1 Core 基线）。
