# 更新日志 / Changelog

本文件记录 `GLFW_Template`（`gldx` 引擎（旧名 `gfx`）+ 演示应用）各版本的变更。历史条目保留当时的 `gfx` 旧称不改写；Unreleased 顶部起用 `gldx`。
版本标签遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

## Unreleased

### 新增 `gldx::TransformFeedback` + demo `menger_sponge`：几何在着色阶段生成，细分层级是 uniform 而非重建

- **`src/gldx/core/TransformFeedback.{h,cpp}`（新）**：TF 捕获会话的 RAII 包装（`Bind()` / `Begin(GL_POINTS)` / `End()`，move-only，一个 GL id 一个 owner；错线程即 `AssertRenderThread` abort，析构补上未闭合的 `End` 而不是让 GL 状态逃逸到下一个持有者）。计数在创建会话时一并申请 query，使用者不必自己管 query 对象生命周期；读出分两个入口——非阻塞的 `PrimitivesAvailable()` 与取值的 `PrimitivesGenerated()`，热路径只许用前者。
- **`VertexArray::DrawTransformFeedback(mode, tf)`**：顶点数直接取 `tf` 那次会话记录的 `PRIMITIVES_GENERATED`，渲染路径上不发生任何 readback，CPU 从不持有“这一帧有多少几何”这个数；仍守住全仓 `glDraw*` 只出现在 `VertexArray.cpp` 的约定。
- **`ShaderProgram::CreateFromSources` 二参重载 + `TransformFeedbackDesc`**：`layout(xfb_buffer)` 是 GLSL 4.30，OpenGL 4.1 core 基线（macOS 上限）内不可用，故捕获目标的变体名表只能在链接前由 C++ 声明（`varyings` + `bufferMode`，默认 `GL_INTERLEAVED_ATTRIBS`）。单参版行为逐字不变，属纯 opt-in 扩展，不触碰既有调用点。
- **新增 demo `menger_sponge`**（`src/demo/menger_sponge/`：`main.cpp` + `MengerShaders.h`）：门格海绵 level-L 恰有 20^L 个等大轴对齐子立方体（3×3×3 中坐标分量=1 的个数≥2 的 7 格被挖），于是“第几个块”就是一个 20 进制数——`gl_VertexID` 逐位解码出中心，几何阶段把中心展成 24 顶点立方体，`uniform int uLevel` **就是**解码循环的上界：调层级不重建任何东西，CPU 每帧只上传一个循环次数，全仓零顶点缓冲、零属性、零 `Mesh`。默认臂把 LOD 决策交给 GPU：CPU 只上传一个种子立方体，一串 TF pass 读同一个队列、当场画掉投影小于 `--minpx` 的块、只把还要细分的 20 个子块写回下一个 buffer（buffer 里因此只有活工作）；LOD 判据（含队列预算）单点住在顶点阶段的 `vKeep`，画臂与捕获臂读同一个 verdict 并共用穷举臂的几何阶段，所以一个块要么被画要么被分，不会两边都不管而留下洞。几何阶段再做三层剔除：视锥保守包围、亚像素、凸体背面。可执行总数 24→25。
- **文档同步**：`doc/user/8-advanced.md` §5 着色阶段的“现场演示”补第 ④ 条（指向 demo，并说明捕获目标为何必须 C++ 侧声明）；README（首段/产物计数/目录树/Demo 索引表）与 `AGENTS.md`（TL;DR 与文件地图登记新 demo、常见任务新增“把几何队列放在 GPU 上”一条）同步。
- **验证**：全量重编零告警（自有 `src/**`）；25 个可执行 `--quit-after 3` 回归 pass=25/fail=0（`shader_stages` 的 SW vertex processing 为仓内已记录的良性驱动提示）；`menger_sponge --quit-after 4` rc=0、stderr 干净，退出行报 `level 3, 8000 cubes in the closing draw, queue peaked at 400 of 26214`；`GLDX_SNAPSHOT`/`GLDX_SNAPSHOT_AT=2.0` 回读一帧为 1600×1200 framebuffer、非 clear 像素覆盖 40.47%，且该比例在 level 1..5 恒定（外皮未被队列预算啃穿）；`--off cull` 与开启态逐像素 0 差异（剔除只省工作、不改画面）；level 3 两臂在 `--minpx 0` 下差 15/1.92M 像素且全是孤立单像素在三色间翻转（两条中心计算路径的末位浮点差，非缺子树）；立方体数随 level 单调 1/20/400/8000/160000/291240；level 5 默认臂 min 24.2ms 画 436678 个立方体，穷举臂同档 min 195ms 画 302892 个（多 44% 几何、快 8.1×）；13 个畸形 argv 用例（`inf`/`nan`/`1e300`/越界/错功能名）全 rc=0、无 UB、无 NaN 进入 uniform。
- **已知边界（未改，取舍留给使用方）**：`--minpx 6` 下 level 6/7/8 与 level 5 逐像素相同——队列在第 5 个 pass 抽干，`uLevel` 高于此只是名义值，HUD 以 `FULL` 明示截断。解除需把队列 buffer 从 8 MiB/个提到 64 MiB/个，等于把“诚实的降级”换成“沉默的显存账单”。

### 基线演示分层：`src/main.cpp` 改走高层 `Mesh`，手搭底层版下放为 demo `hello_triangle`

- **`src/main.cpp`（目标 `GLFW_Template`）改用 `gldx::Mesh` 画 hello-triangle**：默认最小演示应展示绝大多数使用者该走的高层路径——只填 CPU 侧 `MeshData`（3 个 `Vertex`，position+color），`Upload` 一次、逐帧 `Draw`，不再命名 `VertexArray`/`GLBuffer`/`AttachAttribute`。着色器按 `Vertex.h` 固定布局读 location 0（aPos vec3）与 4（aColor vec4），画面与之前逐像素一致。顺带按仓内约定把 std/GLM 文本 include 移到 `import gldx;` 之前（原文件在 import 之后续 `<cstdio>/<memory>/<span>`，是 MSVC 重定义反模式的幸存者而非例外）。
- **新增 demo `hello_triangle`**（`src/demo/hello_triangle/main.cpp`）：原 `src/main.cpp` 的手搭版本（`GLBuffer::Create` + `Bind` 作用域内逐属性 `AttachAttribute` + `DrawArrays`）原样迁入，作为“愿意关注底层的用户自己来看”的对照实现，两份源码 diff 即是 `Mesh::Upload` 代劳的全部内容；窗口标题改为「manual VAO/VBO」与主目标区分。demo 由 `add_gldx_demo` 的 GLOB 自动收编，免改 CMake；可执行总数 23→24。
- **文档同步**：README（首段/产物/目录树/运行效果/Demo 索引表新增 `hello_triangle` 行）与 `AGENTS.md`（TL;DR/命令注释/文件地图）改写基线描述，并顺手把已过时的 demo 计数（16）修正为实际值。
- **验证**：全量重编零告警；`GLFW_Template` 与 `hello_triangle` `--quit-after 3` 均 rc=0，同一三角形画面。

## v1.5.0

### 库线程安全审计 + 纹理格式映射表去重

- **线程安全审计（结论：无需改动）**：复核单渲染线程模型的落地——`RenderContext` 用 `thread_local bool` 实现线程亲和，仅 `MarkAsRenderThread()` 所在线程为真，所有碰 GL 的成员（构造/上传/绑定/绘制/析构/`Use`）入口都以 `AssertRenderThread(...)` fail-fast 守卫；全库跨线程面仅 `ThreadPool` 与 `AssetManager` 两处且都正确加锁（`queueMutex_` 守提交/待传队列、`storeMutex_` 读写锁护资源表、futures 作唯一交接介质，`Upload` 一律回渲染线程 `ProcessUploads` 执行）。无游离可变全局/单例。
- **纹理格式表去重**：`Texture2D` 的 `DataFormat`/`InternalFormat` 与 `Texture2DArray` 的 `ArrayDataFormat`/`ArrayInternalFormat` 是逐字相同的两张通道→GL 格式映射表（array 版原注释即自陈“Mirrors Texture2D's table”），存在漂移隐患。抽为共享内部头 `src/gldx/texture/TextureFormat.h`（`gldx::detail::` 内 `inline`，纯 `GLenum` 算术、无状态无 GL 调用），两个 `.cpp` 从 global module fragment 引用。净删约 40 行重复，行为完全等价。
- **审慎评估后不动**：`ShaderProgram::Set` 热路径的字符串 hash（名多走 SSO、改动仅省栈上临时对象，ROI 极低且触碰导出 API）、RAII 类的 move/析构样板与 `Bind` 的 `glActiveTexture`（target 枚举各异，强行基类化/参数化即项目警告的透传包装反模式）。
- **验证**：全量重建零告警（模块扫描器只重编纹理 2 个 TU 并重链 `libgldx.a`）；23 个可执行 `--quit-after 3` 回归 pass=23/fail=0（`voxel_terrain` 覆盖 `Texture2DArray`、`model_loading`/`pbr_showcase`/`texture_samplers` 覆盖 `Texture2D` 新路径）。

### 跨平台 CI 修复（v1.4.0 之后累积提交首次过三平台）

