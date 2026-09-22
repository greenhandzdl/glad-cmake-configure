# AGENTS.md

面向 AI coding agent 的仓库速查。命令、路径、标识符保持原文以便精确检索。人类读者也可用；文档总入口见 [`doc/README.md`](doc/README.md)。

## 深度运行手册（本速查的展开版）

需要完整、可照做的步骤时读这两篇（本文件是它们的浓缩索引）：
- **替用户搭环境** → [`doc/agents/setup-environment.md`](doc/agents/setup-environment.md)：幂等的 检测→安装→配置构建→验证 流程，每步带成功判据与失败分支。
- **替用户写/改程序** → [`doc/agents/author-program.md`](doc/agents/author-program.md)：消费 `gldx` 的 CMake/代码接线、**子系统按需装配（`GeometryPass` 只硬依赖 camera/lights/pbr/scene；天空盒/阴影/IBL/泛光均为可选记录，`post` 可空、`BuildMinimalPipeline()` 可只 Geometry→DebugHud 直渲出图）**、已验证 API 速查、异步加载、交付自检。

## TL;DR 关键事实

- 项目：跨平台 **OpenGL 4.1 Core / PBR 图形引擎**（`gldx`）+ 按功能拆分的演示。仓库名 `glad-cmake-configure`：`src/main.cpp` 只留**最裸 hello-triangle**（目标 `GLFW_Template`，最小实现基线），其余一个功能一个 demo 在 `src/demo/{feature}/main.cpp`（成品演示 `pbr_showcase` PBR 场景、`voxel_terrain` 体素世界，另 ~13 个单功能 demo）。
- 语言：**C++23**；引擎以 **C++20 named module `gldx`** 交付（静态库）。
- 构建：**CMake ≥ 3.28 + Ninja**。产物落 `output/`：`GLFW_Template`（hello-triangle）+ 每个 `src/demo/{feature}` 一个可执行（`pbr_showcase`、`voxel_terrain`…共 16 个）（Win 加 `.exe`）。
- 平台：Windows / macOS / Linux。
- 依赖：GLFW（系统包）、GLAD（系统优先/子模块回退）、GLM（header-only）、STB（git 子模块内置）、Assimp（git 子模块，可选：`GLDX_ENABLE_ASSIMP` 默认 `ON`）。
- 子模块：`third_party/glad`、`third_party/stb`、`third_party/assimp`。
- 许可证：见 `LICENSE`。当前正式版 tag：`v1.4.0`。

## 硬性前提（先检查，否则必失败）

- **编译器必须支持 named modules 且带依赖扫描器**：Clang ≥ 19 / GCC ≥ 14 / 新 MSVC。
- **macOS 不能用 AppleClang**：`/usr/bin/clang` 无 `clang-scan-deps`，配置 `gldx` 模块会失败。必须用 **Homebrew LLVM**：`brew install llvm`，编译器 `/opt/homebrew/opt/llvm/bin/clang{,++}`。
- **必须先拉子模块**（`--recursive` 或 `git submodule update --init --recursive`），否则 glad/stb/assimp 缺失（用 `-DGLDX_ENABLE_ASSIMP=OFF` 时 assimp 可不拉）。

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
./output/GLFW_Template     # 最裸 hello-triangle
./output/pbr_showcase      # 任一 demo：./output/<demo>（src/demo/ 下一个目录一个）

# macOS 预设（已钉 Homebrew clang，仅 Darwin 生效）
cmake --preset Debug && cmake --build --preset Debug

