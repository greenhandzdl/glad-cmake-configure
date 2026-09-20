# AGENTS.md

面向 AI coding agent 的仓库速查。命令、路径、标识符保持原文以便精确检索。人类读者也可用；文档总入口见 [`doc/README.md`](doc/README.md)。

## 深度运行手册（本速查的展开版）

需要完整、可照做的步骤时读这两篇（本文件是它们的浓缩索引）：
- **替用户搭环境** → [`doc/agents/setup-environment.md`](doc/agents/setup-environment.md)：幂等的 检测→安装→配置构建→验证 流程，每步带成功判据与失败分支。
- **替用户写/改程序** → [`doc/agents/author-program.md`](doc/agents/author-program.md)：消费 `gfx` 的 CMake/代码接线、**默认管线是全家桶（GeometryPass 硬依赖多个字段，别写极简 RenderFrame）**、已验证 API 速查、异步加载、交付自检。

## TL;DR 关键事实

- 项目：跨平台 **OpenGL 4.1 Core / PBR 图形引擎**（`gfx`）+ 交互式演示 `src/main.cpp`。仓库名 `glad-cmake-configure`，可执行目标 `GLFW_Template`（PBR 演示）与 `voxel_demo`（体素演示）。
- 语言：**C++23**；引擎以 **C++20 named module `gfx`** 交付（静态库）。
- 构建：**CMake ≥ 3.28 + Ninja**。产物固定 `output/GLFW_Template` 与 `output/voxel_demo`（Win 加 `.exe`）。
- 平台：Windows / macOS / Linux。
- 依赖：GLFW（系统包）、GLAD（系统优先/子模块回退）、GLM（header-only）、STB + Assimp（git 子模块内置）。
- 子模块：`third_party/glad`、`third_party/stb`、`third_party/assimp`。
- 许可证：见 `LICENSE`。当前正式版 tag：`v1.3.0`。

## 硬性前提（先检查，否则必失败）

- **编译器必须支持 named modules 且带依赖扫描器**：Clang ≥ 19 / GCC ≥ 14 / 新 MSVC。
- **macOS 不能用 AppleClang**：`/usr/bin/clang` 无 `clang-scan-deps`，配置 `gfx` 模块会失败。必须用 **Homebrew LLVM**：`brew install llvm`，编译器 `/opt/homebrew/opt/llvm/bin/clang{,++}`。
- **必须先拉子模块**（`--recursive` 或 `git submodule update --init --recursive`），否则 glad/stb/assimp 缺失。

## 安装

```bash
git clone --recursive https://github.com/greenhandzdl/glad-cmake-configure.git
cd glad-cmake-configure
```

- macOS: `brew install glfw glm cmake ninja llvm`（`uv` 或 `pip install jinja2` 供 GLAD 回退分支）
- Ubuntu/Debian: `sudo apt-get install libglfw3-dev libglm-dev cmake build-essential ninja-build python3-jinja2`
- Fedora: `sudo dnf install glfw-devel glm-devel cmake gcc-c++ ninja python3-jinja2`
- Windows: `vcpkg install glfw3 glm` + `pip install jinja2`（MSVC + Ninja）

## 构建 / 运行

```bash
# 通用（跨平台）
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./output/GLFW_Template
./output/voxel_demo

# macOS 预设（已钉 Homebrew clang，仅 Darwin 生效）
cmake --preset Debug && cmake --build --preset Debug

# 脚本
./scripts/build.sh [debug|release]   # 配置+构建（自动选 Ninja；macOS 上自动 export Homebrew LLVM CC/CXX，已显式给 $CXX 则不覆盖）
./scripts/run.sh [target]            # 运行（默认 GLFW_Template，例：run.sh voxel_demo）
./scripts/clean.sh                   # 清理（等价 cmake --build build --target clean-project）
```

## 验证一次改动是否 OK（推荐流程）