- **Linux 链接修复**：`gldxwin` 的 `Window.cpp` 跨静态库引用引擎侧 GMF hook `gldx::EncodeScreenshot`，但各可执行的 `target_link_libraries` 按 `gldx gldxwin gldxcli` 排序——GNU ld 单趟扫描下扫到 `gldxwin` 时 `gldx.a` 已过、无法回头解析，报 `undefined reference`（Apple ld 多趟扫描故 macOS 侥幸通过）。把提供方 `gldx` 移到链接行**末位**（`gldxwin gldxcli gldx`）：gldx 是引擎底层、不反向依赖 gldxwin/gldxcli，且 MSVC/macOS 对静态库顺序不敏感，故改动仅修 Linux、他平台无副作用。
- **Windows 编译修复**：`multi_viewport`/`multi_window_levels` 的 `View.cpp` 直接文本 `#include <glm/gtc/quaternion.hpp>` 并使用 `glm::quat`/`angleAxis`，与 `import gldx` 经 GMF 附到 global module 的 `glm::qua` 在 MSVC 下重定义（`C2953 'glm::qua' already defined`）。改为不再在 demo TU 命名 `glm::quat`：赋值旋转走引擎已导出的 `gldx::Transform::SetAxisAngle(axis, radians)`，island 线框的顶点旋转改用 quaternion-free 的 `glm::mat3(glm::rotate(...))`（`matrix_transform.hpp` 其余 demo 均在 Windows 正常编译）。
- **Windows 编译修复（第二轮）**：`texture_samplers/SamplerPass.cpp` 把 `import gldx;` 放在 `<span>`/`<glm/glm.hpp>` 等文本 include **之前**，而 gldx 的 GMF 已把 `std::dynamic_extent`、`glm::qualifier` 附到 global module，MSVC 对"import 后再文本包含同名 global-module 实体"报重定义风暴（`C2374/C2011/C2953`）。按仓内既有约定（`voxel_terrain/main.cpp`、各 `View.h` 都是 std/GLM include 先于 `import`）把 `<cstdint>/<cstdio>/<span>/<glm/glm.hpp>` 全部移到 `import gldx;` 之前；已扫描确认无其它 demo TU 残留此反模式。
- **Windows 链接修复（第二轮）**：`pbr_showcase/main.cpp` 是唯一直接调自由函数 `gldx::CaptureScreenshot`（module-gldx purview）的 demo，跨静态库时 MSVC 模块链接器不解析该 `::<!gldx>` 修饰的外部符号（`LNK2019/LNK1120`，此前被 SamplerPass 编译失败掩盖；macOS/Linux ld 容忍）。改用其余所有 demo 已在用的 gldxwin 成员 `info.window->CaptureScreenshot`（单窗语义等价）。
- **验证**：macOS 本机重配+全量重编零告警，`multi_viewport`/`multi_window_levels`/`GLFW_Template`/`texture_samplers` `--quit-after 3` 均 rc=0；Linux/Windows 侧改动由 draft CI 复验。

### 清理 `src/assets/shaders/`：镜像废除、demo 自带着色器就近归位

- **背景**：`src/assets/shaders/` 顶层的 8 个 `.glsl` 长期作为内嵌 GLSL 的“可浏览镜像”，但镜像与单一真源（`src/gldx/shader/*Shaders.h` 的 `gldx::shaders::k*` raw string）极易漂移，维护成本高且从不被编译/加载；而真正**从磁盘加载**的 `file_demo/` 着色器混在镜像目录里，语义不亲。
- **顶层镜像废除**：`pbr/depth/skybox/instanced/postprocess/ibl/sprite/debug.glsl` 内容清空，每个只保留三行 `[MIRROR DEPRECATED]` 注释，指名其唯一真源的具体头文件与常量名（不再与头文件同步）。
- **demo 着色器就近归位**：`git mv` `file_demo/triangle.{vert,frag}` → `src/demo/shader_file/assets/shaders/`，`file_demo/{points.vert,squares.geom,points.frag}` → `src/demo/geometry_shader_file/assets/shaders/`，删除已空的 `file_demo/` 目录。因这些路径只由 demo 的 `--vert/--geom/--frag` 命令行传入（无硬编码默认路径、不被 CMake 引用），搬迁零运行影响，`*.vert/.geom/.frag` 也不会被 demo 的 `GLOB_RECURSE *.cpp` 误当源码。
- **文档**：重写 `src/assets/shaders/README.md`（镜像表→真源映射 + demo 新位置）与 `src/assets/README.md` 相关描述；同步 `AGENTS.md`、`doc/agents/author-program.md` §6、`doc/user/8-advanced.md` §3/§5、`doc/user/9-troubleshooting.md` §5、`doc/developer/README.md` 中对镜像与 `file_demo` 旧路径的引用。
- **验证**：全量重建零告警零错误；`shader_file`/`geometry_shader_file` 两 demo 传新路径端到端仍 exit 0（内嵌 fallback 与文件加载两条路径都未受影响）。

### 新增 `multi_window_levels` demo（多窗口的同步↔异步、per-window 剔除、LOD 分档、独立 context）

- **背景**：`multi_viewport`/`render_passes` 只覆盖了多窗口的**同步、同场景、同等级**一角——所有窗读同一个原子时钟、渲染同一份确定性场景、只差一个固定相机偏移。但引擎的分层允许窗口彼此更不同，使用者文档旧 §7 也只讲了这一角，没把“同步↔异步、不同 culling（有些窗显示有些窗不）、不同视角、完全无关的 GL 上下文、窗口的不同等级”这些正交维度摊开。
- **新增 demo `multi_window_levels`**（`src/demo/multi_window_levels/`，`--windows N` 默认 5）：五个“等级”窗，每窗一个 `Profile`（`Profiles.{h,cpp}`）同时沿四个独立轴取值——**时钟**（Sync 读 `SharedState` 原子相位 vs Async 用 `FrameInfo::dt` 各自累积，带不同 `rate`）、**视角**（固定 yaw/pitch/radius，绕到对面的 `rear-guard` 与 `flagship` 共享同相位却剔成不同可见集）、**视距/FOV**（远平面与张角当“档位”，短远平面 + 窄 fov 的 `scout` 只剩 `6/38`）、**LOD**（球体细分从 `lod 32` 降到 `lod 8`）。第五轴是 island 窗（`independent=true`）：不建 `Renderer`/`Scene`/PBR program/`LightBuffer`，只 `glClear` 自己的默认帧缓冲 + `DebugDraw` 画一套独立线框，证明一进程一渲染线程里两个 context 可彻底互不相干。每窗 HUD 实时打印 `SYNC/ASYNC`、`phase`、`visible x/38`、`lod`、`far`，把每个维度做成屏上可观测证据。拆为 `Profiles.{h,cpp}` + `SharedState.{h,cpp}`（只有同步窗才读的原子时钟 + worker）+ `View.{h,cpp}`（按 `Profile` 分支建/绘，含 island 特判）+ `main.cpp`（多窗接线）四组文件。
- **文档**：`doc/user/2a-window-input-cli.md` §7 新增 §7.1–§7.4 四小节（同步↔异步、per-window 视锥剔除、LOD/视距分档、完全无关的独立 context），每节配完整可编译代码片段并链到 `multi_window_levels`；`doc/user/README.md` 覆盖矩阵拆出“多窗口同步”与“多窗口异步/剔除/LOD/独立 context”两行；根 `README.md` 目录树 + demo 表、`AGENTS.md` 多窗口 how-to 同步补录。
- **验证**：全量重建（新增 `d_multi_window_levels` 目标）零告警零错误；`--quit-after 3` exit 0（Apple 驱动 `GLD_TEXTURE_INDEX_2D ... using zero texture` 为 `GeometryPass` 采样器占位纹理的良性提示、非错误）；`--shot-at 2` 逐窗抓五张（字节数 48–85 KB 各异，内容互不相同）：实测两同步窗 phase 同为 `3.84` 而 `visible` 为 `23/38` vs `24/38`（同钟不同剔除）、两异步窗 phase `2.24`/`3.24`（各自漂移）、island 窗 `INDEP-CONTEXT` 只画自己线框；全部 23 个可执行 `--quit-after 2` 回归扫描均 exit 0 零回退。

### 使用者文档扩充：覆盖每个特性 + 完整代码 + 现场 demo 链接

- **背景**：`doc/user/` 此前给的多是代码片段而非可直接编译的完整代码，且不系统地把特性链到正在使用它的 demo；尤其 `gldxwin`（窗口/输入/光标/截图/多窗口）与 `gldxcli`（命令行开关）在旧文档里几乎只字未提（第 2 章只讲基础建窗）——与“不仅 GL、还包括窗口”的覆盖要求相背。
- **新增专章 `doc/user/2a-window-input-cli.md`（①·补 窗口·输入·命令行）**：把 `gldxwin` 与 `gldxcli` 摊开讲透——生命周期钩子（`OnCreate`/`OnFrame`/`OnDestroy` + `MarkAsRenderThread` 时机）、`FrameInfo` 字段与 `App::Run`/`App::Now`、输入事件回调（`OnKey`/`OnChar`/`OnMouseButton`/`OnCursor`/`OnScroll` 与 `Key`/`KeyAction`/`MouseButton` 可移植枚举）、轮询态（`KeyIsDown`/`MouseIsDown`/`CursorPos`）、光标控制（`SetCursorCaptured`/`SetCursorVisible`/`SetRawMouseInput`）、窗口几何与动作、`CaptureScreenshot`、`SetUserData`/`UserDataAs`/`Handle()` 逃生舱、`ContextIsCurrent` 多窗断言、**多窗口 N 个独立 context 的完整骨架**，以及 `gldx::cli::Flags` 的 `on/number/integer/real/string/quitAfter/wantsHelp/printUsage`。**每特性都给能直接编译的完整代码**，并逐个链到 `src/demo/{feature}/` 现场演示。
- **扩充 `doc/user/8-advanced.md`**：自定义 `RenderPass` 由 `MyPass` 桩升为完整可编译的 `TintPass`（RAII 句柄 + `VertexArray` 成员绘制 + 帧级裸调对照）；新增 §5《`ShaderProgram` 四入口与全阶段装配》（`CreateFromSource`/两文件 `CreateFromFiles`/`CreateFromSources({...})`/多阶段文件 `CreateFromFiles({...})` + `ShaderStage` 枚举，含全 5 阶段与 macOS 细分+几何良性提示说明）与 §6《文字 HUD 与调试绘制》（`Font`/`SpriteBatch`/`TextRenderer` 完整 pass + `DebugDraw`/`Profiler`）。
- **补齐现场演示链接**：为 `3-geometry-scene`、`4-assets-loading`、`5-lighting-ubo`、`6-camera-picking`、`7-voxel-basics` 每个特性段落补 **🔗 现场演示** 行指向对应 demo（`geometry_upload`/`instancing`/`model_loading`/`pbr_lighting`/`shadow_csm`/`texture_samplers`/`ibl_environment`/`skybox`/`camera_picking`/`voxel_terrain`/`particles` 等）；第 2 章与入门页新增指向 2a 专章的导航。
- **索引**：`doc/user/README.md` 纳入 2a 行并新增“**特性 → 现场 demo 覆盖矩阵**”（窗口/输入/命令行/几何/资源/光照/阴影/采样/IBL/后期/相机拾取/体素/自定义 pass/着色器装配/HUD调试 一次排齐）；`doc/README.md` 同步章表与阅读顺序。纯文档变更，不动代码与行为。