# 脚本
./scripts/build.sh [debug|release]   # 配置+构建（自动选 Ninja；macOS 上自动 export Homebrew LLVM CC/CXX，已显式给 $CXX 则不覆盖）
./scripts/run.sh [target]            # 运行（默认 GLFW_Template=hello-triangle，例：run.sh pbr_showcase / run.sh shadow_csm）
./scripts/clean.sh                   # 清理（等价 cmake --build build --target clean-project）
```

## 验证一次改动是否 OK（推荐流程）

```bash
cmake --build build 2>&1 | grep -E 'src/(main|demo|gldx)' | grep -iE 'warning|error'   # 期望：无（自有代码零告警；third_party 告警不计）
./output/GLFW_Template --quit-after 3 >/tmp/o 2>/tmp/e; echo "exit=$?"; grep -v 'UNSUPPORTED (log once)' /tmp/e   # 期望：exit 0、过滤后 stderr 空
./output/pbr_showcase  --quit-after 3 >/tmp/o 2>/tmp/e; echo "exit=$?"; grep -v 'UNSUPPORTED (log once)' /tmp/e   # 换任一个受影响 demo 同样跑（无头自检靠 --quit-after，不用 kill）
```

⚠️ **退出码不是“画面正常”的证据**：空窗口和满画面都 `return 0`。要证明“真出图”，用环境变量钩子回读一帧（无需改 demo，gldxwin `App` 内置）：

```bash
GLDX_SNAPSHOT=/tmp/shot.bmp GLDX_SNAPSHOT_AT=2.0 ./output/pbr_showcase --quit-after 8   # 回读一帧 BMP 后自动关窗
sips -s format png /tmp/shot.bmp --out /tmp/shot.png                                    # 转 PNG 肉眼核对（或 python 数 nonclear 像素比例）
```

### 验证一个渲染开关是否真的还生效

`kill` 只能证明"没崩"，证明不了"画面变了"。各 demo 的每个渲染特性都能用命令行开关
（`gldx::cli::Flags`，`import gldxcli`；`--help` 看全表），配合 `--freeze-at SEC` 可脚本化取证：

```bash
./output/voxel_terrain --freeze-at 8 --auto-break 25 --pitch -1.5 --rise -1 --quit-after 20  # 基准帧
./output/voxel_terrain ... --off particles --quit-after 20                                   # 对照组
# 两次同参数运行必须逐像素全等（噪声底 = 0），否则测不出开关的贡献
```

前提：`--freeze-at` 之后 demo 状态是"时钟的纯函数"，而**不是**帧率的函数——体素碎屑走固定
120 Hz 步长、脚本节拍锁在绝对步数边界上、动画时钟用"距启动秒数"而非 `glfwGetTime()` 绝对值。
新增任何时间驱动的东西都要遵守这条，否则截图对比失效。另外：被测特性必须在默认画面里可见，
否则量到的只是 0（实例化场偏 +x、雾/天空需要远景视角，都得靠 `--yaw/--pitch/--rise` 把镜头转过去）。
还有一条同样致命的前提：脚本化验证跑起来时程序不能拥有指针。Fly 相机原本无条件 `GLFW_CURSOR_DISABLED`（现经 `Window::SetCursorCaptured(true)` 代理），
而 `OnCursor` 回调把鼠标增量写进 `yaw/pitch`——同机的人动一下鼠标，准星就离开 `--yaw/--pitch` 放的位置，
`--auto-break` 数出来的 `blocks broken` 会整组失真。`--freeze-at` 因此同时意味着不捕获指针。

### CPU 侧原语的对抗输入自检（sanitizer）

"没崩"不等于"没读越界"。`Chunk`/`ChunkMesher`/`BlockRegistry`/`Noise`/`RaycastVoxel`/`Frustum` 这些纯
CPU 入口能用带 sanitizer 的 libgfx 单跑一遍（不必建窗、不必抢焦点）：

```bash
LLVM=$(brew --prefix llvm)   # 苹果自带的 clang 没有完整 sanitizer runtime，用 Homebrew LLVM
cmake -S . -B cmake-build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=$LLVM/bin/clang -DCMAKE_CXX_COMPILER=$LLVM/bin/clang++ \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1 -fno-sanitize-recover=undefined"
```

两个坑：① 探针必须写成 `import gldx;` 的模块消费者，文本 `#include` 同一批头文件链不上
（定义在 module 实现单元里的实体带着模块附着，如 `__ZN3gfxW3gfx5NoiseC1Ej`），所以得临时挂一个
`add_executable` 目标、跑完立刻 `git checkout CMakeLists.txt`；include 目录沿用 `-I src` + glad 生成头 +
`-isystem /opt/homebrew/include`，并和 libgfx 用同一份 `-fsanitize=` flags。② 带 `-fno-sanitize-recover`
时一次只报第一个错，修一个跑一个；`-O1` 比 `-O0` 快得多且仍能报。已踩过的真 UB 都是
`static_cast<int>` 吃下了超出 int32 的 float（DDA 的 `(int)floor(NaN)`、Noise 的倍频坐标），
编译器不报错、`-ftrapv` 也抓不到，只有 UBSan 看得见。

