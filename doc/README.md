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
按**由易到难**分四篇，可顺序读也可直接跳：

| 难度 | 文档 | 内容 |
| --- | --- | --- |
| 🟢 入门 | [user/1-getting-started.md](user/1-getting-started.md) | 前置要求、克隆（含子模块）、各平台依赖、四种构建运行方式、演示操作 |
| 🟡 基础 | [user/2-basic-usage.md](user/2-basic-usage.md) | 接入 `main.cpp` 的最小骨架、几何/材质/场景图、最简资源加载、UBO 绑定第一课 |
| 🔴 进阶 | [user/3-advanced.md](user/3-advanced.md) | 自定义渲染 pass、两阶段异步管线、后期链与运行期开关、模块边界与 forward-compat |
| 🧰 排错 | [user/4-troubleshooting.md](user/4-troubleshooting.md) | 坑合集（Homebrew LLVM 工具链、`.idea`、黑屏、forward-compat、资源镜像）＋报错速查表 |

### 2. 面向开发者 — [`developer/`](developer/README.md)
- **[developer/design.md](developer/design.md)**：子系统分层与依赖方向、场景图、线性渲染管线（及为何不用完整 render graph）、材质/光照/UBO、两阶段资源管线、HDR 后期链、以 C++20 named module 交付。
- **[developer/thread-safety.md](developer/thread-safety.md)**：把"GL 只在渲染线程"这条不变量落到代码的每一处机制——线程亲和断言、两阶段管线、锁的划分、move-only RAII、`std::expected` 错误传播，附新代码检查清单。

### 3. 面向 Agent — [`../AGENTS.md`](../AGENTS.md) + [`agents/`](agents/setup-environment.md)
- 仓库根 [`../AGENTS.md`](../AGENTS.md)（**自动发现入口**）：面向检索的速查——关键事实、逐平台命令、文件地图、硬约束、常见报错→修复。
- [`agents/setup-environment.md`](agents/setup-environment.md)：agent **替用户搭环境**的幂等流程（检测→安装→配置构建→验证，带成功判据）。
- [`agents/author-program.md`](agents/author-program.md)：agent **替用户写/改程序**的运行手册（消费 `gfx` 的接线、默认管线约束、已验证 API 速查、异步加载、交付自检）。

命令与标识符保持原文，便于精确匹配。**入口 `AGENTS.md` 刻意留在仓库根**——那是 agent 自动发现的标准位置。

## 阅读顺序建议

- 只想跑起来看效果：[user/1-getting-started.md](user/1-getting-started.md) 一篇足够。
- 要基于引擎写程序：[user/1](user/1-getting-started.md) → [user/2](user/2-basic-usage.md)。
- 要改引擎代码或新增会碰 GL 的功能：[developer/thread-safety.md](developer/thread-safety.md) **先读再动手**。
- 想知道某个东西在哪：[AGENTS.md](../AGENTS.md) 的文件地图与 [developer/design.md](developer/design.md) §1 的分层图。

## 范围 / 路线图

这是一个图形/引擎 **core**，不是完整游戏引擎。当前刻意不做（将来候选）：ECS、骨骼动画/蒙皮、物理/音频/通用输入映射、纹理压缩（KTX/BC/ASTC）、数据驱动的材质/着色器图。详见 [user/](user/README.md) 与 [developer/design.md](developer/design.md)。
