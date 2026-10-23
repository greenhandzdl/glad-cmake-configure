# GLFW + GLAD CMake 图形引擎

跨平台（Windows / macOS / Linux）的 **现代 OpenGL 图形引擎 / PBR 渲染器**：源自一个用 **GLFW** 建窗、**GLAD** 加载函数的 CMake 起步模板，现已成长为一套分层的 `gldx` 引擎子系统。`src/main.cpp` 只留**最简 hello-triangle** 作为“最小实现”基线，其余一个功能一个 demo 在 `src/demo/{feature}/`（成品演示 `pbr_showcase`、可玩体素世界 `voxel_terrain`、五层透明状态机 + 电介质 Cook-Torrance 水面 + 透视/正交切换的 `glass_water`…共 26 个）。

- **语言与交付**：C++23；引擎以 **C++20 named module `gldx`**（静态库）交付，另有窗口库 `gldxwin` 与命令行库 `gldxcli`——消费者统一 `import gldx; import gldxwin; import gldxcli;`
- **图形面**：OpenGL **4.1 Core**（GLSL `#version 410 core`）· PBR 金属/粗糙 · 级联阴影 · IBL · HDR + MSAA + Bloom + ACES · 分层透明（alpha 由菲涅耳与吸收守恒推导）· 透视/正交切换 · 实例化 · 视锥剔除 · CPU 拾取 · 异步资源管线 · 体素原语
- **构建**：CMake ≥ 3.28 + Ninja；需支持 named modules 的编译器（Clang ≥ 19 / GCC ≥ 14 / 最新 MSVC）；macOS 必须用 Homebrew LLVM（AppleClang 无 `clang-scan-deps`）
- **产物**：`output/GLFW_Template` + 每个 demo 一个可执行（`output/pbr_showcase`、`output/voxel_terrain`…；Windows 加 `.exe`）
- **当前正式版**：**[v1.5.0](https://github.com/greenhandzdl/glad-cmake-configure/releases/tag/v1.5.0)**（全部变更见 [CHANGELOG.md](CHANGELOG.md)）

## 快速开始

```bash
git clone --recursive https://github.com/greenhandzdl/glad-cmake-configure.git && cd glad-cmake-configure
cmake --preset Debug && cmake --build --preset Debug     # 跨平台等价式：cmake -S . -B build -G Ninja && cmake --build build
./output/GLFW_Template              # 一个三角形：工具链 + GL 上下文 + import 三件套全通
./scripts/run.sh pbr_showcase       # 任一 demo：./scripts/run.sh <demo> [--off a,b --on c --quit-after N]
```

配置末尾打印 `GLFW_Template 1.5.0 configuration:`（GLAD 走 system 还是子模块、Assimp 是否启用）；逐平台依赖、`-DGLDX_ENABLE_ASSIMP=OFF` 去重依赖、一键脚本的自愈行为、CLion 工具链与常见报错都在 [入门](doc/user/1-getting-started.md)。

## 文档（按读者角色分三条路径）

| 你是谁 / 想做什么 | 去哪里 |
| --- | --- |
| **使用者**：跑起来、接进自己的程序 | [`doc/user/`](doc/user/README.md)：入门 → ①–⑥ API 章 → [进阶](doc/user/8-advanced.md)（自定义 pass、透明与投影）→ [排错](doc/user/9-troubleshooting.md)；每个 demo 演示什么、按什么键见 [Demo 画廊](doc/user/10-demos.md) |
| **开发者 / 贡献者**：改引擎本身 | [`doc/developer/`](doc/developer/README.md)：[design](doc/developer/design.md)（分层与仓库布局）· [thread-safety](doc/developer/thread-safety.md)（碰 GL 代码前必读）· [CI 与发布](doc/developer/release.md) |
| **AI agent** | [AGENTS.md](AGENTS.md)（检索友好的速查与文件地图）+ [`doc/agents/`](doc/agents/setup-environment.md)（搭环境 / 替用户写程序） |

许可证见 [LICENSE](LICENSE)。这是一个图形/引擎 **core**，不是完整游戏引擎：刻意不做 ECS、骨骼动画、物理/音频、纹理压缩与数据驱动材质图。