同一把尺子也要量 demo 自己：`strtod` 接受 `inf`、`nan`、`1e300`，于是任何
`static_cast<int>(flags.number(...))` 都是同一个 UB 形状。这条链不必改 CMakeLists——按上面的 flags 配一个
构建目录、直接 build 目标 demo，再用 `--select inf --rise nan --auto-break 1e300` 这类 argv 跑一遍即可。
一个要记住的不对称：double→float 的越界转换 UBSan 并不报（加 `-fstrict-float-cast-overflow` 也不报），
所以喂给 float 的角度与距离必须先在 double 域里夹好范围再转。
畸形模型走同一条链（`ModelLoader::Load` 无 GL、线程安全，探针里直接调用即可）：判据不是"没崩"，而是"被接受
的模型中每个索引都指向该 mesh 自己发出的顶点"——assimp 的 importer 会拒绝一部分越界文件，但那条不变式该由
`ModelLoader` 自己守住。另外记住退化视口这条路是安全的（`PostProcessChain::Resize` 拒绝 0/负数尺寸、FBO 不完整时
有 stderr 诊断、下一次合法尺寸自愈），所以不必在 demo 的每帧 `Resize` 前再加判空。
数据竞争用 TSan 重配一个目录（`-fsanitize=thread`，**不能**与 address 共用一个构建），直接 build 目标 demo、
照常带 `--quit-after` 跑几十秒即可（空闲 + 挖掘压力两种节奏），不必改 CMakeLists 也不必写探针。

资产文件里的路径引用有两条解析路径，防守点不一样：依赖库自己解析的（assimp 的 glTF buffer uri）已经把引用
关在模型目录下，实测绝对路径和 `../` 都被拒；我们自己拼的（`ModelLoader` 的纹理路径）没人守，判据是
“返回给调用方的每条路径是否仍在模型目录下”。同理，图片尺寸的上限必须在解码前用 `stbi_info` 读头部比对，
解码完再拒只是把“文件→显存”的放大变成“文件→内存”的放大（实测 144 KB → 643 MB 峰值 RSS）。

驱动异步管线（`AssetManager` 这种“每帧 `ProcessUploads()` 一次”的接口）时，探针里的“帧”必须是真墙钟：
一个不带 sleep 的排空循环在 sanitizer 下能跑上万次而只耗几毫秒，worker 还没开工就“超时”，看起来像管线卡死、
其实是测法错了。每帧 `sleep_for(1ms)` 后同一份代码三帧排空。

## 文件地图

