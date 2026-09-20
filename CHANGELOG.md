# 更新日志 / Changelog

本文件记录 `GLFW_Template`（`gfx` 引擎 + 演示应用）各版本的变更。
版本标签遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

## v1.2.0

### 新增

- **正交投影切换（Tab）**：`Camera` 增加 `SetOrthographic` / `ToggleProjection`，正交盒高度按当前
  fov + 轨道半径匹配透视取景；天空盒改用 `SkyboxViewProj`（恒为去平移的透视盒），在正交模式下仍正确。
- **`scripts/release.sh`**：一键把版本号同步到 `Platform.h`、`CMakeLists.txt`、`README.md`、`AGENTS.md`
  等全部锚点（可选 `--commit` / `--tag`），根治发布时源码版本与 tag 漂移；脚本刻意不 push、不建 Release。

### 修复

- **鼠标拾取选错物体**：`PickRay` 原以「远平面反投影点 − 眼位」构造射线且直接吃 GLFW 窗口坐标，
  在 Retina（内容缩放 2×）下整条射线被压向左上半、正交模式下更是方向全错。现改为反投影近/远两点
  构造与投影无关的射线，拾取坐标先按窗口尺寸归一化再乘 framebuffer 像素。
- **Bloom 对 PBR 材质无效果**：亮度过阈原为 1.0，而球体线性 HDR 仅 0.2–0.9 几乎全被键掉；且上一版
  为保小高光改的全分辨率让光晕只剩几像素不可见。现阈值降到 0.35、bloom 缓冲回半分辨率配 6 次宽模糊、
  强度 2.0，方向光高光 ×3 抬进 HDR，材质高光点终于长出柔光环。
- **GL 状态泄漏**：上一帧 HUD / sprite / debug 段关闭的 `GL_DEPTH_TEST` / `GL_CULL_FACE` 未复原，
  泄漏进本帧导致级联阴影图空、HUD 被剔除、正/背面剔除失效。`CascadedShadowMap` / `EnvironmentMap` /
  `SpriteBatch` / `DebugDraw` 各自显式设定所需状态，不再依赖上游段善后。
- **字体重影 / 糊成一团**：`Font` 里 `stbtt_packedchar` 的位图像素以整数除法算 UV 致截断为 0（全部采样
  图集原点），且用图集包围盒（过采样下是 bake 空间的 2×）当 quad 尺寸使字形拉伸重叠。改为浮点除法算 UV、
  用 `xoff2-xoff` 的真实 bake 空间跨度算 quad 尺寸。

## v1.1.0

### 新增

- **按读者角色重组的文档体系**：`doc/user/`（入门 / 基础 / 进阶 / 排错四篇，按难易递进）、
  `doc/developer/`（设计 + 线程安全，已全量中文化）、`doc/agents/`（面向 agent 的
  环境搭建与写程序运行手册），入口见 `doc/README.md`。
- **`AGENTS.md`**：仓库根的 AI agent 速查（关键事实 / 逐平台命令 / 文件地图 / 硬约束 / 报错→修复）。
- **`CMakePresets.json`**：macOS 上把编译器钉到 Homebrew LLVM，使命令行与 CLion 都能正确
  配置 C++20 named module（`condition: Darwin`，不影响 Linux/Windows 与 CI）。
- **`CHANGELOG.md`**：本文件。

### 修复 / 健壮性

- **`Font::LoadFromFile`**：校验 `tellg()` 得到的大小，`<=0` 或超过 64 MB 直接返回错误，
  杜绝畸形或超大字体文件驱动无界分配（潜在崩溃 / DoS）。
- **`ThreadPool::WorkerMain`**：为每个出队任务套 `try/catch(...)`，防止任务抛异常逃逸出
  工作线程顶层触发 `std::terminate`；异常任务被丢弃，关联 `std::future` 以 `broken_promise` 呈现。

### 重构

- **演示 `main.cpp`**：把 `1..5` 五段重复的按键切换边沿检测收敛为一个 `edgeToggle` lambda。

### 文档一致性

- `thread-safety.md` 补充 `ThreadPool` 顶层异常兜底说明；`src/assets/models/README.md` 等
  指向开发者文档的链接随目录重组更新。

## v1.0.0

首个正式版本：跨平台 OpenGL 4.1 Core / PBR 图形引擎 `gfx`（以 C++20 named module 交付的静态库）
与交互式演示应用。

- **渲染**：PBR 金属/粗糙工作流、级联阴影（CSM + PCF）、基于图像的照明（IBL：辐照度 / 预滤波 /
  BRDF LUT）、HDR + MSAA + Bloom + ACES 色调映射、天空盒。
- **架构**：分层子系统 + 单向依赖；线性渲染 pass 链（Shadow / Geometry / PostProcess / DebugHud）；
  场景层级 `Scene`/`SceneNode`/`Transform`；两阶段异步资源管线（后台解码、渲染线程上传）。
- **工具**：精灵批次 + 位图字体 HUD、GPU 实例化绘制、视锥剔除、调试线框、CPU 拾取、CPU+GPU 帧分析。
- **平台**：Windows / macOS / Linux；依赖 GLAD / GLFW / GLM / STB / Assimp；三平台 CI 发布。
- **线程安全**：GL 仅在单一渲染线程，辅以线程亲和断言、move-only RAII、`std::expected` 错误传播。