```bash
cmake --build build 2>&1 | grep -E 'src/(main|gfx)' | grep -iE 'warning|error'   # 期望：无（自有代码零告警）
./output/GLFW_Template >/tmp/o 2>/tmp/e & p=$!; sleep 6; kill $p 2>/dev/null; wc -c /tmp/e   # 期望：stderr 0 字节
./output/voxel_demo    >/tmp/o 2>/tmp/e & p=$!; sleep 6; kill $p 2>/dev/null; wc -c /tmp/e   # 同上（改了体素侧就跑这条）
```

### 验证一个渲染开关是否真的还生效

`kill` 只能证明"没崩"，证明不了"画面变了"。两个 demo 的每个渲染特性都能用命令行开关
（`src/demo_cli.h`，`--help` 看全表），配合 `--freeze-at SEC` 可脚本化取证：

```bash
./output/voxel_demo --freeze-at 8 --auto-break 25 --pitch -1.5 --rise -1 --quit-after 20  # 基准帧
./output/voxel_demo ... --off particles --quit-after 20                                   # 对照组
# 两次同参数运行必须逐像素全等（噪声底 = 0），否则测不出开关的贡献
```

前提：`--freeze-at` 之后 demo 状态是"时钟的纯函数"，而**不是**帧率的函数——体素碎屑走固定
120 Hz 步长、脚本节拍锁在绝对步数边界上、动画时钟用"距启动秒数"而非 `glfwGetTime()` 绝对值。
新增任何时间驱动的东西都要遵守这条，否则截图对比失效。另外：被测特性必须在默认画面里可见，
否则量到的只是 0（实例化场偏 +x、雾/天空需要远景视角，都得靠 `--yaw/--pitch/--rise` 把镜头转过去）。

### CPU 侧原语的对抗输入自检（sanitizer）

"没崩"不等于"没读越界"。`Chunk`/`ChunkMesher`/`BlockRegistry`/`Noise`/`RaycastVoxel`/`Frustum` 这些纯
CPU 入口能用带 sanitizer 的 libgfx 单跑一遍（不必建窗、不必抢焦点）：

```bash
LLVM=$(brew --prefix llvm)   # 苹果自带的 clang 没有完整 sanitizer runtime，用 Homebrew LLVM
cmake -S . -B cmake-build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=$LLVM/bin/clang -DCMAKE_CXX_COMPILER=$LLVM/bin/clang++ \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1 -fno-sanitize-recover=undefined"
```

两个坑：① 探针必须写成 `import gfx;` 的模块消费者，文本 `#include` 同一批头文件链不上
（定义在 module 实现单元里的实体带着模块附着，如 `__ZN3gfxW3gfx5NoiseC1Ej`），所以得临时挂一个
`add_executable` 目标、跑完立刻 `git checkout CMakeLists.txt`；include 目录沿用 `-I src` + glad 生成头 +
`-isystem /opt/homebrew/include`，并和 libgfx 用同一份 `-fsanitize=` flags。② 带 `-fno-sanitize-recover`
时一次只报第一个错，修一个跑一个；`-O1` 比 `-O0` 快得多且仍能报。已踩过的真 UB 都是
`static_cast<int>` 吃下了超出 int32 的 float（DDA 的 `(int)floor(NaN)`、Noise 的倍频坐标），
编译器不报错、`-ftrapv` 也抓不到，只有 UBSan 看得见。

## 文件地图