```
src/CMakeLists.txt           编排：add_subdirectory(gldx/gldxwin/gldxcli) + add_executable(GLFW_Template main.cpp)（link gldx/gldxwin/gldxcli）+ add_subdirectory(demo)
src/main.cpp                 最裸 hello-triangle：自定义 RenderPass + 内联 GLSL 直渲窗口，import gldx/gldxwin/gldxcli，由 gldx::win::App + 一个 Window（OnFrame 驱动）；目标 GLFW_Template，不碰任何可选子系统
cmake/Dependencies.cmake     GLFW find_package · GLAD（系统优先→子模块生成，含 uv venv 回退）· GLM find_path · stb INTERFACE（产出 GLAD_TARGET/GLM_INCLUDE_DIR）
cmake/Assimp.cmake           受 GLDX_ENABLE_ASSIMP 控制的 assimp 子项目块（含 macOS fdopen 修复）
src/gldx/CMakeLists.txt        add_library(gldx STATIC) + FILE_SET CXX_MODULES(gldx.cppm)；PUBLIC glfw/GLAD/src/GLM，PRIVATE stb·assimp，OFF 时定义 GLDX_NO_ASSIMP
src/gldx/gldx.cppm             模块 primary interface：export { #include } 聚合全部公共头
src/gldx/gmf.hpp              共享 global module fragment（GLAD/GLM/std 预包含；GLuint/glm::vec3 挂 global module）
src/gldx/core/                Platform.h(GLFW_PLATFORM_* 宏) · RenderContext(线程亲和) · GLBuffer · VertexArray · UniformBuffer · Sampler · Framebuffer
src/gldx/geometry/            Mesh · InstancedMesh · GeometryFactory(Cube/Sphere/Plane)
src/gldx/texture/             Texture2D · Texture2DArray · TextureCubeMap · RenderTexture
src/gldx/voxel/               BlockRegistry · Chunk · ChunkMesher（纯 CPU 网格化）· VoxelMeshGpu
src/gldx/util/                Noise（Perlin 2D/3D + fBm，seeded、无全局状态）
src/gldx/material/            PbrMaterial
src/gldx/shader/              ShaderProgram + 内嵌 GLSL（ShaderLib.h / PostProcessShaders.h / IblShaders.h，单一真源）
src/gldx/camera|light|shadow/ Camera(orbit+飞行)·Frustum·Picking·VoxelRay(DDA) / LightBuffer(UBO) / CascadedShadowMap·EnvironmentMap
src/gldx/scene/               Scene · SceneNode · Transform（纯 CPU 层级）
src/gldx/render/              Renderer · RenderPass · RenderFrame · RenderPasses(含 Voxel 两 pass) · PostProcessChain · SpriteBatch · TextRenderer · ParticleBatch
src/gldx/text|assets|debug/   Font / AssetManager·ThreadPool·ModelLoader·ImageLoader / DebugDraw·Profiler
src/gldx/third_party/         stb_image_impl.cpp（唯一第三方实现 TU，非模块接口）
src/gldxwin/gldxwin.cppm      引擎无关窗口库 primary interface（namespace gldx::win）：App 单例（glfwInit/Terminate + GL 4.1 core hints + 多窗口帧循环 Run + `Now()`）、Window（建窗+MakeContextCurrent+gladLoadGL，OnCreate/OnFrame/OnDestroy 钩子 + SetCloseOnEsc + UserData 槽 `SetUserData/GetUserData/UserDataAs<T>` + 完整输入面：事件 `OnKey/OnChar/OnMouseButton/OnCursor/OnScroll`、轮询 `KeyIsDown/MouseIsDown/CursorPos/SetCursorPos/SetCursorVisible/SetCursorCaptured/SetRawMouseInput`、几何与上下文 `Pos/SetPos/Size/FramebufferSize/Focus/ContextIsCurrent/CaptureScreenshot`）、WindowDesc/FrameInfo/RunOptions + portable 词汇 `Key/KeyAction/MouseButton/Vec2d`（不露 GLFW 类型）。**GLFW 窗口 user-pointer 被 gldxwin 独占**（构造时无条件装 5 个 trampoline 指回 `this`），demo 严禁再 `glfwSetWindowUserPointer`/`glfwSet*Callback`——改用上面的输入面或回调 lambda 捕获；`Handle()` 是给未代理的窗口系统能力（raw mouse/clipboard/joystick/file drop）留的逃生舱，日常 demo 应保持不调用。仅链 glfw+GLAD，import 不到任何 gldx 类型
src/gldxwin/App.cpp|Window.cpp  App::Run 帧循环实现 + Window 生命周期；gmf.hpp 挂 glad/glfw 到 global module
src/gldxcli/gldxcli.cppm      CLI 开关库 primary interface（namespace gldx::cli::Flags）：--off/--on/--quit-after/--help + number/integer/real/string（域夹范围）；纯标准库、零链接依赖（原 header-only demo_cli.h 迁入）
src/demo/pbr_showcase/       成品 PBR 场景演示（原 src/main.cpp）：HDR/PBR/CSM/IBL/Bloom/天空盒/实例化/HUD，import 三库 + 手写生命周期迁到 App/Window 钩子；拆为 `main.cpp`（装配）+ `Input.{h,cpp}`（回调）+ `Scene.{h,cpp}`（`ShowcaseScene`/`BuildInstancedField`）
src/demo/voxel_terrain/      体素演示入口（原 src/voxel_main.cpp）：chunk 流式生成/网格化 + 方块编辑（保留本地时钟保帧序，仅换窗口生命周期，输入全走 gldxwin 代理不再直调 glfw）；拆为 `main.cpp`（装配+帧循环）+ `World.{h,cpp}`（常量/`World`/`WaterSim`/`GenerateChunk`）+ `Streaming.{h,cpp}`（`ChunkStreamer`：线程池+生成/网格化双队列）+ `Input.{h,cpp}` + `Hud.{h,cpp}`（`VoxelHudPass`）
src/demo/{feature}/          单功能入门 demo：import gldx/gldxwin/gldxcli，用 gldx::win::App+Window 钩子（OnCreate 建 Renderer/MarkAsRenderThread，OnFrame 填 RenderFrame+Render）+ gldx::cli::Flags；只装配它演示的那个子系统；新增一个目录免改 CMake
src/demo/CMakeLists.txt      add_gldx_demo(<name>)：d_<name>→OUTPUT_NAME=<name>→link gldx/gldxwin/gldxcli；GLOB(CONFIGURE_DEPENDS) 遍历含 main.cpp 的子目录
src/assets/                  运行期内容（不编译）：models/ 投放目录 · shaders/ 只读参考镜像（不加载）
third_party/                 glad · stb · assimp（子模块）
scripts/                     build.sh.in / run.sh.in / clean.sh.in（CMake 配置期生成 .sh）
.github/workflows/           release.yml（手动触发的三平台构建+发布）
CMakeLists.txt（根，~60 行 orchestrator：project/标准/option → include(cmake/) → add_subdirectory(src) → 脚本生成 + Summary） · CMakePresets.json
doc/                         README(入口) · user/(入门 + API 分章: 骨架/几何场景/资源加载/光照UBO/相机拾取/体素 + 进阶 + 排错) · developer/(design + thread-safety) · agents/(setup-environment + author-program)
AGENTS.md                    本文件（agent 速查，留在仓库根便于自动发现）
```