### 新增 `geometry_shader_file` demo（多阶段着色器从文件加载）

- **背景**：四个程序装配入口里，`CreateFromSource`（内嵌 V+F）、`CreateFromFiles(vertPath, fragPath)`（两文件版，由 `shader_file` 覆盖）、`CreateFromSources({...})`（多阶段内嵌，由 `geometry_shader`/`shader_stages` 覆盖）都有 demo，**唯独多阶段文件版 `CreateFromFiles(std::initializer_list<ShaderFile>)` 零覆盖**——它能把几何/细分等可选阶段也从磁盘装配，是两文件版根本表达不了的能力。
- **新增 demo `geometry_shader_file`**（`src/demo/geometry_shader_file/`）：`geometry_shader` 的文件孪生。用 `CreateFromFiles({{Vertex,points.vert},{Geometry,squares.geom},{Fragment,points.frag}})` 从三个磁盘文件装配一顶 V+Geom+F 程序，喂 `GL_POINTS` 由（从文件加载的）几何阶段扩成方块阵——没有几何阶段就画不出方块，故“exit 0 + 截图是 4×3 方块阵”即多阶段文件装配确实编译/链接/运行的证据。沿用 `shader_file` 的可脚本验证契约：三个路径都给才走文件版（给定但加载失败 = **exit 1**），一个都不给退回与文件等价的**内嵌** 3 阶段源（`CreateFromSources`），裸跑/CI 仍安全。配套真·被加载的着色器新增在 `src/assets/shaders/file_demo/points.vert`/`squares.geom`/`points.frag`（镜像目录的例外，README 已标注）。
- **验证**：全量重建（新增 `d_geometry_shader_file` 目标）零告警零错误；三条端到端均符合契约：传三文件路径 exit 0 + 120 KB 方块阵截图、裸跑（内嵌 fallback）exit 0、传不存在路径 exit 1（报 `cannot open nope.vert`）。

### 新增 `render_passes` / `shader_stages` demo（多 RenderPass 子类 + 全着色阶段集成）

- **新增 demo `render_passes`**（`src/demo/render_passes/`，`--windows N` 默认 3）：把两条主线一次跑通——**多窗口/多 context 同步**与**一个 for 循环装配多个不同 `RenderPass` 子类**。每窗在自己的 `OnCreate` 里建一个 context-private `gldx::Renderer`：先 `AddPass` 一个 `ClearPass`（让 pass 执行顺序可见），再 **遍历 `ShapeTable()` 工厂表**逐行 `AddPass` 一个不同的 `ShapePass` 子类——`TrianglePass`（`GL_TRIANGLES`）/ `QuadPass`（`GL_TRIANGLE_STRIP`）/ `LinePass`（`GL_LINES`）/ `PointPass`（`GL_POINTS`）/ 第二个 `TrianglePass`，共 5 个图元并排旋转。跨窗只共享一个 `SharedState`（`std::atomic` 时钟 + 后台 worker，纯 std 头，worker 永远碰不到 GL），故各窗形状同相位旋转；截图连拍先 `freeze` 时钟再逐窗 `CaptureScreenshot`，三张 PNG 逐字节同源（实测 3 窗各 74900 B 完全一致），即“不同 GL context、CPU 真值同步”的直接证据。`PointPass` 走 `glEnable/glDisable(GL_PROGRAM_POINT_SIZE)` 的临时开关姿态（macOS 默认忽略 `gl_PointSize`）。拆为 `SharedState.{h,cpp}` + `Passes.{h,cpp}`（RenderPass 家族 + 工厂表）+ `View.{h,cpp}`（逐窗 Renderer 接线）+ `main.cpp`（多窗接线）四组文件，无巨型 main。
- **新增 demo `shader_stages`**（`src/demo/shader_stages/`）：把选择性多阶段装配推到**全 5 个图形阶段一次集成**——`CreateFromSources({Vertex, TessControl, TessEvaluation, Geometry, Fragment})` 装配一条完整管线，喂 4 个控制点的 `GL_PATCHES` quad，由细分控制/求值对把它细分成密集网格，再由几何阶段把每个生成的三角形重发为 `line_strip` 线框，fragment 按细分 UV 上渐变蓝→橙。截图里“被细分的线框格阵”是五阶段全部编译/链接/运行才能产生的结果（去掉细分对只剩单个 quad，去掉几何阶段则是实心填充）。GLSL 全在 `Stages.h`，`main.cpp` 只留接线与绘制，同样遵“长代码不塞一个 main”。无 Compute 阶段（4.3+，超 4.1 基线）。
- **验证**：全量重建（新增 `d_render_passes`/`d_shader_stages` 两目标）零告警零错误；`shader_stages` 端到端 exit 0 + 134 KB 截图（细分线框格阵核验通过，Apple 驱动的“SW vertex processing for EVAL_PROG + GEOM_PROG”为细分+几何共用的良性提示、非错误）；`render_passes --windows 3` 端到端 exit 0 + 三张逐字节同源的截图（三角形/四边形/线/点/第二三角形五图元可见）；全部 21 个 demo `--quit-after 3` 回归扫描均 exit 0 零回退。

### `ShaderProgram` 选择性多阶段装配（几何 / 细分着色的接入面）

- **新增（gldx 着色器）**：`ShaderProgram` 此前只有 `CreateFromSource(vert, frag)` / `CreateFromFiles(vertPath, fragPath)` 两参入口，无法装配几何、细分控制、细分求值等着色阶段，扩展性受限。现补一个泛型 `enum class ShaderStage : GLenum { Vertex, Fragment, Geometry, TessControl, TessEvaluation }`（底层值即 GL 枚举，直接 `static_cast` 进 `glCreateShader`）+ 两个聚合体 `ShaderSource{stage, source}` / `ShaderFile{stage, path}`，并新增选择性装配入口 `CreateFromSources(std::initializer_list<ShaderSource>)` 与 `CreateFromFiles(std::initializer_list<ShaderFile>)`：调用方用花括号列表按需列出要挂的阶段即可（顺序无关、每阶段至多一次）。核心 `AssembleFromSources` 先校验“至少含 vertex + fragment”（4.1 图形管线的最小集，几何/细分为可选中间级），再逐阶段编译→attach→link，任一编译/链接失败返回 `std::unexpected`（info log）并释放已建的全部 shader，绝不抛异常。**不提供 Compute**：`GL_COMPUTE_SHADER` 是 OpenGL 4.3+，高于本项目 4.1 core 基线（macOS 上限），GLAD 4.1 loader 根本不定义该常量——已在枚举注释里写明“除非抬高基线否则勿加”。
- **向后兼容**：旧的两参 `CreateFromSource(vert, frag)` / `CreateFromFiles(vertPath, fragPath)` 签名不变，内部改为转发给泛型入口，全部既有引擎着色器与 demo 零改动、行为逐字节不变。
- **新增 demo `geometry_shader`**（`src/demo/geometry_shader/`）：端到端验证选择性装配。用 `CreateFromSources({{Vertex},{Geometry},{Fragment}})` 挂一个几何阶段，喂 `GL_POINTS`、由几何着色器把每个点扩成一块带径向渐变的方块——没有几何阶段就画不出这些方块，故「exit 0 + 截图里是 4×3 方块阵」即几何阶段确实被编译、链接、运行的直接证据。纯内嵌源、无外部文件依赖，CI 安全。
- **验证**：全量重建（含 `GLFW_Template` 与全部 demo，新增 `geometry_shader` 目标）零告警零错误；`geometry_shader` 端到端 exit 0 + 120 KB 截图（方块阵核验通过）；`shader_file` 三条回归（文件加载 exit 0 + 截图、坏路径 exit 1、内嵌 fallback exit 0）无回退。

### 新增 `multi_viewport` demo + gldxwin 多窗口契约修复

- **修复（gldxwin 契约）**：`App::Run` 主循环现在每帧先 `glfwMakeContextCurrent(该窗 handle)` 再触发 `OnFrame`——此前单窗口循环恰好运行在"最后一个被 makeCurrent 的 context"上，多窗口循环则会把所有窗口的绘制全部堆进同一个 context。gldxwin 的头注释一直承诺 OnCreate/OnDestroy 在"该窗口 context current"下运行，本次把同样的保证补齐到 OnFrame（单窗口下该调用是幂等 no-op，零行为变化）。
- **新增 demo `multi_viewport`**（`src/demo/multi_viewport/`，`--windows N` 默认 3）：N 个 GLFW 窗口 = N 个互不共享的 GL context，同一场景以不同角度同步呈现。每个窗口的全部 GL 资源（PBR program、网格 VAO/VBO、字体纹理、LightBuffer UBO、Renderer/pass 链）在各自 `OnCreate` 里用**同一段确定性代码**在自己的 context 上独立创建、`OnDestroy` 里独立销毁；共享的只有 CPU 侧真值——一个 `SharedState`（`std::atomic` 的动画时钟/轨道角/心跳计数）由**后台 worker 线程**持续推进、渲染线程逐帧采样，三窗口截图的 phase/ticks 完全一致而视角各差 120°。任一窗口拖拽改变共享轨道角，所有窗口同步摆动；每帧断言"当前 context == 本窗口 context + 仍在唯一渲染线程"。
- **验证**：TSan 下 3 窗口 + worker 连跑 30s 零数据竞争；ASan+UBSan 12 组（1/3/6 窗口、截图连拍、`--windows 0/-5/999/abc`、`--shot-at nan/inf`、`--` 裸分隔）全部 exit 0 零报错；`--shot-at SEC --shot-prefix PATH` 连拍取证先冻结共享时钟再逐窗 `CaptureScreenshot`，三张 PNG 场景状态逐像素同源。gldxwin 修复后全量重建零告警，`pbr_lighting`/`debug_draw`/`camera_picking` 截图回归无回退。

### gldxwin 输入面加厚 + demo 全面去 GLFW 直调 + 巨型 main 拆分