```
src/main.cpp                 PBR 演示入口：#include "gfx/core/Platform.h" + import gfx; 建窗/组场景/每帧 renderer.Render(frame)
src/gfx/gfx.cppm             模块 primary interface：export { #include } 聚合全部公共头
src/gfx/gmf.hpp              共享 global module fragment（GLAD/GLM/std 预包含；GLuint/glm::vec3 挂 global module）
src/gfx/core/                Platform.h(窗口/GLFW/宏/常量) · RenderContext(线程亲和) · GLBuffer · VertexArray · UniformBuffer · Sampler · Framebuffer
src/gfx/geometry/            Mesh · InstancedMesh · GeometryFactory(Cube/Sphere/Plane)
src/gfx/texture/             Texture2D · Texture2DArray · TextureCubeMap · RenderTexture
src/gfx/voxel/               BlockRegistry · Chunk · ChunkMesher（纯 CPU 网格化）· VoxelMeshGpu
src/gfx/util/                Noise（Perlin 2D/3D + fBm，seeded、无全局状态）
src/gfx/material/            PbrMaterial
src/gfx/shader/              ShaderProgram + 内嵌 GLSL（ShaderLib.h / PostProcessShaders.h / IblShaders.h，单一真源）
src/gfx/camera|light|shadow/ Camera(orbit+飞行)·Frustum·Picking·VoxelRay(DDA) / LightBuffer(UBO) / CascadedShadowMap·EnvironmentMap
src/gfx/scene/               Scene · SceneNode · Transform（纯 CPU 层级）
src/gfx/render/              Renderer · RenderPass · RenderFrame · RenderPasses(含 Voxel 两 pass) · PostProcessChain · SpriteBatch · TextRenderer · ParticleBatch
src/gfx/text|assets|debug/   Font / AssetManager·ThreadPool·ModelLoader·ImageLoader / DebugDraw·Profiler
src/gfx/third_party/         stb_image_impl.cpp（唯一第三方实现 TU，非模块接口）
src/voxel_main.cpp           体素演示入口：chunk 流式生成/网格化（ThreadPool worker + 渲染线程上传）+ 方块编辑
src/demo_cli.h               两个 demo 共用的命令行开关（--off/--on/--quit-after/--help + 数值选项）；header-only，不进 module gfx
src/assets/                  运行期内容（不编译）：models/ 投放目录 · shaders/ 只读参考镜像（不加载）
third_party/                 glad · stb · assimp（子模块）
scripts/                     build.sh.in / run.sh.in / clean.sh.in（CMake 配置期生成 .sh）
.github/workflows/           release.yml（手动触发的三平台构建+发布）
CMakeLists.txt · CMakePresets.json
doc/                         README(入口) · user/(使用者:入门/基础/进阶/排错) · developer/(design + thread-safety) · agents/(setup-environment + author-program)
AGENTS.md                    本文件（agent 速查，留在仓库根便于自动发现）
```

## 硬约束 / 不变量（改代码前必读）

1. **所有 GL 调用（含创建与删除）只在渲染线程**。`RenderContext::MarkAsRenderThread()` 在 `main` 里调一次；每个拥有/驱动 GL 的类在公有入口与析构里调 `AssertRenderThread(...)`（错线程即 `abort`）。
2. **两阶段资源管线**：Stage A（工作线程，仅 CPU 解码，产出 `LoadedModelData`/`Texture2DDesc`）→ Stage B（渲染线程 `AssetManager::ProcessUploads()` 才建 GL 对象）。工作线程里绝不 `gl*`。
3. **move-only RAII**：GL 包装与 `ThreadPool`/`AssetManager`/`RenderPass` 等 delete 拷贝，只能 `std::move`/`unique_ptr`。一个 GL id 只有一个 owner。
4. **错误用 `std::expected<T,E>`，不跨线程抛异常**。（`ThreadPool::WorkerMain` 顶层有 `try/catch` 兜底，防逃逸出 `std::thread` 触发 `std::terminate`。）
5. **场景节点持非拥有指针**：`SceneNode` 存 `const Mesh*`/`const PbrMaterial*`，GL 对象生命周期归应用（`main.cpp`）。
6. **依赖只向下**：application→orchestration(render/scene)→features→core。高层可用低层，反之不行。
7. **named module 边界**：宏与 `<GLFW/glfw3.h>` 不跨模块 → `Platform.h` 必须文本 include；global module 类型（`glm::vec3`/`GLuint`）消费者自行 include。

## 常见任务 how-to

