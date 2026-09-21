# 文档中心 / Documentation

本项目的文档按**读者角色**分成三条独立的路径，各自归到独立目录。挑一条开始即可，不必通读。

| 你是谁 / 想做什么 | 去这里 |
| --- | --- |
| **使用者**：从零搭建、跑起来、进阶把 `gfx` 接进自己的程序、以及需要特别当心的坑 | 👉 [`user/`](user/README.md) |
| **开发者 / 贡献者**：想理解 `gfx` 为什么这样分层、GL 调用如何保证只在一个线程发生 | 👉 [`developer/`](developer/README.md) |
| **AI Agent / 自动化**：需要被机器快速检索的"装什么、怎么建、怎么跑、报错怎么办"，以及帮用户搭环境/写程序的完整手册 | 👉 [`../../AGENTS.md`](../AGENTS.md)（速查）+ [`agents/`](agents/setup-environment.md)（深度手册） |

---

## 三条路径分别覆盖什么

### 1. 面向使用者 — [`user/`](user/README.md)
按**由易到难**分章，基础部分一章只讲一个 API 主题，可顺序读也可直接跳：

| 难度 | 文档 | 内容 |
| --- | --- | --- |
| 🟢 入门 | [user/1-getting-started.md](user/1-getting-started.md) | 前置要求、克隆（含子模块）、各平台依赖、四种构建运行方式、演示操作 |
| 🟢 ① | [user/2-core-setup.md](user/2-core-setup.md) | 接入 `main.cpp` 的最小骨架：窗口/渲染线程/Renderer/每帧形状 |
| 🟢 ② | [user/3-geometry-scene.md](user/3-geometry-scene.md) | 网格、材质、场景层级、实例化 |
| 🟡 ③ | [user/4-assets-loading.md](user/4-assets-loading.md) | 贴图/模型的异步两阶段加载与拒收规则 |
| 🟡 ④ | [user/5-lighting-ubo.md](user/5-lighting-ubo.md) | 光照数据、UBO 绑定第一课、采样器、IBL |
| 🟢 ⑤ | [user/6-camera-picking.md](user/6-camera-picking.md) | 相机双控制模型、透视⇄正交、视锥剔除、鼠标拾取 |
| 🟢 ⑥ | [user/7-voxel-basics.md](user/7-voxel-basics.md) | 体素子系统：方块表/地形/成块网格/射线交互/粒子 |
| 🔴 进阶 | [user/8-advanced.md](user/8-advanced.md) | 自定义渲染 pass、后期链与运行期开关、GLSL 单一真源、投放约定 |
| 🧰 排错 | [user/9-troubleshooting.md](user/9-troubleshooting.md) | 坑合集（Homebrew LLVM 工具链、`.idea`、黑屏、forward-compat、资源镜像）＋报错速查表 |

### 2. 面向开发者 — [`developer/`](developer/README.md)
- **[developer/design.md](developer/design.md)**：子系统分层与依赖方向、场景图、线性渲染管线（及为何不用完整 render graph）、材质/光照/UBO、两阶段资源管线、HDR 后期链、以 C++20 named module 交付。
- **[developer/thread-safety.md](developer/thread-safety.md)**：把"GL 只在渲染线程"这条不变量落到代码的每一处机制——线程亲和断言、两阶段管线、锁的划分、move-only RAII、`std::expected` 错误传播，附新代码检查清单。

### 3. 面向 Agent — [`../AGENTS.md`](../AGENTS.md) + [`agents/`](agents/setup-environment.md)
- 仓库根 [`../AGENTS.md`](../AGENTS.md)（**自动发现入口**）：面向检索的速查——关键事实、逐平台命令、文件地图、硬约束、常见报错→修复。
- [`agents/setup-environment.md`](agents/setup-environment.md)：agent **替用户搭环境**的幂等流程（检测→安装→配置构建→验证，带成功判据）。
- [`agents/author-program.md`](agents/author-program.md)：agent **替用户写/改程序**的运行手册（消费 `gfx` 的接线、子系统按需装配与极简管线、已验证 API 速查、异步加载、交付自检）。

命令与标识符保持原文，便于精确匹配。**入口 `AGENTS.md` 刻意留在仓库根**——那是 agent 自动发现的标准位置。

## 阅读顺序建议

- 只想跑起来看效果：[user/1-getting-started.md](user/1-getting-started.md) 一篇足够。
- 要基于引擎写程序：[user/1](user/1-getting-started.md) → [user/2](user/2-core-setup.md)，其余 API 章（[3](user/3-geometry-scene.md)～[7](user/7-voxel-basics.md)）按需跳读。
- 要改引擎代码或新增会碰 GL 的功能：[developer/thread-safety.md](developer/thread-safety.md) **先读再动手**。
- 想知道某个东西在哪：[AGENTS.md](../AGENTS.md) 的文件地图与 [developer/design.md](developer/design.md) §1 的分层图。

## 范围 / 路线图

这是一个图形/引擎 **core**，不是完整游戏引擎。当前刻意不做（将来候选）：ECS、骨骼动画/蒙皮、物理/音频/通用输入映射、纹理压缩（KTX/BC/ASTC）、数据驱动的材质/着色器图。详见 [user/](user/README.md) 与 [developer/design.md](developer/design.md)。