- **新增（gldxwin 输入面）**：`Window` 从「窗口生命周期 + 帧回调」加厚为完整的**输入 proxy**——portable 词汇 `Key`/`KeyAction`/`MouseButton`/`Vec2d`（不暴露任何 GLFW 类型、不依赖 glm），5 个事件订阅 `OnKey`/`OnChar`/`OnMouseButton`/`OnCursor`/`OnScroll`，轮询态 `KeyIsDown`/`MouseIsDown`/`CursorPos`/`SetCursorPos`/`SetCursorVisible`/`SetCursorCaptured`/`SetRawMouseInput`，窗口查询动作 `Pos`/`SetPos`/`Size`/`FramebufferSize`/`Focus`/`SetTitle`/`ContextIsCurrent`，以及 `App::Now()`（glfwGetTime 的薄封装）。gldxwin 自行安装 GLFW trampoline 并**保留 GLFW user-pointer**：demo 不得再调 `glfwSetWindowUserPointer`/`glfwSet*Callback`，改用 `Window::SetUserData` 或在回调 lambda 里捕获。封装边界规则沉淀为「高频重复且代理后不露 GLFW 类型 → 收进 `Window`；稀有/窗口系统专属（raw mouse、clipboard、joystick、file drop）→ 保留 `Handle()` 逃生舱直调」。
- **新增（截图收口）**：`Window::CaptureScreenshot(path)`（gldxwin：makeCurrent + viewport 读回 + 翻转）把 PNG 编码交给 `gldx::EncodeScreenshot`（gldx 全局模块片段，stb_image_write 仍是引擎私有依赖）——两侧 GMF 声明同一 hook 避免模块界 mangling 不匹配。纯 viewport 读回的 `gldx::CaptureScreenshot(path)` 保留可用。
- **重构（demo 去 GLFW 直调）**：13 个含 `glfwGetKey`/`glfwSet*Callback`/`glfwGetTime`/裸 `GLFW_KEY_*` 的 demo 全部迁到 gldxwin 输入面；`texture_samplers` 的手写 VAO/VBO/EBO（`glGen*`/`glVertexAttribPointer`/`glDelete*`）改用引擎既有 RAII `gldx::VertexArray`/`gldx::GLBuffer`/`gldx::Sampler`（`glViewport`/`glBindFramebuffer`/`glDrawElements` 等 pass 作者合法底层操作不代理）。至此 demo 代码中再无 GLFW 常量/函数直调（`Handle()` 逃生舱外）。
- **重构（巨型 main 拆分）**：按「一个 main 别太巨大」把大 demo 拆成多文件小项目——`multi_viewport`（433 行）拆出 `SharedState.{h,cpp}`（原子共享态 + 后台 worker）与 `View.{h,cpp}`（每窗 context-private 资源与逐帧绘制），main 只留接线（→146 行）；`texture_samplers`（221 行）拆出 `SamplerPass.{h,cpp}`（两采样器渲染 pass），main 精简到接线（→58 行）；`voxel_terrain`（873 行）抽出 `Streaming.{h,cpp}`（线程池 + 生成/网格化双队列 + 上传 drain 的整块流式加载），main →681 行。拆分均不改行为，靠 demo 目录 `GLOB_RECURSE` 自动收集新增 `.cpp`，无需改 CMake。
- **验证**：全量重建 61 目标零告警零错误；`multi_viewport` TSan 3 窗口 + worker 连跑零数据竞争、三窗截图连拍正常；`pbr_showcase`/`multi_viewport`/`texture_samplers` 截图回归无回退。

### 新增 `ShaderProgram::CreateFromFiles`（可选的外部 GLSL 加载）

- **新增（gldx 着色器）**：`ShaderProgram` 补一个 opt-in 静态工厂 `CreateFromFiles(vertexPath, fragmentPath)`——读两个磁盘上的 `.glsl`（顶点 + 片元）再转发给既有的 `CreateFromSource`。读文件走 `std::ifstream`（沿用 `Font::LoadFromFile` 的姿态：先量大小、带 4 MB sanity 上限防超大/畸形文件驱动无界分配），打不开 / 读失败 / 超限一律返回 `std::unexpected`、不抛异常，在渲染线程执行（要链接 program）。**这是纯增量能力**：引擎自带着色器的单一真源仍是 `src/gldx/shader/*Shaders.h` 的内嵌 raw string，`src/assets/shaders/*.glsl` 依旧是不被读取的镜像——普通构建保持「运行期零路径 / 工作目录依赖」的 CI 安全保证不变，新 API 只留给 demo 或用户自行热加载的外部着色器。VAO/VBO/UBO/Sampler/FBO 的 RAII 封装（`gldx::VertexArray`/`GLBuffer`/`UniformBuffer`/`Sampler`/`Framebuffer`）此前已齐备，demo 亦无裸调，本轮无需再动。
- **新增 demo `shader_file`**（`src/demo/shader_file/`）：端到端跑通 `CreateFromFiles`。从 `--vert`/`--frag` 指定的文件加载并链接程序，画一个纯由 `gl_VertexID` 生成（无顶点缓冲）的全屏三角，用暖色斜渐变证明“确实有着色器在跑”而非 clear 色。语义上可脚本验证：不传路径时退回与文件等价的**内嵌**源仍能开窗口；传了路径却加载失败则 **exit 1**（“exit 0 + 非空截图”即文件路径真跑通的证据）。配套的真·被加载着色器放在 `src/assets/shaders/file_demo/triangle.{vert,frag}`（镜像目录的例外，README 已标注）。
- **仓库卫生（models 目录）**：`src/assets/models/` 新增局部 `.gitignore` 把投放内容**全量忽略**（只留 README + 这份 .gitignore 撑住目录），删除冗余的 `.gitkeep`；仓库根新增 `.gitattributes`，用**注释掉**的 `*.glb filter=lfs ...` 规则 + 步骤说明写清“将来若要版本化大模型资产，怎么开 Git LFS”（默认仍关闭，不影响三平台“克隆即可构建”）。

### 绘制原语收进 `VertexArray`（OpenGL 函数选择性封装的收口）

- **新增（gldx core）**：`VertexArray` 补三个渲染线程守卫的绘制成员 `DrawArrays(mode, first, count)` / `DrawElements(mode, count, type, offset)` / `DrawElementsInstanced(mode, count, type, offset, instanceCount)`。此前 `VertexArray` 只管 `Create`/`Bind`/`Unbind`/`AttachAttribute`，绘制仍由各处裸 `glDraw*` 发起。收进来后，**全仓库的 `glDraw*` 只出现在 `VertexArray.cpp` 一处**，绘制原语有了单一真源。这三个成员不碰绑定（`Bind`/`Unbind` 仍归调用方），是纯薄封装。
- **重构（库内 + demo 去裸绘制）**：把 `Mesh`/`InstancedMesh`/`VoxelMeshGpu`/`PostProcessChain`/`EnvironmentMap`/`SpriteBatch`/`ParticleBatch`/`DebugDraw` 内部、以及 demo 层 `shader_file`/`texture_samplers` 与 `src/main.cpp` 的 `TrianglePass` 的裸 `glDrawArrays`/`glDrawElements`/`glDrawElementsInstanced` 全部改走新成员。顺带把 `main.cpp` 的 `TrianglePass` 从手写 `GLuint vao_/vbo_` + `glGen/glBind/glBufferData` 迁到引擎既有 RAII `gldx::VertexArray`/`GLBuffer`，app 层再无手写 GL 句柄。
- **封装边界（刻意不封的部分）**：`glViewport` / `glBindFramebuffer` / `glClearColor` / `glClear` 这些**帧级 / pass 级** GL 操作**保持裸调**，不并入 `VertexArray`，也不回收到 `Renderer`。理由沉淀进 `doc/developer/design.md` §9：它们作用于“当前绑定到哪张目标、这一帧怎么清”，语义属 pass 自身而非某个 GL 句柄对象；`Renderer` 刻意只是“全局状态 + pass 排序”，清屏由各 pass 决定（阴影 pass 清深度数组层、后期 pass 清离屏 target、HUD 反而不能清），强行上收会退化成透传包装反模式。`Framebuffer` 已就近提供 `Viewport`/`ClearColor`/`ClearDepth`，够用的地方不再加壳。
- **验证**：全量重建（含 `GLFW_Template` 与全部 demo）零告警零错误；`shader_file` 端到端 exit 0 + 非空截图、`texture_samplers`/`main` 三角形截图回归无回退（绘制结果逐字节不变，仅调用路径改走 `VertexArray`）。

## v1.4.0

### 精简与安全验证（发布前体检）

- **精简**：删除 `InstancedMesh::UploadInstances()` 的私有声明——无定义、无调用方（上传逻辑早已内联进 `Create`/`SetInstances` 的历史遗留；若被引用会是链接错误，删之零风险、接口面不变）。对 gldx 全部公共头 + `gldxwin`/`gldxcli` 接口做了系统性死代码扫描：除此之外无死代码；`Window` 的 `UserData` 槽、`Chunk::SetOrigin` 等为有意保留的对称公共 API，GLSL 内联函数与头内 inline 均证实有消费者。
- **安全测试**：ASan+UBSan 构建下 11 组无头跑全净（含体素流式生成/网格化、脚本挖掘+放水、占位纹理修复路径、两条截图回读）；17 组畸形 argv 对抗输入（`inf`/`nan`/`1e300`/负数/空串/`--`/吞值/拼错功能名）全部正常退出、零 sanitizer 报错；TSan 下体素 demo 空闲 30s + 挖掘压力 25s 零数据竞争。
- **发布制品**：确认 CI 三平台均为全量 `cmake --build` + 整 `output/` 目录打包，zip 内含主可执行与全部 15 个 demo；清理了本地遗留的 pre-refactor `voxel_demo` 产物。
- **验证**：精简后全量重建干净，`pbr_showcase`/`instancing`/`voxel_terrain` 截图复验无回退（nonclear 像素 93.0% / 30.4% / 96.7%；instancing 正是被删声明所属类的使用者）。
- **CI 发布修复**：`voxel_terrain/World.h` 补 `#include <mutex>`（Linux libstdc++ 的 `<shared_mutex>` 不透出 `std::unique_lock`，macOS 构建掩盖了此缺失）；release 工作流 Windows job 钉到 `windows-2022`（`windows-latest` 镜像升到 VS 18 / MSVC 14.51 后，C++20 named modules 在 import 侧 TU 触发 STL 头 C2572/C7571 重定义风暴，v1.3.x 系列均在 VS2022 镜像上验证通过）。

