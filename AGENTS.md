# AGENTS.md

面向 AI coding agent 的仓库速查。命令、路径、标识符保持原文以便精确检索。人类读者也可用；文档总入口见 [`doc/README.md`](doc/README.md)。

## 深度运行手册（本速查的展开版）

需要完整、可照做的步骤时读这两篇（本文件是它们的浓缩索引）：
- **替用户搭环境** → [`doc/agents/setup-environment.md`](doc/agents/setup-environment.md)：幂等的 检测→安装→配置构建→验证 流程，每步带成功判据与失败分支。
- **替用户写/改程序** → [`doc/agents/author-program.md`](doc/agents/author-program.md)：消费 `gldx` 的 CMake/代码接线、**子系统按需装配（`GeometryPass` 只硬依赖 camera/lights/pbr/scene；天空盒/阴影/IBL/泛光均为可选记录，`post` 可空、`BuildMinimalPipeline()` 可只 Geometry→DebugHud 直渲出图）**、已验证 API 速查、异步加载、交付自检。

## TL;DR 关键事实

- 项目：跨平台 **OpenGL 4.1 Core / PBR 图形引擎**（`gldx`）+ 按功能拆分的演示。仓库名 `glad-cmake-configure`：`src/main.cpp` 只留**最简 hello-triangle**（目标 `GLFW_Template`，最小实现基线，走高层 `Mesh`），其余一个功能一个 demo 在 `src/demo/{feature}/main.cpp`（成品演示 `pbr_showcase` PBR 场景、`voxel_terrain` 体素世界、`hello_triangle` 手搭底层对照、`menger_sponge` 着色阶段生成分形（细分层级是 uniform，另一臂由 GPU 自行细分）、`textured_sphere` 贴图球体（同一份 `MeshData` 既能 CPU 重网格也能走 TCS/TES 硬件细分）、`glass_water` 自由飞行透明场景（五层状态机，四道透明闸门各自可关，透明度是电介质 Cook-Torrance 且 `Tab` 切透视/正交），另 ~20 个单功能 demo）。
- 语言：**C++23**；引擎以 **C++20 named module `gldx`** 交付（静态库）。
- 构建：**CMake ≥ 3.28 + Ninja**。产物落 `output/`：`GLFW_Template`（hello-triangle，Mesh 高层路径）+ 每个 `src/demo/{feature}` 一个可执行（`pbr_showcase`、`voxel_terrain`…共 26 个 demo，加 `GLFW_Template` 合计 27 个可执行）（Win 加 `.exe`）。
- 平台：Windows / macOS / Linux。
- 依赖：GLFW（系统包）、GLAD（系统优先/子模块回退）、GLM（header-only）、STB（git 子模块内置）、Assimp（git 子模块，可选：`GLDX_ENABLE_ASSIMP` 默认 `ON`）。
- 子模块：`third_party/glad`、`third_party/stb`、`third_party/assimp`。
- 许可证：见 `LICENSE`。当前正式版 tag：`v1.5.0`。

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
./output/GLFW_Template     # 最简 hello-triangle（Mesh 高层路径）
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
新增任何时间驱动的东西都要遵守这条，否则截图对比失效。另有三条同等致命、且容易只做到一半：

- **`--freeze-at SEC` 必须不晚于 `--snap-at`**。时钟停在 freeze 时刻，那么 freeze **之后**拍的快照仍在动的东西（`glass_water` 的海面）就会让两次同参数运行差出几千像素；先拿这个往回查，不要先怀疑渲染开关本身没生效。
- **对比时必须 `--off hud`**。HUD 里通常有实时 fps / CPU ms 数字，留着它等于每次都比一遍文字，噪声底永远不为 0（`glass_water` 的 HUD 还打印提交顺序，那部分是有意的证据，但只在肉眼读图时开）。两条都满足后 `glass_water --off hud --freeze-at 2 --snap-at 2.5` 两次运行 `cmp` 全等，像素差才敢当测量用。
- **指针未捕获时，整个输入层都不许驱动仿真**（不只是鼠标）。`--snapshot` 运行不捕获指针，但窗口仍然是 frontmost：键盘事件照样进来，而滚轮回调是无条件注册的。一条杂散按键（或桌面上一段滚动）就能把相机开走，实测差出 36% 像素，看起来像“渲染器不确定”而不是“输入没闸门”。约定：`HandleKeys`/`ReadFlyIntent`/`MouseCallback`/`ScrollCallback` 先查 `captured`，只留释放指针与退出那几个键（`F`/`Esc`/`X`）活着。