- **加一个渲染 pass**：继承 `gfx::RenderPass`，`void Execute(gfx::RenderFrame&) override`，`renderer.AddPass(std::make_unique<...>())`。GL 工作在渲染线程执行（`Renderer::Render` 保证）。
- **加几何**：`gfx::GeometryFactory::Cube/Sphere/Plane` 或自填 `gfx::MeshData` → `Mesh::Upload(std::move(data))`（渲染线程）。
- **加载模型/贴图**：`AssetManager::RequestModel/RequestTexture(path)`，每帧 `ProcessUploads()`，就绪前 `Find*` 返回 `nullptr`。
- **改 GLSL**：改 `src/gfx/shader/*Shaders.h`（单一真源），**不要**改 `src/assets/shaders/`（那只是镜像）。
- **UBO 绑定**：`ShaderProgram::SetBlockBinding("LightingBlock", gfx::LightBuffer::kBinding)` 等，链接后设一次。

## 报错 → 修复

| 症状 | 根因 | 修复 |
| --- | --- | --- |
| `...may use modules, but the compiler...cannot scan`（`cmake-cxxmodules(7)`） | 用了 AppleClang，缺 `clang-scan-deps` | 换 Homebrew clang：`cmake --preset Debug` 或 `-DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++` |
| CLion 无运行配置 / "此文件不属于任何项目目标" | CLion 默认工具链是 AppleClang，且注入 `-DCMAKE_CXX_COMPILER` 覆盖预设 | Toolchains 加 Homebrew LLVM 并设为 Debug profile 工具链，Reset Cache and Reload |
| 场景全黑、无 GL 报错 | 忘了给 uniform block 绑定（GLSL 4.10 无 `layout(binding=N)` on blocks） | `SetBlockBinding(...)`；采样器 `Set("uShadowMap", (int)gfx::texunit::...)` |
| macOS 建窗失败 / 上下文为空 | 未开 forward-compat | `glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE)`（`#if GLFW_PLATFORM_MACOS`） |
| 找不到 glad/stb/assimp 头 | 子模块未初始化 | `git submodule update --init --recursive` |
| GLAD 生成报错 | 缺 Python/jinja2 | `brew install uv` 或 `pip install jinja2` |
| `glad/gl.h file not found`（仅 IDE 静态分析报） | include 路径在构建期由 CMake 提供 | 忽略；以真实 `cmake --build` 为准 |
| 粒子/点精灵画成一个像素的方点 | macOS 默认不开 `GL_PROGRAM_POINT_SIZE`，着色器的 `gl_PointSize` 被驱动忽略 | 画点前 `glEnable` 该 cap 并在退出时恢复（见 `ParticleBatch::Draw`） |
| 截图对比量到 0 差异，但开关确实改了状态 | 被测内容不在默认视野内（实例化场、天空、雾），或时间驱动内容随帧率漂移 | 用 `--yaw/--pitch/--rise` 把镜头转过去；把模拟挂到固定步长 + `--freeze-at` |

## CI / 发布

- 工作流：`.github/workflows/release.yml`，**仅手动触发**（`gh workflow run "Build & Release" -f build_type=Release [-f draft=true]`）。
- 矩阵：Linux / macOS / Windows，统一 Ninja，产物 `GLFW_Template-{linux-x64,macos-arm64,windows-x64}.zip`。
- 输入：`build_type` / `release_tag`（空则 `ci-<日期>-<sha>`）/ `prerelease` / `draft`。
- CI **不使用 `--preset`**（用显式 `cmake -S/-B`），故 `CMakePresets.json` 的 Darwin 条件对 Linux/Windows 无影响。

## 约定

- 提交/推送遵循仓库既有风格（`type: 摘要` + 正文）。**不要**把 `build/`、`cmake-build-*/`、`output/`、`.idea/` 入库（已 gitignore）。
- 自有 `src/**` 代码保持零告警（Homebrew clang 23 基线）；显式 include，不依赖传递包含。
- 触碰会拥有/调用 GL 的代码前，先读 [`doc/developer/thread-safety.md`](doc/developer/thread-safety.md)。