### 修复（PBR 空场景根因）+ 截图验证钩子 + 两个成品 demo 的小项目化拆分

一轮“给每个 demo 真出图、逐张看像素”的验证带出的修复与工具（此前只凭 `--quit-after` 退出码判“零回退”不足为证——空窗口和满画面都 `return 0`）：

- **修复：`GeometryPass` 为未装配的 shadow / IBL 采样器补 1×1 占位纹理**（`EnsureSamplerPlaceholders()`）：PBR 着色器把 `uShadowMap`/`uIrradiance`/`uPrefilter`/`uBrdfLut` 静态声明为活跃采样器，当 demo 未装配 CSM / IBL 子系统时这些纹理单元悬空，Apple 驱动在 draw 时把整个 draw call 判为 INVALID_OPERATION 吞掉——表现为“场景全空、只剩天空/清屏色”。现在 `Execute` 在绑定真实子系统之前，先把这些采样器指向各自专用纹理单元并用类型正确的 1×1 stand-in（depth array / cube / 2D 白图，GL 4.1 无 `glTexStorage` 故用 `glTexImage*` 真实写入一个 texel 使纹理 complete）兜底；真实子系统随后紧接重绑同一单元，因此仅在其缺席时生效。这是本轮“PBR 场景全是空的”的真因。
- **新增 `gldx::CaptureScreenshot(path)`**（`src/gldx/util/Screenshot.h/.cpp`）：在帧末、swap 之前、context 存活时把默认帧缓冲回读为 RGB PNG，按当前 `GL_VIEWPORT` 取尺寸（含 Retina 缩放）。纯验证辅助，不属于任何 pass。
- **新增 `gldxwin` 环境变量截图钩子**（`App.cpp`，全部在 `GLDX_SNAPSHOT` 之后、未设即死代码）：`GLDX_SNAPSHOT=<路径>` 令 `App::Run` 在窗口 BMP 回读一帧（`GLDX_SNAPSHOT_AT=<秒>`，默认 2.0s）后自动 `Close`，给无头回归一个不依赖 demo 主动调用的取证通道。
- **两个成品 demo 拆为小 C++ 项目结构**（不再是单文件数百行、类定义全塞一处）：`pbr_showcase` 把输入回调拆到 `Input.{h,cpp}`、场景构建与资源拥有拆到 `Scene.{h,cpp}`（`ShowcaseScene`/`BuildInstancedField`），`voxel_terrain` 把世界常量与 `World`/`WaterSim` 拆到 `World.{h,cpp}`、输入拆到 `Input.{h,cpp}`、HUD pass 拆到 `Hud.{h,cpp}`；两处 `main.cpp` 只留窗口/帧循环装配。`add_gldx_demo` 已用 `file(GLOB_RECURSE CONFIGURE_DEPENDS)` 收子目录内全部 `.cpp`，新增文件免改 CMake。GPU 资源仍由 `main.cpp` 的栈式生命周期在 `glfwTerminate` 前析构（契约不变）。
- **验证**：`pbr_showcase`/`voxel_terrain` 拆分后各自重建（多 `.cpp` 编译链接通过、`GLOB mismatch` 自动重配）、出图肉眼核对无回退（棋盘地面+球阵+天空+HUD / 起伏地形+树+雾+准星）；全量 52 目标构建干净；本文件所述空场景修复经截图证实——修复前 PBR demo 只剩天空，修复后场景实体正常呈现。

### 破坏性变更 / 重构（引擎改名 `gfx` → `gldx` + 抽取 `gldxwin`/`gldxcli` + CMake 分模块拆分）

把“引擎 + 窗口 + CLI”拆成三个独立的 C++20 named module，并把引擎从 `gfx` 更名为 `gldx`：

- **引擎改名 `gfx` → `gldx`**：named module 名、`namespace gfx`→`namespace gldx`、源码目录 `src/gfx/`→`src/gldx/`（`gfx.cppm`→`gldx.cppm`）、`#include "gfx/..."`→`"gldx/..."`、`import gfx;`→`import gldx;`、CMake target `gfx`→`gldx`、option `GFX_ENABLE_ASSIMP`→`GLDX_ENABLE_ASSIMP`、宏 `GFX_NO_ASSIMP`→`GLDX_NO_ASSIMP`。OpenGL 4.1 基线、单渲染线程、GL 资源在 `glfwTerminate` 前析构的约定、主可执行 `GLFW_Template` 名与 CI 制品 `GLFW_Template-<platform>` 均**不变**；引擎公共接口面除名字外不变。
  - ⚠️ **破坏性**：外部若按名引用 `gfx` target / `-DGFX_ENABLE_ASSIMP` / `namespace gfx` / `#include "gfx/..."` 需同步改名为 `gldx`。
- **新增窗口库 `gldxwin`**（`namespace gldx::win`，静态库 + named module）：引擎无关的 GLFW 窗口库——`App` Meyers 单例（`glfwInit`/`glfwTerminate` + GL 4.1 core hints（含 macOS forward-compat）+ 多窗口帧循环 `Run`）、`Window`（构造即建窗 + `MakeContextCurrent` + `gladLoadGL`，`OnCreate`/`OnFrame`/`OnDestroy` 钩子 + `SetCloseOnEsc` + `UserData` 槽）、`WindowDesc`/`FrameInfo`/`RunOptions`。仅链 `glfw` + GLAD，**不** import 任何 `gldx` 类型；`RenderContext::MarkAsRenderThread()` 由 demo 在 `OnCreate` 首行自调。
- **新增 CLI 库 `gldxcli`**（`namespace gldx::cli::Flags`，静态库 + named module）：原 header-only `src/demo/demo_cli.h` 的 `Flags` 整体迁入（`--off/--on/--quit-after/--help` + `number/integer/real/string` 域夹范围逻辑全保留）；纯标准库、零链接依赖。
- **全部 demo + `src/main.cpp` 改为 `import gldx; import gldxwin; import gldxcli;`**：删除 `src/demo/demo_app.h`、`src/demo/demo_cli.h`；13 个单功能 demo 与 `GLFW_Template` 用 `App::Get().Run()` + 一个 `Window`（`OnFrame`）驱动；`pbr_showcase`/`voxel_terrain` 的手写生命周期迁到 `App`/`Window` 钩子，逐帧行为保持一致（画面零回退）。应用标识常量 `kAppName`/`kAppVersion`/`kWindowTitle`/`kWindowWidth`/`kWindowHeight` 从 `Platform.h` 下沉到各 demo 本地 `constexpr`；`Platform.h` 现在只留 `GLFW_PLATFORM_*` 宏与 GLAD-before-GLFW 顺序。
- **CMake 分模块递归拆分**：根 `CMakeLists.txt` 瘦身为 orchestrator（`project`/标准/`option` → `include(cmake/Dependencies.cmake)` 做 GLFW/GLAD/GLM/stb 探测 + `include(cmake/Assimp.cmake)` → `add_subdirectory(src)` → 脚本生成 + Summary）；`src/CMakeLists.txt` 逐个 `add_subdirectory(gldx/gldxwin/gldxcli)` 并定义 `GLFW_Template` 与 `demo`，每个库/可执行各持 `CMakeLists.txt`；`add_gfx_demo`→`add_gldx_demo`（链 `gldx gldxwin gldxcli`）。目标名与产物目录不变。
- **修复**：`gldxwin::App` 构造内把 `glfwInit()` 提到 GL hints 之前（先前提交误将 `glfwInit` 放在 `glfwWindowHint*` 之后，属运行期顺序 bug）。
- **验证**：`GLDX_ENABLE_ASSIMP=ON` 全量构建（gldx+gldxwin+gldxcli+`GLFW_Template`+15 demo，376 targets）、`OFF` 树配置+构建（160 targets，`model_loading` 优雅降级）；自有代码 `-Wall -Wextra -Werror` 零告警（仅 vendored stb 两类降级）；16 个可执行逐个 `--quit-after` 无头 exit 0、过滤系统噪声后 stderr 为空；`pbr_showcase`/`voxel_terrain` 回归画面零回退（`--freeze-at` 两次跑流式生成/网格计数完全一致）。

### 新增（演示重构：`src/main.cpp` 最小化 + `src/demo/{feature}` 分功能入门演示）

把演示从“两个大入口”拆成“一个最小基线 + 一个功能一个 demo”，全面展示按需装配：

- **`src/main.cpp` 收缩为最裸 hello-triangle**：一个自定义 `TrianglePass : gfx::RenderPass` 在内联 `#version 410 core` GLSL + 手写 VAO/VBO 上 `glClear`+`glDrawArrays(3)`，逐帧只填 `fbWidth/fbHeight` 的空 `RenderFrame`，不碰 `GeometryPass`/`LightBuffer`/PBR/scene。主可执行目标名 `GLFW_Template` **不变**，CI 制品名 `GLFW_Template-<platform>` 不受影响。
- **成品演示迁移（内容零回退，仅换目录 + include 路径）**：`src/main.cpp` 的完整 PBR 展示 → `src/demo/pbr_showcase/main.cpp`；`src/voxel_main.cpp` → `src/demo/voxel_terrain/main.cpp`；`src/demo_cli.h` → `src/demo/demo_cli.h`。
- **新增 13 个单功能 demo**（`src/demo/{feature}/main.cpp`）：`pbr_lighting`、`shadow_csm`、`ibl_environment`、`postprocess_bloom`、`skybox`、`instancing`、`particles`、`text_hud`、`camera_picking`、`debug_draw`、`geometry_upload`、`texture_samplers`、`model_loading`。每个只装配它演示的那一个子系统，其余留缺省。
- **共享脚手架 `src/demo/demo_app.h`**（header-only、非模块）：`demo::Run(flags, title, app)` 拥有 `glfwInit→GL 4.1 core 建窗→makeContextCurrent→gladLoad→MarkAsRenderThread→帧循环→glfwDestroyWindow/Terminate` 生命周期与析构顺序，`demo::Ctx::Loop` 拥有逐帧循环（建空 `RenderFrame`、Esc/`--quit-after` 退出、fps 窗口平均）；内含 `import gfx;`，故 demo TU 只需 include 它。`demo_cli.h` 新增字符串选项 `Flags::string()`。
- **CMake 嵌套与自动遍历**：根 `CMakeLists.txt` 删除重复的两段 `add_executable`，只留 `GLFW_Template`（从 `src/main.cpp`）+ `add_subdirectory(src/demo)`；`src/demo/CMakeLists.txt` 定义 `add_gfx_demo(<name>)`（`d_<name>` → `OUTPUT_NAME <name>` → link `gfx` → 落 `output/`）+ `file(GLOB CONFIGURE_DEPENDS)` 遍历含 `main.cpp` 的子目录——**新增 demo 免改任何 CMake**。`scripts/build.sh.in`/`run.sh.in` 尾提示改为指向 `./scripts/run.sh <demo>`。
- **验证**：`GFX_ENABLE_ASSIMP=ON` 全量构建（gfx + `GLFW_Template` + 15 个 demo 全链接）、`OFF` 树配置+构建（`model_loading` 仍链接、运行降级）；16 个 demo/main TU 用 ninja 真实命令加 `-Wall -Wextra -Werror` 零告警；逐个 `--quit-after 3` 无头跑 exit 0、过滤系统噪声后 stderr 为空；`model_loading` 在 ON/OFF 两构建树各跑一次；`pbr_showcase`/`voxel_terrain` 迁移后回归 exit 0、画面零回退。

