# 🧰 排错：坑合集与报错速查

这一篇把**需要特别当心的坑**集中在一起，按"症状 → 根因 → 修复"组织，方便出问题时速查。前面的教程在 [🟢 入门](1-getting-started.md)与基础系列（[① 骨架](2-core-setup.md)～[⑥ 体素世界](7-voxel-basics.md)），扩展管线在 [🔴 进阶](8-advanced.md)。

目录：
- [§1 macOS / CLion：必须用 Homebrew LLVM 工具链](#1-macos--clion必须用-homebrew-llvm-工具链)
- [§2 删了 `.idea` 会退回 AppleClang](#2-删了-idea-会重新生成但可能退回-appleclang)
- [§3 黑屏且无任何报错：忘了给 UBO(uniform block) 绑定](#3-黑屏且无任何报错忘了给-ubouniform-block-绑定)
- [§4 macOS 建窗要开 forward-compat](#4-macos-建窗要开-forward-compat)
- [§5 资源投放：改 `src/assets/shaders/` 没反应](#5-资源投放改-srcassetsshaders-没反应)
- [§6 报错 → 修复速查表](#6-报错--修复速查表)

---

## §1 macOS / CLion：必须用 Homebrew LLVM 工具链

**症状**：CLion 里"此文件不属于任何项目目标"、运行配置为空；或命令行配置报
`CMake Error ... has C++ sources that may use modules, but the compiler ... cannot scan them`（见 `cmake-cxxmodules(7)`）。

**根因**：CLion 默认自动检测系统 AppleClang（`/usr/bin/c++`），它没有 `clang-scan-deps`，无法解析 named module 的依赖。而且 CLion 会用它检测到的工具链在命令行注入 `-DCMAKE_CXX_COMPILER=/usr/bin/c++`，**优先级高于 `CMakePresets.json` 里的 `cacheVariables`**，把预设钉的编译器覆盖掉。

**修复（一次性，全局）**：
1. `Settings → Build, Execution, Deployment → Toolchains → +`，命名如 `Homebrew LLVM`：
   - C compiler：`/opt/homebrew/opt/llvm/bin/clang`
   - C++ compiler：`/opt/homebrew/opt/llvm/bin/clang++`
2. `Settings → Build, Execution, Deployment → CMake → Debug` profile：把 **Toolchain 选成 `Homebrew LLVM`**（生成器 Ninja），然后 `Tools → CMake → Reset Cache and Reload Project`。
3. 想让它**删掉 `.idea` 也能自动复原**：把 `Homebrew LLVM` 排到工具链列表**第一位**（新建 profile 默认取首位）。

> 命令行用户不受此影响：`cmake --preset Debug` 或显式 `-DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++` 即可。

---

## §2 删了 `.idea` 会重新生成，但可能退回 AppleClang

`.idea/` 已被 `.gitignore` 忽略、纯本地。删掉后 CLion 会重建并重新导入 `CMakePresets.json`，但**新建 profile 会用工具链列表首位**——若首位仍是自动检测的 AppleClang，就会再次失败。做完 [§1](#1-macos--clion必须用-homebrew-llvm-工具链) 第 3 步（把 Homebrew LLVM 置首位）即可免疫。**日常不建议删 `.idea`**（它无害且已配好）。

> 另注：CLion **运行时**会回写 `workspace.xml` 等配置，要手工改这些文件必须先退出 CLion，否则会被覆盖。

---

## §3 黑屏且无任何报错：忘了给 UBO(uniform block) 绑定

GLSL 4.10 **不支持在 uniform block 上写 `layout(binding=N)`**，绑定必须在 C++ 侧显式做：

```cpp
pbr->SetBlockBinding("LightingBlock", gldx::LightBuffer::kBinding);             // binding 1
pbr->SetBlockBinding("ShadowBlock",   gldx::CascadedShadowMap::kShadowBinding); // binding 2
```

漏掉这步是经典的"场景全黑但没有 GL 错误"。程序链接后设一次即可（采样器 uniform 同理，用 `Set("uShadowMap", (int)gldx::texunit::shadowArray)` 等设一次）。用法背景见 [④ 光照与 UBO](5-lighting-ubo.md#2-灌进-ubo-并绑定黑屏第一课)。

---

## §4 macOS 建窗要开 forward-compat

Core Profile 下 macOS 需要：

```cpp
#if GLFW_PLATFORM_MACOS
glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
```

`GLFW_PLATFORM_MACOS` 来自文本 include 的 `Platform.h`。写法与原因见 [① 核心骨架 §1](2-core-setup.md#1-两行引入一个骨架)。

---

## §5 资源投放：改 `src/assets/shaders/` 没反应

`src/assets/shaders/` 里的 `.glsl/.vert/.frag` **只是内嵌 GLSL 的只读参考镜像，不被编译、不被加载**。真正的着色器在 `src/gldx/shader/*Shaders.h` 单一真源里。要改着色效果，改 `*Shaders.h`；别指望动 `shaders/` 影响运行，也别把要加载的模型丢进 `shaders/`。

- 运行期**模型/贴图**投放目录是 `src/assets/models/`。

---

## §6 报错 → 修复速查表

| 症状 | 根因 | 修复 |
| --- | --- | --- |
| `...may use modules, but the compiler...cannot scan`（`cmake-cxxmodules(7)`） | 用了 AppleClang，缺 `clang-scan-deps` | 换 Homebrew clang：`cmake --preset Debug` 或 `-DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++`（详 [§1](#1-macos--clion必须用-homebrew-llvm-工具链)） |
| CLion 无运行配置 / "此文件不属于任何项目目标" | CLion 默认工具链是 AppleClang，且注入 `-DCMAKE_CXX_COMPILER` 覆盖预设 | 见 [§1](#1-macos--clion必须用-homebrew-llvm-工具链)：Toolchains 加 Homebrew LLVM 并设为 Debug profile 工具链，Reset Cache and Reload |
| 删 `.idea` 后配置丢失 / 退回系统 clang | 新 profile 取工具链列表首位（=AppleClang） | 见 [§2](#2-删了-idea-会重新生成但可能退回-appleclang)：把 Homebrew LLVM 置首位 |
| 场景全黑、无 GL 报错 | 忘了给 uniform block 绑定（GLSL 4.10 无 `layout(binding=N)` on blocks） | 见 [§3](#3-黑屏且无任何报错忘了给-ubouniform-block-绑定)：`SetBlockBinding(...)`；采样器 `Set("uShadowMap", (int)gldx::texunit::...)` |
| macOS 建窗失败 / 上下文为空 | 未开 forward-compat | 见 [§4](#4-macos-建窗要开-forward-compat)：`glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE)`（`#if GLFW_PLATFORM_MACOS`） |
| 找不到 glad/stb/assimp 头 | 子模块未初始化 | `git submodule update --init --recursive` |
| GLAD 生成报错 | 缺 Python/jinja2 | `brew install uv` 或 `pip install jinja2` |
| 改了 `src/assets/shaders/*.glsl` 画面没变 | 那只是镜像，真源在 `*Shaders.h` | 见 [§5](#5-资源投放改-srcassetsshaders-没反应)：改 `src/gldx/shader/*Shaders.h` |
| `glad/gl.h file not found`（仅 IDE 静态分析报） | include 路径在构建期由 CMake 提供 | 忽略；以真实 `cmake --build` 为准 |
| 请求模型/贴图后马上取却是空 | 异步加载尚未完成 | 每帧 `ProcessUploads()`，就绪前 `Get*` 返回 `nullptr`，按可能为空写代码（[③ 贴图与模型加载](4-assets-loading.md)） |
| 模型加载成功但没有贴图 | 材质里的纹理引用用了绝对路径或 `..` 走出模型目录，被 `ModelLoader` 拒收 | 把贴图放模型同目录（或其子目录），材质里用相对引用；stderr 有 `[ModelLoader] ... names a path outside the model's own directory` 诊断行 |
| 模型"不见了"（无崩溃无 GL 报错） | 顶点位置含 NaN/inf：合法上传但永不光栅化；或面索引越界被整面丢弃（静默，不打诊断） | 看 stderr 的 `[ModelLoader] ... positions are not finite` 行；无该行则用建模工具重导模型（越界面丢弃是为保住"索引只指向本 mesh 顶点"的不变式） |
| 图片加载失败 | 边长超 `kMaxTextureSide`（16384），解码前读头部即被拒（防解压炸弹） | 缩小图片；拒收走 `std::expected` 错误串 `image too large: ... is WxH`，带真实尺寸与上限，由调用方自行落日志 |

---

## 还没解决？

- 编译/运行命令对不对：核对 [🟢 入门 §4](1-getting-started.md#4-构建与运行)。
- 是不是踩了 GL 线程规则（崩溃/abort 在 `AssertRenderThread`）：读 [../developer/thread-safety.md](../developer/thread-safety.md)。
- 机器可读的命令与文件地图：[../../AGENTS.md](../../AGENTS.md)。
