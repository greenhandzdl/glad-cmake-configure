# 使用者文档 / User Docs

面向"要把项目**跑起来**，进而把它当 **`gfx` 引擎接入自己的程序**"的读者。

这里按**由易到难**分成四篇。建议按顺序读，也可直接跳到你需要的难度：

| 难度 | 文档 | 读完你能 |
| --- | --- | --- |
| 🟢 **入门** | [1-getting-started.md](1-getting-started.md) | 从零把项目克隆、装依赖、配置、构建、运行，看懂演示界面与按键 |
| 🟡 **基础** | [2-basic-usage.md](2-basic-usage.md) | 把 `gfx` 接进你自己的 `main.cpp`，创建几何/材质/场景节点，加载贴图模型，跑通最小渲染循环 |
| 🔴 **进阶** | [3-advanced.md](3-advanced.md) | 自定义渲染 pass、理解两阶段异步资源管线、调后期与运行期开关、处理模块边界与前向兼容 |
| 🧰 **排错** | [4-troubleshooting.md](4-troubleshooting.md) | 遇到黑屏 / CLion 无配置 / `clang-scan-deps` 缺失等坑时，按"症状→根因→修复"速查 |

---

## 该从哪一篇开始？

- **"我只想看它跑起来长什么样"** → [🟢 入门](1-getting-started.md) 一篇就够。
- **"我要基于它写自己的程序"** → 入门后直接 [🟡 基础](2-basic-usage.md)。
- **"我要扩展引擎 / 加渲染效果"** → 先 [🟡 基础](2-basic-usage.md)，再 [🔴 进阶](3-advanced.md)，并且**动手前务必读**开发者文档 [../developer/thread-safety.md](../developer/thread-safety.md)。
- **"卡住了 / 报错了"** → [🧰 排错](4-troubleshooting.md)。

## 这些文档不讲什么

设计原理（为什么这样分层、为何用线性管线而非 render graph、named module 的取舍）与线程安全机制的**权威说明**在开发者区：
[../developer/design.md](../developer/design.md) 与 [../developer/thread-safety.md](../developer/thread-safety.md)。
使用者文档只给"怎么做"和一个"为什么"的简述，深入原因请转过去。

> 权威用法示例永远是仓库里的 [`../../src/main.cpp`](../../src/main.cpp)——它演示了引擎的全部对外用法；文档与它冲突时以它为准。
