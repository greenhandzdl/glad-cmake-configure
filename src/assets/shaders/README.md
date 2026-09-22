# 着色器目录（已废除的镜像）

本目录顶层的 8 个 `.glsl` 文件**已全部废除**：它们过去是引擎内嵌 GLSL 的"可浏览镜像"，
但镜像与真源极易漂移、维护成本高，如今每个文件只剩一行注释，指向真正的单一真源。

## 唯一真源在哪

引擎实际编译的所有 GLSL 都以 raw string 内嵌在头文件里，形如 `gldx::shaders::k*`。**要读
或改着色器，直接打开下面这些头文件**，不要在本目录找：

| 已废除的镜像        | 唯一真源（`src/gldx/shader/…`）                                                          |
| ----------------- | --------------------------------------------------------------------------------------- |
| `pbr.glsl`        | `ShaderLib.h` — `kPbrVertex`、`kPbrFragment`                                             |
| `depth.glsl`      | `ShaderLib.h` — `kDepthVertex`、`kDepthFragment`                                         |
| `skybox.glsl`     | `ShaderLib.h` — `kSkyboxVertex`、`kSkyboxFragment`                                       |
| `instanced.glsl`  | `ShaderLib.h` — `kInstancedVertex`、`kInstancedFragment`                                 |
| `postprocess.glsl`| `PostProcessShaders.h` — `kPostVertex`、`kBrightPassFragment`、`kBlurFragment`、`kCompositeFragment` |
| `ibl.glsl`        | `IblShaders.h` — `kIblVertex`、`kSkyGenFragment`、`kIrradianceFragment`、`kPrefilterFragment`、`kBrdfFragment` |
| `sprite.glsl`     | `SpriteShaders.h` — `kSpriteVertex`、`kSpriteFragment`                                   |
| `debug.glsl`      | `DebugShaders.h` — `kDebugVertex`、`kDebugFragment`                                      |

（`VoxelShaders.h` 与 `ParticleShaders.h` 一直没有镜像文件；体素/粒子 GLSL 只住在这两个头里。）

所有程序都面向 **GLSL `#version 410 core`**（OpenGL 4.1 Core 基线）。GLSL 内嵌而非加载，换来运行期
零路径/工作目录依赖（三平台 CI 因此安全）、着色器随编译产物交付、无需任何异步加载管线。

## 从磁盘加载的 demo 着色器（已搬到 demo 目录下）

引擎提供一个**可选**的外部加载入口 `gldx::ShaderProgram::CreateFromFiles(...)`，供 demo 或用户热加载
自己写的 GLSL。真正被 demo 从磁盘读取的着色器**不再放在本镜像目录**，而是就近住在各自 demo 的
`assets/shaders/` 下：

- [`../../demo/shader_file/assets/shaders/`](../../demo/shader_file/assets/shaders/)：
  `triangle.vert` / `triangle.frag` —— `shader_file` demo 走两文件版 `CreateFromFiles(vertPath, fragPath)` 加载。
- [`../../demo/geometry_shader_file/assets/shaders/`](../../demo/geometry_shader_file/assets/shaders/)：
  `points.vert` / `squares.geom` / `points.frag` —— `geometry_shader_file` demo 走多阶段版
  `CreateFromFiles({{Vertex,…},{Geometry,…},{Fragment,…}})` 加载（含一个从文件装配的几何阶段）。

> 这些路径由 demo 的 `--vert/--geom/--frag` 命令行传入（demo 从仓库根运行），并非构建/运行期硬编码；
> 引擎自带路径仍以内嵌头文件为唯一真源，本目录顶层文件永不被任何构建/运行路径读取。