被测特性必须在默认画面里可见，
否则量到的只是 0（实例化场偏 +x、雾/天空需要远景视角，都得靠 `--yaw/--pitch/--rise` 把镜头转过去）；`glass_water` 这里还多一层：“被吃掉的像素”必须是一个**在正确帧里清清楚楚可见**的透明物体，所以水下那只箱子专门放在开阔水面下而不是玻璃板后面——被逐层衰减到看不见的伪证什么都证明不了，而同框的不透明桥墩是对照组（它先画、 blend 层的任何错误都吃不掉它）。但**别把“哪个开关在起作用”当成“哪个开关单独就能复现”**：正确排序下箱子先于盖它的水面 tile 提交，先画的东西不会被后来的深度值拒掉，所以 `--on depthwrite` 单独打开时先消失的是最后画的那两盏 additive 灯（Glow 层不写深度，但深度测试一直接着），“箱子整块没了”需要 `--off sort` 与 `--on depthwrite` 同时打开。报告像素差时要把“是哪几个开关的组合”写进同一行，否则下一个人复现不出来。
还有一条前提同样常被忘掉：脚本化验证跑起来时程序不能拥有指针。Fly 相机原本无条件 `GLFW_CURSOR_DISABLED`（现经 `Window::SetCursorCaptured(true)` 代理），
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
src/main.cpp                 最简 hello-triangle：自定义 RenderPass + 内联 GLSL + 高层 gldx::Mesh（MeshData→Upload→Draw，不命名 VAO/VBO/属性布局）直渲窗口，import gldx/gldxwin/gldxcli，由 gldx::win::App + 一个 Window（OnFrame 驱动）；目标 GLFW_Template，不碰任何可选子系统。底层手搭对照版在 src/demo/hello_triangle/
cmake/Dependencies.cmake     GLFW find_package · GLAD（系统优先→子模块生成，含 uv venv 回退）· GLM find_path · stb INTERFACE（产出 GLAD_TARGET/GLM_INCLUDE_DIR）
cmake/Assimp.cmake           受 GLDX_ENABLE_ASSIMP 控制的 assimp 子项目块（含 macOS fdopen 修复）
src/gldx/CMakeLists.txt        add_library(gldx STATIC) + FILE_SET CXX_MODULES(gldx.cppm)；PUBLIC glfw/GLAD/src/GLM，PRIVATE stb·assimp，OFF 时定义 GLDX_NO_ASSIMP
src/gldx/gldx.cppm             模块 primary interface：export { #include } 聚合全部公共头
src/gldx/gmf.hpp              共享 global module fragment（GLAD/GLM/std 预包含；GLuint/glm::vec3 挂 global module）
src/gldx/core/                Platform.h(GLFW_PLATFORM_* 宏) · RenderContext(线程亲和) · GLBuffer · VertexArray(含 `DrawTransformFeedback`) · TransformFeedback(TF 捕获会话 + 非阻塞计数回读) · UniformBuffer · Sampler · Framebuffer
src/gldx/geometry/            Mesh（含 `DrawAs(GLenum)`，只为 `GL_PATCHES` 而存在）· InstancedMesh · GeometryFactory(Cube/Quad/Sphere/Plane；统一绕序约定：从顶点法向那侧看为 CCW，反了不会报错，只会被剔成背面或露出内表面)
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
src/demo/hello_triangle/     基线对照 demo（原 src/main.cpp 的手搭版）：VertexArray+GLBuffer+AttachAttribute 画同一个三角形，展示 Mesh::Upload 背后发生了什么；窗口标题标「manual VAO/VBO」
src/demo/voxel_terrain/      体素演示入口（原 src/voxel_main.cpp）：chunk 流式生成/网格化 + 方块编辑（保留本地时钟保帧序，仅换窗口生命周期，输入全走 gldxwin 代理不再直调 glfw）；拆为 `main.cpp`（装配+帧循环）+ `World.{h,cpp}`（常量/`World`/`WaterSim`/`GenerateChunk`）+ `Streaming.{h,cpp}`（`ChunkStreamer`：线程池+生成/网格化双队列）+ `Input.{h,cpp}` + `Hud.{h,cpp}`（`VoxelHudPass`）
src/demo/menger_sponge/      着色阶段生成几何：`main.cpp`（两条臂装配 + `RunQueue` pass 链）+ `MengerShaders.h`（内嵌 GLSL）。穷举臂 = `gl_VertexID` 的 20 进制解码（`uniform int uLevel` 即循环上界，零顶点缓冲/零属性/零 Mesh）；默认臂 = transform feedback 自细分队列（CPU 只上传 1 个种子立方体，每 pass 当场画掉已完成的、只 capture 还要细分的子块，绘制数取 `PRIMITIVES_GENERATED`）。CLI `--level/--size/--distance/--minpx` + `--off subdivide,cull,hud,spin`，`Up`/`Down`/`+`/`-` 运行时改档
src/demo/textured_sphere/    一条索引流、两种细分的贴图球体（`main.cpp` 单文件）：CPU 侧按键只置 `view.dirty`、下一帧在 `Execute` 里重建（`Mesh::Update` 保留 VAO 只换 buffer，这才是“逐次按键重网格”便宜的原因）；硬件侧把**同一个** `MeshData` 以 `Mesh::DrawAs(GL_PATCHES)` 送进 TCS/TES（球的索引流本来就是每 3 个索引一个三角形，一个三角形就是一个 3 控制点 patch，零额外几何代码；`glPatchParameteri(GL_PATCH_VERTICES, 3)` 属 pass 级裸调，与 `glViewport` 同类），所以 `G` 键切换是在同一份缓冲上对照；自适应 level 只由边的**两端点对称地**算出，否则相邻 patch 会在共享边上裂开（故用 `equal_spacing`）；`GL_MAX_TESS_GEN_LEVEL` 启动时读一次、CPU 侧夹住再传给 TCS（超范围是 UB）；patch 程序链接失败只打 `stderr` + 降级到 CPU 网格，不阻断 demo；三张 512² 程序化 equirectangular 贴图（UV 网格 / 带原点象限标记的 checker / `Noise::Fbm3` 沿圆周采样故经度无缝的 planet）经 `Texture2D::Upload`，零外部资源文件、三平台通用；手写 GLSL 410 一对只读 location 0/1/2，并完整做了 sRGB 往返（贴图 `srgb=true` 上传 → 采样即线性值 → 写回非 sRGB 默认帧缓冲前要 `pow(1/2.2)` 编码，否则颜色写对了也发闷：任何直渲窗口的自定义 lit pass 都适用这条。但“发闷发暗”常有**两个独立原因**，另一个是绕序，别只查一半就下结论）。可执行名 `textured_sphere`；CLI `--segments/--rings/--texture uv|checker|planet/--tess-level/--sphere-radius/--yaw/--pitch/--orbit/--snapshot/--snap-at` + features `--on/--off wireframe,hud,tess,adaptive,cull`（功能开关只能 `--on/--off`，写 `--wireframe` 会被 Flags 报 unknown argument；`segments`/`rings` 这类要转整型的选项必须用 `flags.integer()` 不是 `static_cast<int>(flags.number())`——后者碰到 `inf`/`1e300` 是 UB）。注意 zsh **不做未加引号变量的词分割**：把命令行串存进 `$ARGS` 再 `./exe $ARGS` 会整串当一个 argv 传入 → Flags 报 unknown argument，取证脚本要么写死参数要么逐行执行。两个要写 `view_` 的 pass 成员必须是 `View&` 而非 `const View&`，且 `RenderPass` 无默认构造、派生类得显式 `: RenderPass("Name")`；GLSL 410 里函数体内的 `const vec3 n = normalize(...)` 编译不过（const 初始值必须是常量表达式）。
src/demo/{feature}/          单功能入门 demo：import gldx/gldxwin/gldxcli，用 gldx::win::App+Window 钩子（OnCreate 建 Renderer/MarkAsRenderThread，OnFrame 填 RenderFrame+Render）+ gldx::cli::Flags；只装配它演示的那个子系统；新增一个目录免改 CMake
src/demo/glass_water/        自由飞行的透明/深度对照场景，一个小型多文件工程：`main.cpp`（装配 + 相机 + 截图）+ `Shaders.h`（一份 GLSL 410，透明度由 uniform 参数化，五层共用它）+ `Scene.{h,cpp}`（物品表 + 程序化贴图 + 水面 tile）+ `Input.{h,cpp}`（飞行相机与四个闸门键）+ `BlendPass.{h,cpp}`（五层状态机）+ `Hud.{h,cpp}`（`GlassHudPass`）。不用 `BuildPbrPipeline()`，而是手工 `AddPass`：`BlendPass → PostProcessPass → GlassHudPass`。关键约定：① **混合只在 HDR 线性 target 里做**（8 位整数会舍入每个中间值并截顶 additive），编码留给 `PostProcessPass`，所以 pass 本身不 `pow(1/2.2)`；`glBlendFuncSeparate` 的 alpha 用 `GL_ZERO, GL_ONE` 钉住目标 alpha。② **排序粒度必须匹配几何**：水面是 36 个索引连续的 `GeometryRange` tile（`MakeWaterGrid` 按 tile 逐块发索引，顶点仍共用一张网），tile 与物体塞同一张排序表，否则“排序已开”仍会在一个跨几米的大 quad 内部错序。③ 关排序时走 `Mesh::Draw()`（按声明序遍历 ranges），得到的就是同一批三角形的错误顺序，不需要第二份几何。④ 闭合壳双趟（`GL_FRONT` 再 `GL_BACK`，均不写深度）只在 `shellTwoPass` 的 item 上生效；关掉剔除时该机制退化，所以 `C` 与 `3` 不是两个无关开关。⑤ pass 析构前必须恢复 GL 默认状态（深度写/混合/剔除），`Execute` 末尾显式做，不能依赖下一个 pass 自己设。⑥ `Scene::Add` 返回 `items_` 里的引用，而 pass 长期持有它们，所以 `Build()` 必须 `reserve` 覆盖全场景（超出时 `Add` 会往 `stderr` 报警，不静默悬垂）；`Item::label` 是 borrowed `const char*`，需要拼字符串时得用 `static const char* const[]` 而不是临时缓冲。⑦ **透明度是守恒推出来的，不是四个手调旋钮**：`Surface` 只有 `ior`/`roughness`/`absorption`+`pathLen`/`baseAlpha`/`fresnelToAlpha`，F0 由 IOR 推导（`((ior-1)/(ior+1))²`），fragment 是一份电介质 Cook-Torrance（`D_GGX` + `V_SmithGGXCorrelated` 把 `1/(4NoVNoL)` 折进可见度项，故**刻意没有独立 G 项** + 矢量 `F_Schlick`）；`coverage = mirror·fresnelToAlpha + (1-mirror)·absorb`，`absorb` 来自 Beer-Lambert 斜程 `exp(-absorption·pathLen/max(NoV,0.12))`，`fresnelToAlpha = 1` 时 alpha 严格等于 `1-(1-F)T`，`< 1` 只有一层含义“这块表面在谎报自己的反射率”。`uPremultiply` 与混合方程是**配对关系**：只有方程为 `(ONE, 1-SRC_ALPHA)` 的那一层（Blend）置 1，Sky/Opaque/Cutout/Glow 为 0，所以 `premul_` 必须按层给、不能做成全局；标量 alpha 下 straight 与 premultiplied 逐字节相同，坏的是错配的一对（都不乘→刷白，都乘→a² 衰减）。水面那片“白沫”是亚像素高光火点，用 Karis 解析 AA 压（`fwidth(n)` 按平方和展宽 lobe + 按太阳立体角裁剪峰值，两步缺一不可；调大 roughness 蒙混会连带毁掉要演示的镜面）。太阳立体角写 `PI*θ²` 而非 `2*PI*(1-cos θ)`——后者在 float 里是两个七位有效数字相同的数相减。⑧ **正交下 `viewDir` 取 `-normalize(uCamForward)`**：正交 frustum 没有眼位，透视式在这里不是粗糙近似而是未定义，硬套会让 NoV→菲涅耳→alpha 随屏幕位置漂移（投影伪影）。投影每帧按 `input.ortho` 重设 `SetPerspective`/`SetOrthographic`（resize、滚轮、`Tab` 三处都会动；重设而不是按键时设一次，帧末状态才等于 HUD 报的那个），顶点阶段没有任何分支；`Tab` 翻转那帧按 `orthoHeight = 2·d·tan(fov/2)`（d = 相机到 `kViewTarget` 的距离）把取景交过去，滚轮乘法式缩放当前生效量（fov 8..110、盒 2..80）。⑨ 默认混合模式已是 **premultiplied**（不是 straight），且指针未捕获时整个输入层都不驱动仿真——见上面「验证一个渲染开关是否真的还生效」的第三条前提。可执行名 `glass_water`；CLI features `bloom,hud,sort,depthwrite,shell,cull,wireframe,glow,capture,ortho` + options `blend,yaw,pitch,pos-x,pos-y,pos-z,speed,wave,fov,ortho-height,freeze-at,snapshot,snap-at`；末帧固定打一行 `N blend submissions, M total draws` 供回归比对。
src/demo/CMakeLists.txt      add_gldx_demo(<name>)：d_<name>→OUTPUT_NAME=<name>→link gldx/gldxwin/gldxcli；GLOB(CONFIGURE_DEPENDS) 遍历含 main.cpp 的子目录
src/assets/                  运行期内容（不编译）：models/ 投放目录 · shaders/ 只读参考镜像（不加载）
third_party/                 glad · stb · assimp（子模块）
scripts/                     build.sh.in / run.sh.in / clean.sh.in（CMake 配置期生成 .sh）
.github/workflows/           release.yml（手动触发的三平台构建+发布）
CMakeLists.txt（根，~60 行 orchestrator：project/标准/option → include(cmake/) → add_subdirectory(src) → 脚本生成 + Summary） · CMakePresets.json
doc/                         README(入口) · user/(入门 + API 分章: 骨架/几何场景/资源加载/光照UBO/相机拾取/体素 + 进阶 + 排错 + 10-demos.md Demo 画廊：逐 demo 画面叙述/键位/索引表) · developer/(design：含 §10 仓库目录树 + thread-safety + release：CI 与版本号锚点) · agents/(setup-environment + author-program)
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
- **多窗口**：一 GLFW 窗 = 一 GL context = 一私有对象命名空间，跨窗只共享 CPU 真值（`std::atomic` + 后台 worker）。每窗全部 GL 资源在各自 `OnCreate` 建（首行 `MarkAsRenderThread()`）、`OnDestroy` 毁；`Window` 必须先于其 `View`/renderer 析构。同步多窗参考 `src/demo/multi_viewport/`（共享一个原子时钟、同一场景）；需要**异步时钟 / per-window 剔除 / LOD 分档 / 完全无关的独立 context**参考 `src/demo/multi_window_levels/`（`Profiles.{h,cpp}` 定义五个“等级”窗：同步窗读共享原子、异步窗用 `FrameInfo::dt` 各自累积；接 `Frustum` 进 `GeometryPass` 后同一场逐窗 `visible/total` 不同；island 窗不建 `Renderer`、只 `glClear`+`DebugDraw`，证明 context 可彻底互不相干）。
- **加几何**：`gldx::GeometryFactory::Cube/Sphere/Plane` 或自填 `gldx::MeshData` → `Mesh::Upload(std::move(data))`（渲染线程）。“几何形状会变但资源不重建”（改细分档位、chunk remesh）走 `Mesh::Update(std::move(data))`：整缓冲 orphan 重传、VAO 与属性接线保留，所以能便宜到逐次按键实时重网格；惯例是输入回调只置 `dirty` 标志，重建放在 `Execute` 里（上下文已当前、且紧邻要用它的那次 draw），而 `Execute` 的首帧 guard 要写成“先建再判 `valid()`”——先 `if (!mesh_.valid()) return;` 会永远不建（参考 `src/demo/textured_sphere/`：`Sphere(radius, segments, rings)` 的两个整数即细分，HUD 直接打印换算出的 verts/idx）。**绕序是填索引时的硬约定**：`Renderer::Init()` 设的是 `glFrontFace(GL_CCW) + glCullFace(GL_BACK)`，而引擎着色器无一使用 `gl_FrontFacing` 做双面翻转，所以自填 `MeshData` 时必须保证“从顶点法向所指那一侧看为 CCW”（用具体顶点拿 `cross(v1-v0, v2-v0)` 和该处法向对一下符号即可）；反了不会任何报错，只会少一半面或画出闭合形体的内壁。要把几何交给细分阶段时不必做第二份数据：`GeometryFactory::Sphere` 的索引流本来就是 3 索引/三角形，同一份缓冲 `Mesh::Draw()` 画三角形、`Mesh::DrawAs(GL_PATCHES)` 就是一个三角形一个 patch（配 `glPatchParameteri(GL_PATCH_VERTICES, 3)`，属 pass 级裸调）。
- **画东西（OpenGL 封装边界）**：句柄对象的操作走 RAII 包装（`VertexArray`/`GLBuffer`/`Texture`…，别裸调 `glGen/Bind/Delete`）。绘制原语收在 `VertexArray`：绑定后用 `vao.DrawArrays(mode,first,count)` / `vao.DrawElements(...)` / `vao.DrawElementsInstanced(...)`，**不要**在 demo/pass 里直接 `glDraw*`（全仓 `glDraw*` 只应出现在 `VertexArray.cpp`）。但 `glViewport`/`glBindFramebuffer`/`glClear` 这些**帧级/pass 级**操作**刻意保持裸调**（属本 pass 自身、非某对象，封进去就是透传包装反模式），别去“补封装”。理由见 `doc/developer/design.md` §9。
- **加载模型/贴图**：`AssetManager::RequestModel/RequestTexture(key, path)`（key 由调用方命名，重复 key 被忽略），每帧 `ProcessUploads()`，就绪前 `Get*` 返回 `nullptr`。纹理引用必须落在模型目录内、图片边长≤`kMaxTextureSide`（16384，解码前校验），两条越界都会被拒。
- **改 GLSL**：改 `src/gldx/shader/*Shaders.h`（单一真源），**不要**改 `src/assets/shaders/`（顶层 `.glsl` 已全部废除为只剩注释的镜像；真正从磁盘加载的 demo 着色器住在各 demo 的 `src/demo/{feat}/assets/shaders/` 下）。要 demo/用户从外部 `.glsl` 热加载用 opt-in 的 `ShaderProgram::CreateFromFiles(vertPath, fragPath)`（仅读入后转发 `CreateFromSource`；不破坏内嵌单一真源，普通构建仍零运行期路径依赖）。
- **装配额外着色阶段（几何/细分）**：`CreateFromSource`/`CreateFromFiles` 只处理 vertex+fragment 这对经典组合；要挂几何、细分控制/求值等着色阶段，用泛型入口 `ShaderProgram::CreateFromSources({{ShaderStage::Vertex, src}, {ShaderStage::Geometry, src}, {ShaderStage::Fragment, src}})`（按需列阶段，顺序无关、每阶段至多一次），文件版同理 `CreateFromFiles({...ShaderFile...})`（从磁盘装配多阶段，参考 `src/demo/geometry_shader_file/`：V+Geom+F 三个文件走 `CreateFromFiles({{Vertex,…},{Geometry,…},{Fragment,…}})`）。`ShaderStage` 底层就是 GL 枚举。**4.1 图形管线至少含 vertex+fragment**，否则返回 `std::unexpected`。**没有 `Compute`**——那是 OpenGL 4.3+、超出 4.1 core 基线（macOS 上限），GLAD 4.1 loader 不定义 `GL_COMPUTE_SHADER`；要抬基线另说。参考 `src/demo/geometry_shader/`（喂 `GL_POINTS`、几何阶段扩成方块的端到端示例）；要“一次集成全 5 个图形阶段”参考 `src/demo/shader_stages/`（`CreateFromSources({Vertex,TessControl,TessEvaluation,Geometry,Fragment})` 装配完整管线、画 `GL_PATCHES` 细分线框；细分是 4.0+、几何是 3.2+，均在 4.1 core 内；macOS 下细分+几何共用会触发“SW vertex processing”驱动提示，属良性非错误）。
- **UBO 绑定**：`ShaderProgram::SetBlockBinding("LightingBlock", gldx::LightBuffer::kBinding)` 等，链接后设一次。
- **把几何队列放在 GPU 上（transform feedback）**：4.1 core 没有 compute / SSBO / atomic counter，“还有多少几何要生成”交给 GPU 自己决定只能走 TF。捕获目标必须在**链接前**声明——`ShaderProgram::CreateFromSources({{ShaderStage::Vertex, v}, {ShaderStage::Geometry, g}}, gldx::TransformFeedbackDesc{std::span<const char* const>(kVaryings)})`（`layout(xfb_buffer)` 是 GLSL 4.30、基线内不可用，所以变体名表只能由 C++ 给；`bufferMode` 默认 `GL_INTERLEAVED_ATTRIBS`）。绘制用 `vao.DrawTransformFeedback(GL_POINTS, tf)`——顶点数取的就是 `tf` 那次会话记录的 `PRIMITIVES_GENERATED`，CPU 不经手；第一个 pass 没有上游，用普通 `DrawArrays` 喂种子。读回计数只为 HUD 仪表与退出诊断，故必须先问非阻塞的 `PrimitivesAvailable()`，并且**放在帧首**读上一帧的：同帧 `End()` 后立即查会因 GPU 落后一帧而永远 false。参考 `src/demo/menger_sponge/`（buffer 里只装活工作：每 pass 当场画掉已完成的立方体、只 capture 还要细分的子块，于是队列不会被已画内容占满）。
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
| 全量重编时每个 TU 都报 `clang-scan-deps: No such file or directory` | `brew upgrade llvm` 后旧构建目录的缓存仍钉着带版本号的 Cellar 实路径（`…/Cellar/llvm/<ver>/bin`） | 用预设新开一个构建目录重新配置（`cmake --preset Release` → `cmake --build cmake-build-Release`），别动 CLion 的 debug 目录 |
| 贴图颜色写对了、画面却整体发闷发暗（无任何报错） | 按 sRGB 上传的贴图采样出来是线性值，而默认帧缓冲不是 sRGB 的，写回前缺了编码。**但“发暗”常有两个独立原因**，先排绕序（下一行）再改 gamma，不然会去提亮度把坑埋深 | 在线性域算完光照后 `pow(c, vec3(1.0 / 2.2))` 再写 `FragColor`（走 `PostProcessChain` 的链已由 ACES+gamma 收尾，只有绕过它的 pass 要自己做） |
| 球体/闭合物画成一只**开口的碗**，或近侧整面消失（无报错，顶点法向看着完全正常） | 绕序 inside-out：三角形从外侧看是顺时针，`glCullFace(GL_BACK)` 把整个可见面剔掉，剩下的是远侧内壁（`GeometryFactory::Sphere` 旧索引序就踩过这个，波及 10 个用 `Sphere` 的 demo） | 把背面剔除关掉、在 fragment 里按 `gl_FrontFacing` 压暗：近侧亮远侧暗 = 绕序对，反过来 = 反了，把索引序每三角形反一个面（参考 `textured_sphere` 的 `C` 键） |
| 开了自适应细分，相邻 patch 之间裂开一条缝（T-junction） | 共享边的 `gl_TessLevelOuter` 两端点算出了不同值——`outer[i]` 是顶点 i 的**对边**，且 level 必须只依赖这条边**两端点**的对称函数 | 把判据写成对边长度/深度这类对称量（别用“当前顶点自己的深度”）；用 `equal_spacing` 而非 `fractional_*`，前者把边点放在精确分数处，level 一致即闭合 |
| GLSL 410 编译报 `Initializer not allowed` | 函数体内 `const` 变量的初始化器必须是常量表达式，`const vec3 n = normalize(…)` 不合法 | 去掉 `const`（同一基线下 `half` 是保留字，不能当变量名） |

## CI / 发布

- 工作流：`.github/workflows/release.yml`，**仅手动触发**（`gh workflow run "Build & Release" -f build_type=Release [-f draft=true]`）。
- 矩阵：Linux / macOS / Windows，统一 Ninja，产物 `GLFW_Template-{linux-x64,macos-arm64,windows-x64}.zip`。
- 输入：`build_type` / `release_tag`（空则 `ci-<日期>-<sha>`）/ `prerelease` / `draft`。
- CI **不使用 `--preset`**（用显式 `cmake -S/-B`），故 `CMakePresets.json` 的 Darwin 条件对 Linux/Windows 无影响。
- Windows job **钉死 `windows-2022` 镜像**：更新镜像带来的新 MSVC 在 named module 的 `import` 侧触发 STL 重定义编译风暴；解除条件=确认新工具链能过模块。逐平台其他硬约束见 [`doc/developer/release.md`](doc/developer/release.md)。
- 版本号锚点共 6 处（两个 `kAppVersion`、`project(... VERSION)`、README 的配置摘要行与“当前正式版”链接、本文件的 tag），用 `scripts/release.sh <X.Y.Z>` 一次性重写；**删改 README 时留意这两个锚点**，脚本靠 grep 它们工作（命中不了只会报 `unchanged`，不报错）。

## 约定

- **根 `README.md` 长期保持在 ≤30 行**：只放一句话定位、少量硬事实、快速开始与文档导航。长段落、表格、逐 demo 叙述一律写进 `doc/`（画廊在 `doc/user/10-demos.md`，开关总表在 `doc/user/2a-window-input-cli.md` §8.1，目录树在 `doc/developer/design.md` §10，发布在 `doc/developer/release.md`），不要塞回 README；新增 demo 时改画廊与 `doc/user/README.md` 的覆盖矩阵，而不是 README。
- 提交/推送遵循仓库既有风格（`type: 摘要` + 正文）。**不要**把 `build/`、`cmake-build-*/`、`output/`、`.idea/` 入库（已 gitignore）。
- 自有 `src/**` 代码保持零告警（Homebrew clang 23 基线）；显式 include，不依赖传递包含。
- 触碰会拥有/调用 GL 的代码前，先读 [`doc/developer/thread-safety.md`](doc/developer/thread-safety.md)。