### 变更（gfx 运行时解耦：子系统按需装配）

天空盒 / 阴影 / IBL / 后处理(泛光·HDR·MSAA) / 粒子 / 实例化都不再是“必须申请”的前提：
**最小出图路径只需 相机 + 光照 UBO + PBR 或体素程序 + 一个目标**，其余子系统不装配也能跑。解耦只落在
运行时组合层（延续“pass 内 null 守卫 + RenderFrame 聚合 + 线性 pass 列表”），不引入 render graph、不拆分区/多库。

- **渲染目标与后处理解耦**：`GeometryPass`/`VoxelOpaquePass` 不再在 `!f.post` 时 `return`。新增内部接缝 `BeginSceneTarget(f)`：`f.post` 非空时 `Resize`+`BeginScene`（原路），为空时直接绑默认帧缓冲、自行 `glClear` 渲到窗口。`PostProcessPass` 自此为可选 pass。注：直渲窗口路径不经 ACES/伽马合成，颜色空间即着色器输出（现有 demo 仍走完整链，无回退）。
- **天空盒剥离为独立 `SkyboxPass`**：原先硬编进两个几何 pass 末尾的 `skybox->Draw(...)` 抽为一个 `RenderPass`（排在开场的场景目标仍开着、`PostProcess` 之前）。不装配 = 完全不触碰 `SkyboxRenderer`/`EnvironmentMap`。
- **`RenderFrame` 按可选子系统分段**：核心只留 `camera/frustum/scene/viewProj/post(现可选)/lights/pbr/lightSetup/selected/useBloom/ortho/fbWidth/fbHeight/smoothedFps/visibleCount/totalNodes` 与裸指针 `particles`；其余归嵌套记录 `sky{box,env}`、`shadow{map,depth,sunToward,enabled}`、`ibl{enabled}`、`instances{field,prog,enabled}`、`overlay{debug,sprite,font,white,profiler,useDebug}`。默认构造 = 全空/全关。字段改名（旧→新）：`useShadow→shadow.enabled`、`shadow→shadow.map`、`depth→shadow.depth`、`sunToward→shadow.sunToward`、`skybox→sky.box`、`env→sky.env`、`useIbl→ibl.enabled`、`instField/instProg/useInstances→instances.*`、`debug/sprite/font/white/profiler/useDebug→overlay.*`。
- **`Renderer` 组合式 builder**：`BuildDefaultPipeline()` 拆为 `BuildPbrPipeline()`（等价原默认全量档，内部含 `SkyboxPass`）与新增 `BuildMinimalPipeline()`（Geometry→DebugHud，什么都不申请也能出图）。`AddPass()` 仍是唯一装配原语，builder 只是预设。
- **assimp 改为可选依赖（`GFX_ENABLE_ASSIMP`，默认 `ON`）**：`OFF` 时不检/不 `add_subdirectory` assimp 子模块、不 `link assimp::assimp`，给 `gfx` 加 `GFX_NO_ASSIMP` 宏；`ModelLoader::Load` 降级为返回 `std::unexpected("gfx built without Assimp: model import unavailable")` + 一行 stderr 提示。导出接口与签名不变，仅能力在 `OFF` 构建下降为“模型导入不可用”；体素/程序化几何/PBR 不受影响。配置摘要新增 `Assimp` 行。
- **验证**：`GFX_ENABLE_ASSIMP=ON` 下两 demo 全量构建；5 个改动 TU（`RenderPasses.cpp`/`Renderer.cpp`/`RenderFrame.h` 宿主/`ModelLoader.cpp`/两 demo main）用 ninja 真实命令加 `-Wall -Wextra -Werror` 零告警；`-DGFX_NO_ASSIMP` stub 单独编译零告警；`-DGFX_ENABLE_ASSIMP=OFF` 配 `build-noassimp` 树并构 `voxel_demo`，不拉 assimp 子模块、编译链接通过；两 demo 无头回归与逐开关 toggle（全关/全开）exit 0 且过滤系统噪声后 stderr 无报错；临时将 `main.cpp` 切 `BuildMinimalPipeline()`+`frame.post=nullptr` 无头跑几帧，确认直渲窗口出图、无 GL error（验后已还原）。

### 新增（体素 demo）

体素 demo 三个体验问题的根因修复：碰撞缺失、水不流、从下往上看丢面。

- **`Collision.h`（`src/gfx/voxel/`）**：纯 CPU 的 AABB 体素碰撞求解 `gfx::MoveVoxelAabb` / `gfx::VoxelAabbSolid`。把玩家/刚体建模成以脚底中心为锚的轴对齐盒
  （`VoxelBody{radius, height}`），逐轴移动并吸附到被穿过的格面，天然产生“贴墙滑行”；返回 `VoxelMoveResult{hitX,hitY,hitZ,grounded}`。按 solidity
  谓词模板化，无 GL、无全局状态、可任意线程调用；已随 `gfx` 模块导出。单帧位移超过一格需调用方子步进（快速移动/传送时）。
- **`VoxelPipeline.doubleSided`**（默认 `false`，行为不变）：打开后体素不透明 pass 跳过背面剔除，从下方/腔体内看地形不再丢面。

### 变更（体素 demo）

- **飞行相机改为真实碰撞**：去掉“低于海平面且脚下非空就把相机弹回 `kSeaLevel+2`”的启发式（正是“y 到某个高度下不去”的元凶），改用
  `gfx::MoveVoxelAabb` 逐帧解算脚底 AABB（眼睛在脚上方 `kEyeHeight`）：能沿墙滑行、能下到刚挖开的竖井底部；每帧位移按 ≤ 0.5 格子步进防穿透。
- **下落式水流**：新增 demo 侧 `WaterSim`（非库）。挖开水面正下方的方块后，上方的水按每 tick 下落一格的速度填充（守恒体积、到底即停、不横向
  不回流）；由每次编辑标脏的局部工作集驱动，不扫全图。`B` 键或 `--on double-sided` 切双面。
- **验证**：`Collision.h` 与水流规则各自写了纯 CPU 探针（在 `-fsanitize=address,undefined` 下跑过落地/滑行/下竖井/不穿透，与水的下落/守恒/不漂移）；
  三个改动的 TU（`gfx.cppm`/`RenderPasses.cpp`/`voxel_main.cpp`）在 `-Wall -Wextra -Werror` 下零告警；体素 demo 无头多帧跑（含 `--auto-break` 脚本挖掘与
  `--on double-sided`）无 GL 报错。

## v1.3.1

一轮“把每个开关都亲手关掉再打开、拿截图对比”的验证带出来的东西。验证方法本身也进了仓库（`src/demo_cli.h`）。

### 修复

- **粒子在 macOS 上实际上看不见**：`ParticleBatch::Draw` 未开 `GL_PROGRAM_POINT_SIZE`，而该 cap 在 macOS 默认就是关的，
  驱动因此忽略着色器里的 `gl_PointSize`，每个粒子塌成一个物理像素（Retina 下几乎不可见）。
  现在与 depth/blend 一起成对保存、开启、恢复，并在 `ParticleBatch.h` 的契约注释里写明。
  截图取证：同一固定画面下 `--off particles` 从 0.005%（与噪声同量级）变为 3.59% 画面变化。
- **`scripts/build.sh` 在 macOS 上根本配不起来**：生成的脚本用默认的 Unix Makefiles（不支持 C++20 modules）
  且不挑编译器（AppleClang 没有 `clang-scan-deps`）。现在自动选 Ninja、在 macOS 上自动导出 Homebrew LLVM 的
  `CC/CXX`（已显式给 `$CXX` 则不覆盖）、生成器/编译器与旧缓存不符时加 `--fresh` 重配，结尾提示两个可执行文件。
  这三件事 CI 与 README 早就做对了，只有生成的脚本漏了。
- **文档与 HUD 的键位文案对不上实际**：体素 README 写着按住 `Q`+`E` 减速，而代码里根本没有这两个键
  （实为按住 `Shift` 减速、`Ctrl` 下降），也没提 `Tab` 切投影；PBR 的 HUD 提示串少了天空盒的 `6`，
  README 则漏写 `6` 与 `Tab`。逐条对着 `glfwGetKey` 的调用点核过再改。
- **天空盒没有键盘开关**：HUD 会显示 `Sky ON/OFF`，但只有命令行能改。新增 `6` 键。
- **HUD 的 fps 读数与它所在的那一帧无关**：`EMA(1/dt)` 被最后卡顿的一帧主导，两个 demo 均改为固定窗口（≥ 0.5 s）计数。
- **demo 的时间驱动内容不可复现**（导致截图对比本身不可信）：carousel 用 `glfwGetTime()` 绝对值做相位（随开机时间漂移，
  且 float 化后精度损失已可比见）；体素碎屑用变步长积分（同参数两跑差 2% 画面）；脚本挖掘节拍相对上一次触发排序
  （首帧跨多步则整体平移，最后一炮落在 `--freeze-at` 内外全看帧率）。现在动画时钟是“距启动秒数”、
  碎屑走固定 120 Hz 步长、节拍锁在绝对步数边界上：**同参数两跑逐像素全等**（实测噪声底 0.000%）。
