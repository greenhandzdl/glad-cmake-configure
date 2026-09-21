# 使用者文档 / User Docs

面向"要把项目**跑起来**，进而把它当 **`gfx` 引擎接入自己的程序**"的读者。

基础部分已按 **一章一个 API 主题** 拆成小章（原"基础用法"一章内容太多，现在每章只讲一件事、可单独读完）：

| 顺序 | 文档 | 读完你能 |
| --- | --- | --- |
| 🟢 **入门** | [1-getting-started.md](1-getting-started.md) | 从零把项目克隆、装依赖、配置、构建、运行，看懂演示界面与按键 |
| 🟢 **① 核心骨架** | [2-core-setup.md](2-core-setup.md) | 用最少的代码接进自己的 `main.cpp`：窗口、渲染线程声明、`Renderer` 初始化、每帧的形状 |
| 🟢 **② 几何与场景** | [3-geometry-scene.md](3-geometry-scene.md) | 造网格、给材质、挂场景层级、批量实例化 |
| 🟡 **③ 贴图与模型加载** | [4-assets-loading.md](4-assets-loading.md) | `AssetManager` 异步两阶段：`Request*` → `ProcessUploads` → `Get*`，以及三条资产拒收规则 |
| 🟡 **④ 光照与 UBO** | [5-lighting-ubo.md](5-lighting-ubo.md) | `LightSetup`/雾、灌 UBO 并绑定（黑屏第一课）、采样器、IBL 环境图 |
| 🟢 **⑤ 相机与拾取** | [6-camera-picking.md](6-camera-picking.md) | orbit/fly 双控制模型、透视⇄正交、视锥剔除、鼠标点中物体三步 |
| 🟢 **⑥ 体素世界** | [7-voxel-basics.md](7-voxel-basics.md) | 方块表→噪声地形→成块网格→上传绘制→射线挖方块→AABB 碰撞站立→粒子点缀 |
| 🔴 **进阶** | [8-advanced.md](8-advanced.md) | 自定义渲染 pass、后期链与运行期开关、GLSL 单一真源、投放目录约定 |
| 🧰 **排错** | [9-troubleshooting.md](9-troubleshooting.md) | 遇到黑屏 / CLion 无配置 / `clang-scan-deps` 缺失等坑时，按"症状→根因→修复"速查 |

①～⑥ 之间互相引用很少、可单独跳读；但第一次通读建议按顺序（①→④→⑤ 是最短主线）。

---

## 该从哪一篇开始？

- **"我只想看它跑起来长什么样"** → [🟢 入门](1-getting-started.md) 一篇就够。
- **"我要基于它写自己的程序"** → 入门后从 [① 核心骨架](2-core-setup.md) 起，按需跳章。
- **"我要画体素/大世界"** → 直接 [⑥ 体素世界](7-voxel-basics.md)（前置只依赖 [⑤ 相机与拾取](6-camera-picking.md)）。
- **"我要扩展引擎 / 加渲染效果"** → 先过 ①～④，再 [🔴 进阶](8-advanced.md)，并且**动手前务必读**开发者文档 [../developer/thread-safety.md](../developer/thread-safety.md)。
- **"卡住了 / 报错了"** → [🧰 排错](9-troubleshooting.md)。

## 这些文档不讲什么

设计原理（为什么这样分层、为何用线性管线而非 render graph、named module 的取舍）与线程安全机制的**权威说明**在开发者区：
[../developer/design.md](../developer/design.md) 与 [../developer/thread-safety.md](../developer/thread-safety.md)。
使用者文档只给"怎么做"和一个"为什么"的简述，深入原因请转过去。

> 权威用法示例永远是仓库里的 [`../../src/main.cpp`](../../src/main.cpp) 与 [`../../src/voxel_main.cpp`](../../src/voxel_main.cpp)——它们演示了引擎的全部对外用法；文档与它们冲突时以代码为准。