## 硬约束 / 不变量（改代码前必读）

1. **所有 GL 调用（含创建与删除）只在渲染线程**。`RenderContext::MarkAsRenderThread()` 由 demo 在 `Window::OnCreate` 首行调一次（`gldxwin` 故意引擎无关，不代劳）；每个拥有/驱动 GL 的类在公有入口与析构里调 `AssertRenderThread(...)`（错线程即 `abort`）。GL 资源在 `OnCreate` 建、`OnDestroy` 拆（后者在 `glfwDestroyWindow` 前、上下文仍当前时回调）。
2. **两阶段资源管线**：Stage A（工作线程，仅 CPU 解码，产出 `LoadedModelData`/`Texture2DDesc`）→ Stage B（渲染线程 `AssetManager::ProcessUploads()` 才建 GL 对象）。工作线程里绝不 `gl*`。
3. **move-only RAII**：GL 包装与 `ThreadPool`/`AssetManager`/`RenderPass` 等 delete 拷贝，只能 `std::move`/`unique_ptr`。一个 GL id 只有一个 owner。
4. **错误用 `std::expected<T,E>`，不跨线程抛异常**。（`ThreadPool::WorkerMain` 顶层有 `try/catch` 兜底，防逃逸出 `std::thread` 触发 `std::terminate`。）
5. **场景节点持非拥有指针**：`SceneNode` 存 `const Mesh*`/`const PbrMaterial*`，GL 对象生命周期归应用（demo 的 `app` 回调，在其中构造并在 `glfwTerminate` 前析构）。
6. **依赖只向下**：application→orchestration(render/scene)→features→core。高层可用低层，反之不行。
7. **named module 边界**：宏与 `<GLFW/glfw3.h>` 不跨模块 → `Platform.h` 必须文本 include；global module 类型（`glm::vec3`/`GLuint`）消费者自行 include。

## 常见任务 how-to

