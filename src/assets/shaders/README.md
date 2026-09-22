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

> 另有一个**可选**的外部加载入口 `gldx::ShaderProgram::CreateFromFiles(vertPath, fragPath)`，
> 供 demo 或用户热加载自己写的 `.glsl`。它不改变上面的约定：引擎自带着色器仍以头文件内嵌
> 为唯一真源，本镜像目录**顶层**的文件依旧不被任何构建/运行路径读取。
>
> **例外**：子目录 [`file_demo/`](file_demo/) 下的文件**不是**镜像——它们是 demo 真正从磁盘加载的着色器：
> - `triangle.vert` / `triangle.frag`：`shader_file` demo 走两文件版 `CreateFromFiles(vertPath, fragPath)` 加载。
> - `points.vert` / `squares.geom` / `points.frag`：`geometry_shader_file` demo 走多阶段版 `CreateFromFiles({{Vertex,…},{Geometry,…},{Fragment,…}})` 加载（含一个从文件装配的几何阶段，两文件版表达不了）。