- **脚本化验证曾被真实鼠标悄悄改写**：Fly 相机无条件 `GLFW_CURSOR_DISABLED` 捕获指针，而 `MouseCallback` 把
  鼠标增量写进 `yaw/pitch`——用户在同机工作时动一下鼠标，准星就离开 `--yaw/--pitch` 放的位置。症状是同一组
  参数的两跑结果不一致：`--auto-break 25` 报出的 `blocks broken` 在 0..38 之间乱跳（一个都没命中也能报 0）。
  现在 `--freeze-at` 同时意味着不捕获指针（`ApplyCapture` 新增 `keepCursor`），手动按 `ESC` 仍可自由捕获。同一组
  参数稳定拿到 `blocks broken 25`＝请求数（25 次挖掘全部命中），HUD 末行也从 `ESC pointer captured` 变为
  `ESC pointer free`（截图为证）。

### 加固

- `Texture2D::Upload` / `Texture2DArray::Upload`：`glTexImage*` 会按 `width*height*channels` 从调用方 buffer 里读，
  所以先证明 `pixels` 至少那么长。通道数限定在 1..4（`DataFormat` 把其他值映射成 RGBA，上传格式与长度计算会不一致），
  边长/层数上限 16384 / 4096（使乘法不回绕），长度校验改用除法而不是可能回绕的乘积。
- `GeometryPass` / `PostProcessPass` / `DebugHudPass` / `ShadowPass`：只使用应用真正交出的子系统，
  入口一次判全必需指针（与 `VoxelOpaquePass` 已有契约对齐），免得一个可选特性为 null 时解空指针。
- `BlockRegistry::Append`：达到 `kMaxTypes` 后返回 `kAir` 而不是把 id 回绕到内置方块上（否则一个自定义方块会默默变成草）。

### 安全与健壮性（ASan + UBSan 对抗输入自检）

把 `Chunk` / `ChunkMesher` / `BlockRegistry` / `Noise` / `RaycastVoxel` / `Frustum` 这些 CPU 侧原语，加上图片解码，喂给一份用 `-fsanitize=address,undefined` 与 `-fno-sanitize-recover=undefined` 编译的 libgfx，输入包括 NaN、±inf、1e30、`INT_MIN` 坐标、未注册 id、越界访问、反向包围盒、畸形图片文件。UBSan 在“没崩”的情况下揪出三处真未定义行为，均已修：

- **`RaycastVoxel` 的 float→int 转换**：起点/方向只要有一个分量是 NaN、inf 或 1e30，`(int)std::floor(oA)`
  就越出 int32 表示范围（UB），紧随其后的 `lo - cell` 还可能有符号溢出。现在入口拒绝非有限的射线
  （契约已写进注释：这种射线答“未命中”而不是读出一个随机 cell），首 cell 改在 double 域算出、只在确认
  落在 `[lo, hi]` 内后才转 int，`tMax` 用等价的闭合形式一次算出（对合法输入逐位等价，且少一次舍入）。
- **`Noise::Perlin2/Perlin3` 的 `floorMask`**：同一个 `(int)std::floor(v)` 问题，`v` 来自 fbm 的倍频阶梯，
  坐标一大就 UB。Perlin 只用得到 `(int)floor(v) & 255` 和小数部分，两者对“减去 256 的整数倍”都不变量，
  所以先 `std::fmod(v, 256.0)` 折叠再取整：合法坐标逐位等价，极端坐标从 UB 变成确定的有限输出。
- **`ChunkMesher` 的跳过条件**：原来只测 `id == 0`，于是未注册的 id（旧存档的方块表、被卸载的 mod）
  虽然被 `BlockRegistry::Get` 当作 air 解析，却仍然生成贴图切片 0 的幽灵面。新增
  `BlockRegistry::IsAir(id)`（id 为 0 或超出表长），生成面与邻居遮挡判定都改用它。

- **demo 自己的命令行也在这条链上**：`strtod` 乐意把 `inf`、`nan`、`1e300` 当成合法数字，而
  `voxel_main.cpp` 里有四处 `static_cast<int>(flags.number(...))`（`--select` / `--rise` /
  `--auto-break` / `--auto-place`）——正是上面那两个探针抓到的同一类 UB（独立探针实测
  `(int)strtod("inf")` 在 UBSan 下 rc=134）。新增 `Flags::integer()`（越界饱和、NaN 落到下界）
  与 `Flags::real()`（非有限值回退默认，边界在 double 域内夹好之后再转 float），四个 int 调用点
  与 `--yaw` / `--pitch` / `--radius` 全部改用它们。`--auto-break` / `--auto-place` 另有 100000
  上限：下一行就要把两者相加，只饱和到 `INT_MAX` 会让那个加法回绕成负数。
  一点如实的区分：double→float 的越界转换在这套工具链上 UBSan 并不报（实测 `static_cast<float>(1e300)`
  rc=0，加 `-fstrict-float-cast-overflow` 也不报），所以 `real()` 是防御性收敛，不是被抓到的 UB。

- **`ModelLoader::Load` 的两处收尾**：把 8 份畸形模型喂给带 ASan + UBSan 的 libgfx（只有 `v` 没有 `f`、空文件、
  随机字节、截断的 glTF JSON、指向不存在 `.bin` 的 glTF、面索引越界、位置为 NaN/inf、材质指向一张只有文件头的
  PNG），判据不是"没崩"，而是"被接受的那些模型里，每个索引都指向该 mesh 自己发出的顶点"。两处改动：① 面索引现在由
  我们自己判界并整面丢弃（丢单个索引会打乱下面按三个一组算出的 `GeometryRange`）——OBJ importer 恰好会拒绝这种文件，
  但把这条不变式托付给"哪个 importer 碰巧跑了"不负责任；② 位置非有限时打一行 stderr，因为 NaN 顶点是合法上传、
  永不光栅化，用户的感受是"模型不见了"而不是"模型坏了"。两个 demo 都不经 `AssetManager::RequestModel`，画面无关。

- **模型文件里的一条路径引用可以走出模型目录**（纹理和 buffer 不对称）：assimp 自己把 glTF 的 buffer uri 关在
  模型目录里（绝对路径和 `../../x.bin` 都报 "could not open referenced file"），而 `ModelLoader` 的纹理路径是
  手工拼的：`dir + texPath` 对绝对引用产出 `models//etc/hosts` 这种既不是模型所指、也无法读取的字符串（一个
  真实缺陷：带绝对纹理路径的 `.mtl` 从来就没加载出过贴图），而对 `../../../../etc/hosts` 则原样交给调用方去
  打开——一条依赖已经守住的规则，在我们的拼接里漏了。现在 `LeavesDirectory()` 拒绝绝对路径、盘符与任何 `..`
  段并留一行 stderr；相对引用（含子目录）不受影响。

- **144 KB 的 PNG 能让 worker 先分配 148 MB 再被丢掉**：`LoadImageToDesc` 直接 `stbi_load`，尺寸检查在下游
  `Texture2D::Upload`（16384 上限）才做。实测一张 16500×3000 的全零 PNG（144 KB）解码后峰值 RSS 从 185 MB
  涨到 643 MB，然后被 Upload 整个拒掉。现在先用 `stbi_info` 只读头部比对上限，同一张图在 0 MB 增量上被拒。
  顺手把这个上限从两个 `.cpp` 的匿名 namespace 提到 `Texture2D.h`（`inline constexpr kMaxTextureSide`），以
  免解码端与上传端各写一份而漂移。

### 新增

- **`src/demo_cli.h`**：两个 demo 共用的 header-only 命令行开关（不进 `module gfx`）。
  `--off a,b` / `--on a,b` / `--quit-after SEC` / `--help`，以及数值选项
  `--freeze-at` / `--yaw` / `--pitch` / `--radius` / `--rise` / `--select` / `--auto-break` / `--auto-place`。
  不传任何选项时与旧行为逐位等价（`flags.number(name, fallback)` 直接回退默认值）。
- **`voxel_demo` 退出统计行**：帧数、平均/最低 fps、生成与网格化的 chunk 数、破坏方块数、存活/累计粒子数——
  流式收敛与粒子压力这类“截图看不出来”的性质自此可自证。

### 验证结果

- **对抗输入**：35 项检查在 ASan + UBSan（不可恢复）下全通过，进程退出码 0 —— 任一 UB 都会当场 abort。
  除上述 CPU 原语外还包括图片解码：10 个畸形文件（只有头的 PNG、空文件、随机字节、只有一半的 JPEG、
  声称 6000×6000 却只装 8×8 数据的 PNG）× 6 个 `forceChannels`（含非法的 `7` 与 `-3`）组合，
  20 次被接受的解码每一次都满足 `pixels.size() == width*height*channels`——正是 `Upload` 先验再交给
  `glTexImage2D` 的那个不变式。探针本身是仓外的临时工具（`/tmp/adv/advcheck.cpp`）：它是 `import gfx;` 的
  模块消费者，因为定义在 module 实现单元里的实体带着模块附着（`__ZN3gfxW3gfx5NoiseC1Ej`），
  文本 include 同一份头文件链不上。
- **畸形模型文件与退化视口**：9 份模型文件（8 份畸形 + 1 份正常立方体，正常那份是"失败不等于加载器坏了"的对照）
  与下面的视口检查合起来 29 项，在 ASan + UBSan 下全通过，退出码 0（含 0×0 / 1×1 / 超过
  `GL_MAX_TEXTURE_SIZE` 的视口、`SpriteBatch` 与 `PickRay` 的退化尺寸）。退化尺寸下驱动只报
  `GL_INVALID_VALUE` 而链路自愈：`PostProcessChain::Resize` 拒绝 0/负数尺寸保住上一个好尺寸，不可能的尺寸让 FBO
  不完整（有诊断行），下一次合法 `Resize` 即恢复；`PickRay` 在 0 高度下给出非有限射线，而加固后的
  `RaycastVoxel` 与 `RaySphere` 都拒绝它。