- **加一个渲染 pass**：继承 `gldx::RenderPass`，`void Execute(gldx::RenderFrame&) override`，`renderer.AddPass(std::make_unique<...>())`。GL 工作在渲染线程执行（`Renderer::Render` 保证）。要“一个 for 循环装配多个不同子类”参考 `src/demo/render_passes/`（`Passes.{h,cpp}` 定义 `ShapePass` 基类 + Triangle/Quad/Line/Point 子类 + `ShapeSpec` 工厂表，`View::Create` 遍历表逐行 `AddPass`；多窗各自 context-private Renderer 共设一个 `SharedState` 原子时钟）。
- **加几何**：`gldx::GeometryFactory::Cube/Sphere/Plane` 或自填 `gldx::MeshData` → `Mesh::Upload(std::move(data))`（渲染线程）。
- **画东西（OpenGL 封装边界）**：句柄对象的操作走 RAII 包装（`VertexArray`/`GLBuffer`/`Texture`…，别裸调 `glGen/Bind/Delete`）。绘制原语收在 `VertexArray`：绑定后用 `vao.DrawArrays(mode,first,count)` / `vao.DrawElements(...)` / `vao.DrawElementsInstanced(...)`，**不要**在 demo/pass 里直接 `glDraw*`（全仓 `glDraw*` 只应出现在 `VertexArray.cpp`）。但 `glViewport`/`glBindFramebuffer`/`glClear` 这些**帧级/pass 级**操作**刻意保持裸调**（属本 pass 自身、非某对象，封进去就是透传包装反模式），别去“补封装”。理由见 `doc/developer/design.md` §9。
- **加载模型/贴图**：`AssetManager::RequestModel/RequestTexture(key, path)`（key 由调用方命名，重复 key 被忽略），每帧 `ProcessUploads()`，就绪前 `Get*` 返回 `nullptr`。纹理引用必须落在模型目录内、图片边长≤`kMaxTextureSide`（16384，解码前校验），两条越界都会被拒。
- **改 GLSL**：改 `src/gldx/shader/*Shaders.h`（单一真源），**不要**改 `src/assets/shaders/`（那只是镜像）。要 demo/用户从外部 `.glsl` 热加载用 opt-in 的 `ShaderProgram::CreateFromFiles(vertPath, fragPath)`（仅读入后转发 `CreateFromSource`；不破坏内嵌单一真源，普通构建仍零运行期路径依赖）。
- **装配额外着色阶段（几何/细分）**：`CreateFromSource`/`CreateFromFiles` 只处理 vertex+fragment 这对经典组合；要挂几何、细分控制/求值等着色阶段，用泛型入口 `ShaderProgram::CreateFromSources({{ShaderStage::Vertex, src}, {ShaderStage::Geometry, src}, {ShaderStage::Fragment, src}})`（按需列阶段，顺序无关、每阶段至多一次），文件版同理 `CreateFromFiles({...ShaderFile...})`。`ShaderStage` 底层就是 GL 枚举。**4.1 图形管线至少含 vertex+fragment**，否则返回 `std::unexpected`。**没有 `Compute`**——那是 OpenGL 4.3+、超出 4.1 core 基线（macOS 上限），GLAD 4.1 loader 不定义 `GL_COMPUTE_SHADER`；要抬基线另说。参考 `src/demo/geometry_shader/`（喂 `GL_POINTS`、几何阶段扩成方块的端到端示例）；要“一次集成全 5 个图形阶段”参考 `src/demo/shader_stages/`（`CreateFromSources({Vertex,TessControl,TessEvaluation,Geometry,Fragment})` 装配完整管线、画 `GL_PATCHES` 细分线框；细分是 4.0+、几何是 3.2+，均在 4.1 core 内；macOS 下细分+几何共用会触发“SW vertex processing”驱动提示，属良性非错误）。
- **UBO 绑定**：`ShaderProgram::SetBlockBinding("LightingBlock", gldx::LightBuffer::kBinding)` 等，链接后设一次。
- **demo 里处理输入/截图**：只用 `gldx::win::Window` 的输入面——订阅 `OnKey/OnChar/OnMouseButton/OnCursor/OnScroll` 或在 lambda 里捕获状态，轮询用 `KeyIsDown/MouseIsDown/CursorPos`，指针锁定用 `SetCursorCaptured`，截图用 `Window::CaptureScreenshot(path)`；**不要** `glfwSet*Callback`/`glfwSetWindowUserPointer`/`glfwGetKey`/`glfwGetTime`（后者用 `App::Now()`）——user-pointer 归 gldxwin 独占，直调会踩坏回调分发。只有窗口系统专属能力（raw mouse/clipboard/joystick/file drop）才落回 `Handle()` 逃生舱。

## 报错 → 修复

| 症状 | 根因 | 修复 |
| --- | --- | --- |
| `...may use modules, but the compiler...cannot scan`（`cmake-cxxmodules(7)`） | 用了 AppleClang，缺 `clang-scan-deps` | 换 Homebrew clang：`cmake --preset Debug` 或 `-DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++` |
| CLion 无运行配置 / "此文件不属于任何项目目标" | CLion 默认工具链是 AppleClang，且注入 `-DCMAKE_CXX_COMPILER` 覆盖预设 | Toolchains 加 Homebrew LLVM 并设为 Debug profile 工具链，Reset Cache and Reload |
| 场景全黑、无 GL 报错 | 忘了给 uniform block 绑定（GLSL 4.10 无 `layout(binding=N)` on blocks） | `SetBlockBinding(...)`；采样器 `Set("uShadowMap", (int)gldx::texunit::...)` |
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