- **模型引用逃逸与图片解压炸弹**：探针扩到 34 项（ASan + UBSan，退出码 0），改前/改后跑同一份：绝对与越界的
  纹理引用从"返回 1 条越界路径"变成"0 条 + 一行诊断"（对照组：`badtex.obj` 的相对引用始终保留 1 条路径），
  glTF 的绝对/越界 buffer uri 两轮都由 assimp 拒收；`bomb.png`（144 KB 声称 16500×3000）的峰值 RSS 从
  643 MB（基线 185 MB）降到 185 MB（增量 0），拒收文案带上真实尺寸与上限。两个 demo 都不从磁盘读图或读模型
  （`src/assets/models/` 只有 README，几何与纹理全是程序生成的），所以这两处改动不可能影响画面；仍复跑了一轮
  12 组开关矩阵验收（常量上提重编了纹理上传）。

- **异步资产管线（两个 demo 都不跑的那条路）**：探针扩到 42 项。`AssetManager` 的 `RequestTexture` /
  `RequestModel` / `ProcessUploads` 此前从未在运行时被驱动过，现在用隐藏窗口一次覆盖完：模型无纹理（第二阶段
  会不会永远等下去）、纹理解码失败的模型、被拒的模型、读不到的图片、被头部拒收的超大图片、同一 key 重复请求。
  ASan + UBSan 与 TSan 各 42/42、退出码 0、**0 条数据竞争**。一个只属于探针的坑：`ProcessUploads` 每帧一次，
  而空转 900 次循环在 TSan 下不足一毫秒墙钟，worker 根本来不及跑完——第一版因此报“18 项仍 pending”，
  给每帧加上 1 ms 的 sleep 后同一份代码 3 帧排空。

- **对抗 argv**：两个 demo 用 UBSan 构建跑畸形命令行——`--select inf`、`--auto-break 1e300`、
  `--rise nan`、`--auto-place -inf`、`--pitch nan`、`--radius -1e300`、`--yaw 1e300`、`--off nosuch`、
  `--on bogus`、缺值的 `--auto-break`、`--freeze-at abc`、空值、`+` 与 `x`——退出码全 0，无一条 sanitizer
  报告；拼错的 feature 与非数字的值各自留下一行 stderr，而不是被静默吞掉。
- **数据竞争（ThreadSanitizer）**：三轮覆盖两条线程路径——体素空闲 25 s（1067 帧，378 gen / 486 meshed，
  worker 与渲染线程一直在交接 chunk）、体素挖掘压力 30 s（`--auto-break 60 --auto-place 40`，47 次挖掘 /
  1034 个粒子 / 366 次 remesh，即反复抢写锁）、PBR 25 s（`--on instances,debug`）。**0 warning**。
  两阶段线程约定（worker 只算 CPU、渲染线程独占 GL）在运行时拿到了旁证。

- **性能**（60 s × 3 轮，`/usr/bin/time -l`）：PBR CPU 18.3 s / RSS 117 MB；体素空闲 20.7 s / 131.8 MB /
  平均 116 fps（最低 81.5）/ `chunks gen 243 == meshed 243`（流式收敛）；体素压力（`--auto-break 200`）
  25.3 s / RSS 131.5 MB（长跑不涨）/ 平均 117.3 fps / `spawned 748`、退出时 `live 0`（粒子池全部退休，无泄漏）。
  同参数再各跑 180 s：PBR 退出码 0、RSS 114.6 MB（低于 60 s 基线的 117 MB）、`[ModelLoader]` 诊断 0 次；
  体素退出码 0、RSS 127.8 MB（基线 131.8 MB）、`chunks gen 243 == meshed 243`——三分钟不比一分钟胖，
  流式队列在长跑里仍然收敛。

12 个开关全部拿到“信号 ≫ 噪声底”的截图证据（每组噪声底 0.000%，即同参数两跑逐像素全等）：
PBR `shadow` 0.14% / `ibl` 15.4% / `bloom` 57.8% / `debug` 3.7% / `instances` 6.6% / `sky` 91.4% / `ortho` 18.7%；
体素 `particles` 3.59% / `fog` 63.5% / `sky` 36.5% / `ortho` 77.8% / `water` 4.5%（均为画面变化像素占比）。改完 CLI 的数字访问器后整轮复跑，12 项数值与改前逐项相同——默认命令行下的画面一字未动。`ModelLoader` 收尾与指针捕获两处修复之后又复跑一轮，结果文件逐字段与上一轮完全相同（含每组的绝对 `changed_px`）。

### 文档一致性

- **示例 API 与代码对不上**：`doc/user/`、`doc/agents/` 与 `AGENTS.md` 里的异步加载示例写的是
  `RequestTexture(path)` + `Find*`，而真签名是 `RequestTexture/RequestModel(key, path)`，查找方法是
  `GetTexture/GetModel`（返回 `shared_ptr`，就绪前为空）——`Find*` 从未存在过。全部示例改正，并补上
  "重复 key 被忽略"与返回类型。入门/进阶的操作与开关表补上 v1.3.1 新增的 `6`（天空盒）与 `Tab`（投影切换），
  排错速查表新增模型无贴图/模型"不见了"/图片超限三行（措辞照抄 stderr/错误串的真实前缀）。
- **`src/assets/` 三份 README 中文化**（仓库文档规范要求全中文），顺带修两处陈旧内容：模型目录的 README
  引用了不存在的 `src/utils/stb.cpp`（实现在 `src/gfx/third_party/stb_image_impl.cpp`）并据实补上本轮新增的
  两条拒收规则；`assets/README.md` 里"模型是唯一从磁盘读的东西"不属实（HUD 字体也走 `Font::LoadFromFile`），
  改为"模型/贴图与字体两类"并说明后者是可选同步加载。
- **使用者文档拆分入门小章**：原 `2-basic-usage.md` 一章塞了四个不相关主题，拆为骨架（`2-core-setup`）、
  几何与场景（`3-geometry-scene`）、贴图与模型加载（`4-assets-loading`，并入原进阶的两阶段细节）、
  光照与 UBO（`5-lighting-ubo`）四章，另新增从未进过用户文档的相机/拾取（`6-camera-picking`）与体素
  子系统（`7-voxel-basics`）两章；原进阶/排错顺延为 `8-advanced`/`9-troubleshooting`。新章每个签名先对
  头文件核实后再写（修正了初稿中不存在的 `SceneNode::CreateChild`、`World::Set`（实为 `Edit`）、
  静态形式的 `InstancedMesh::Create`、`LightBuffer::handle()` 四处）；agent 速查表同步修掉已改签名的旧写法（`PickRay` 多余的 `eye` 参数、
  `Sphere/Plane` 参数名、`InstancedMesh::Create` 实为返 bool 的成员函数），并清掉本 CHANGELOG 自己的两处
  陈旧 API 名（`Fbm2d`→`Fbm2`、`MoveUp(dt)`→`MoveUp(distance)`）；全仓 markdown 的相对链接/锚点与
  全部 `gfx::` 符号、反引号内方法名经脚本扫描零断链、零不存在的符号。

## v1.3.0

### 新增

- **体素子系统 `src/gfx/voxel/`**：`Chunk`（16³ uint16 方块存储 + 本地/越界查询 + dirty 与邻居 dirty 标记 + 包围球）、
  `BlockRegistry`（header-only，内置 air/grass/dirt/stone/sand/wood/leaves/water/snow，`solid/transparent/cutout/emissive` 属性）、
  `ChunkMesher`（纯 CPU、可在 worker 线程跑：相邻不透明面剔除、同种方块互剔皮肤、逐顶点 4 档 AO + 方向 tint、
  输出 opaque / transparent 两份网格）、`VoxelMeshGpu`（一条 chunk 的 GPU 记录，`Update` 原地换缓冲）。不做 greedy meshing。
- **`Texture2DArray`**：一方块一个纹理层，生成完整 mipmap 链，语义对齐 `Texture2D`；层间隔离彻底规避图集 bleed，
  顶点只需携带一个 layer float。
- **`Mesh::Update(MeshData&&)`**：glBufferData 整缓冲 orphan 重传、VAO 复用，chunk remesh 不再重建 GPU 资源。
- **`VoxelOpaquePass` / `VoxelTransparentPass`**：chunk 不进 `Scene` 节点图，pass 直接持有渲染记录列表，
  按相机距离降序（back-to-front）绘制透明流并关掉 depth-write，不透明流与 cutout（树叶）同批走 alpha cutoff；
  两者都做包围球视锥剔除。
- **`VoxelShaders`**（GLSL 410）：采样 `sampler2DArray`，顶点 light 通道调制 + `LightingBlock` 平行光近似 + 雾 + 水面 uv scroll。
- **`RaycastVoxel`（DDA）**：`src/gfx/camera/VoxelRay.h`，Amanatides & Woo 逐格步进，header-only 纯函数，
  对 solidity 谓词做模板参数（chunk / chunk 网格 / 扁平数组皆可），返回命中格 + 进入面法向 + 距离，放置方块不必二次查询。
- **`Camera` 飞行接口**：`SetYawPitch(yaw, pitch)` 与 `MoveForward/MoveRight/MoveUp(distance)`（参数是世界单位距离，`速度×dt` 留在调用方游戏层），原有 orbit 行为不变。
- **`Noise`（`src/gfx/util/`）**：seeded 排列表的 Perlin 2D/3D + `Fbm2(x, y, octaves)` + `ToUnit`；确定性、无全局状态、worker 线程安全。
- **`ParticleBatch`**：POINT sprite 批次，`Spawn/Update/Draw`，着色器内按 lifetime 缩尺寸与淡出（挖掘碎屑、放置反馈）。
- **线性距离雾**：`LightingBlock` 尾部追加 `fogColor` / `fogParams`，`LightSetup` 加 CPU 侧字段，PBR / Instanced / Voxel
  三条 fragment 程序统一 `mix`；默认关闭，未开启时既有场景视觉零变化（2D 精灵不雾）。
- **`voxel_demo` 可执行目标**：第二个 target（`src/voxel_main.cpp`），fBm 高度场地形 + 水位 + 树，
  worker 生成/网格化 + 渲染线程限量上传，飞行相机、DDA 破坏/放置、碎屑粒子、雾与 HUD；
  `scripts/run.sh [target]` 据此选择运行目标。世界层（chunk 网格、流式策略、地形生成、编辑规则）刻意留在 demo 侧。

### 边界（本轮不含，留作后续增量）

- 火把光 flood-fill 传播、greedy meshing、方块实体 / 生物 / 合成 / 存档 / 网络；`Chunk` 尺寸固定 16³。

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
