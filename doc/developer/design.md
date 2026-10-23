# 设计

本文说明 `gldx` 这一层是如何组织、以及为什么这样组织。文中引用的文件名与符号
都真实存在于 `src/gldx/**`。

## 1. 分层的子系统

引擎是 `src/gldx/` 下的一组小子系统，每个只负责一个概念。依赖方向**只向下**——
上层可以用下层，反过来绝不行。

```
  应用层
    src/main.cpp ·························· 演示应用，负责装配
        │ 使用
  编排层
    render/ · Renderer · RenderPass · RenderFrame
    scene/  · Scene · SceneNode · Transform ···· CPU 场景 + GL 各 pass
        │ 使用
  特性层
    geometry · texture · material · shader
    camera   · light   · shadow   · text  · debug
    assets   ······························ 模型/贴图的异步加载
        │ 使用
  平台 / GL 层
    core/ · RenderContext · GLBuffer · VertexArray · UniformBuffer · Sampler · Framebuffer
```

- **core/** 封装裸的 GL 句柄，并持有线程亲和守卫（`RenderContext`）。它以上的
  每一层都用这些 RAII 对象来表达。
- **scene/** 是纯 CPU（矩阵 + 包围体），不碰 GL，因此脱离上下文也能安全推理、
  独立测试。
- **render/** 是唯一逐帧编排 GL 的地方。

## 2. 场景图（`src/gldx/scene/`）

一个轻量的变换层级，刻意**不是** ECS。

- `Transform`（仅头文件）：`translation` · `rotation`（单位四元数）· `scale`。
  `LocalMatrix()` 组合出 T·R·S（列主序 GLM）；
  `WorldFrom(parentWorld)` = `parentWorld * local`。
- `SceneNode`：持有一个 `Transform`、一对**非拥有**的 `const Mesh*` +
  `const PbrMaterial*`、一个子节点容器
  `std::vector<std::unique_ptr<SceneNode>>`，以及 `visible` / `castsShadow`
  标志。`UpdateWorld(parentWorld)` 递归下推，缓存本节点的世界矩阵与一个
  **世界空间包围球**（`center = world * localCenter`，
  `radius = localRadius * maxAxisScale(world)`）。局部球由
  `SceneNode::BoundsFromMeshData` 从 `MeshData` 一次性算出。
- `Scene`：一片根节点组成的森林。`Update()` 用单位矩阵刷新每个根，并把遍历
  展平成有序列表——`Renderables()`、`ShadowCasters()`、`PickTargets()`——供各
  渲染 pass 直接消费，无需再走一遍树。

因为节点持**非拥有**指针，所以 `Mesh`/`PbrMaterial` 的生命周期归*应用*（即
`src/main.cpp`）；节点只是引用它们。这始终把 GL 对象的所有权留在渲染线程、
留在 CPU 层级之外。见 [thread-safety.md](thread-safety.md) §4。

## 3. 渲染管线（`src/gldx/render/`）

一条**线性 pass 链**，不是完整的 render graph。

- `RenderFrame` 是逐帧的值类型结构，拆成一个小的**核心**（足以出图：相机 + 视锥 +
  场景 + 一个光照 UBO + 一个着色器程序 + 一个目标）和若干**可选子系统记录**
  （`sky`/`shadow`/`ibl`/`instances`/`overlay`，外加裸指针 `particles`）。每个记录
  把某项功能的指针与启用开关归在一起；缺省构造即"应用没带那个子系统"，消费方都
  停在各自的 null/开关守卫后——这就是天空盒/阴影/IBL/泛光/实例化从"必须"降为"按需
  装配"的机制。`post` 如今也是可选：置空则几何/体素 pass 直渲默认帧缓冲。它不拥有
  任何 GL 资源——只是"这一帧的输入"。
- `RenderPass` 是抽象基类：`virtual void Execute(RenderFrame&) = 0;` 外加一个
  `name`。它不可拷贝。
- 具体 pass（在 `RenderPasses.h/.cpp`），按序执行：
  1. `ShadowPass` —— 更新 CSM，逐层联级把投影者渲进深度数组。
  2. `GeometryPass` —— 把 HDR 场景渲进 MSAA target（无 post 链时直渲窗口）：绑定
     UBO/阴影/环境，绘制经视锥剔除的可渲染物（对被拾取节点套一层高亮材质），可选
     的实例化场。打开的场景目标一直留到 `PostProcessPass` 才关，故天空盒能接在其后。
     注：PBR 着色器静态声明的 shadow/IBL 采样器即使对应子系统未装配仍是活跃的，一个
     活跃但未绑定（或不 complete）的采样器会在 draw 时毒化整个调用（Apple 驱动尤甚），
     故 `EnsureSamplerPlaceholders()` 在 draw 前给这些纹理单元各兜上一个类型正确的 1×1
     stand-in；真实子系统随后重绑同一单元，所以仅在其缺席时生效。
  3. `SkyboxPass`（可选） —— 用 LEQUAL 深度技巧以 HDR 环境立方填上没被不透明几何
     覆盖的像素；不装配它就完全不触碰 `SkyboxRenderer`/`EnvironmentMap`。
  4. `PostProcessPass` —— resolve MSAA，可选的 bright/blur bloom，然后把 ACES
     合成输出到默认帧缓冲（可选：无 post 链时 3D pass 直接渲到窗口，本 pass 不入链）。
  5. `DebugHudPass` —— `DebugDraw` 线框 + `SpriteBatch`/`Font` 的 HUD 文字，并
     收束 `Profiler` 的这一帧。
- `Renderer` 持有有序的 `std::vector<std::unique_ptr<RenderPass>>`；
  `BuildPbrPipeline()` 装上面那套全量演示链（含 `SkyboxPass`），`BuildMinimalPipeline()`
  只装 `Geometry → DebugHud` 以演示"什么都不申请也能出图"；`AddPass()` 是唯一的装配
  原语（builder 只是预设），`Render(frame)` 只是按序跑一遍。`Init()` 设置一次性全局
  GL 状态（深度测试、面剔除）。

### 为什么是线性链，而不是 render graph？

通用的 render graph（瞬态资源、自动 barrier/资源状态管理、sub-pass 合并）只在
你有大量 pass、且在异步 compute 或基于 tile 的 GPU 上有不平凡的读/写依赖时，才
对得起它的复杂度。本管线只是把手上几个长寿命 render target 过几遍固定顺序的
pass。手写这个顺序更短、更好调，也契合 GL 状态的实际用法。`RenderFrame` +
`RenderPass` 已经预留了将来要长成 graph 所需的接缝（每个 pass 在 `Execute` 里
声明自己的活，`Renderer` 负责编排顺序），所以我们在抽象"值回票价"之前先不做它。

## 4. 材质、光照与 UBO

光照与阴影状态通过 **uniform buffer block** 共享：

- `LightingBlock` → 绑定点 **1**，`ShadowBlock` → 绑定点 **2**（分别由
  `src/gldx/light` 与 `src/gldx/shadow` 下的 `LightBuffer` 和 `CascadedShadowMap`
  管理）。
- block 用 `layout(std140)`；C++ 侧的镜像结构体按同样布局打包。
- **GLSL 4.10 不支持在 uniform *block* 上写 `layout(binding=N)`**（那是更晚的
  GLSL 特性），所以绑定点要从 C++ 侧用 `glUniformBlockBinding` /
  `ShaderProgram::SetBlockBinding` 指派。忘了这步是经典的"全黑但无报错"陷阱，
  因此绑定这一步在程序链接时**显式**写出，而不是从着色器里想当然。

材质（`PbrMaterial`）暴露 albedo / metallic-roughness / normal / AO 贴图并带
标量回退，绘制前把自己作为 uniform 应用一遍；PBR 程序从已绑定的 block 读取
光照与阴影，所以材质只设每对象的纹理/uniform 状态。

## 5. 两阶段资源管线（`src/gldx/assets/`）

资源加载被拆成两阶段，使任何 GL 调用都不会离开渲染线程：

- **Stage A（工作线程，无 GL）。** `AssetManager::RequestModel()` /
  `RequestTexture()` 把文件 IO + Assimp/STB 解码排进 `ThreadPool`。future 产出
  的是 CPU 侧描述（`LoadedModelData`、`Texture2DDesc`），包在 `std::expected`
  里。
- **Stage B（渲染线程）。** `AssetManager::ProcessUploads()` 每帧调一次，抽取
  已完成的 Stage-A 结果并执行 GL 上传（`Mesh::Upload`、`Texture2D` 创建）。只有
  在这里才碰 GL 对象。

查询返回一个句柄 / `shared_ptr<const …>`，就绪前给出 `nullptr`，所以调用方永远
不会看到一个半成品 GL 资源。完整保证与确切的同步原语见
[thread-safety.md](thread-safety.md)。

## 6. HDR 后期链（`src/gldx/render/` + `PostProcessShaders.h`）

几何/天空盒/实例化这些绘制把**线性 HDR** 输出到一个 RGBA16F、经 MSAA resolve 的
target。色调映射与 gamma **不在** PBR 着色器里——它们活在那唯一的一次 composite
pass 中，从而让整帧（场景*与*自发光*与*天空盒）被一致地映射：

```
线性 HDR ──► 曝光 ──► ACES filmic ──► gamma(1/2.2) ──► 默认 FBO
       ▲
       └── 叠加式 bloom：bright-pass(阈值) → 可分离模糊 → 混合
```

因为在唯一那次 composite 之前一切都保持线性，bloom 与太阳光盘的表现都符合物理；
关掉 bloom 只是把 bloom 强度置 0，色调映射照跑不误。精灵/文字 HUD 是在 composite
**之后**直接画到默认帧缓冲上的，所以不会被二次色调映射。

## 7. 以 C++20 named module 交付

整个引擎是一个 C++20 **named module `gldx`**，以静态库构建
（`add_library(gldx STATIC)`）。应用对它的唯一耦合，就是 `src/main.cpp` 里的
`import gldx;`——不再逐个 `#include` `gldx/**` 头。这取代了早先的布局：那时
`main.cpp` 文本包含约 27 个引擎头，所有东西直接编进一个可执行文件。

- **主接口** —— `src/gldx/gldx.cppm`：用 `export { #include "..." }` 把每个公共引擎
  头各包一次，于是模块再导出了完整的 API。用单一接口单元（而非每个子系统一个
  partition）契合这套以头为中心的代码库，并避开了跨厂商的 partition 排序隐患。
- **实现单元** —— 那 31 个 `src/gldx/**/*.cpp` 各是一个 `module gldx;` 单元。它们
  通过对主接口的隐式 import 看到全部引擎声明，因此**不带**任何
  `#include "gldx/…"`。
- **global module fragment** —— GLAD、GLM 和常用标准库头都收在 `src/gldx/gmf.hpp`
  里，在每个单元 `module;` 片段的顶部被文本包含。这把 `GLuint` / `glm::vec3`
  等附到*global module*（而非 `gldx`）上，于是导出的签名引用的是 global-module
  类型，而文本包含同样这些头的 `main.cpp` 解析到的也是一模一样的实体，不会撞上
  一个冲突的模块作用域重复声明。
- **第三方策略** —— GLAD + GLM 泄漏进了公共 API，所以它们是 global fragment 的
  include（绝不作为实体被 `export`）。**STB 与 Assimp 仅存在于实现**：它们留在
  各单元自己的片段里（如 `ModelLoader.cpp`、`third_party/stb_image_impl.cpp` 这个
  TU），从不出现在模块接口上。引擎头也不再 `#include` 它们分毫。
- **窗口部分留在模块之外** —— `src/gldx/core/Platform.h`（GLAD-before-GLFW 顺序、
  `GLFW_PLATFORM_*` 宏）刻意只是普通文本 include，因为 `<GLFW/glfw3.h>` 和 `#if` 可见的
  宏没法干净地跨过模块边界。它**不再携带任何窗口/应用常量**（`kApp*`/`kWindow*` 已下沉到各
  demo 自己定义）；引擎自身不建窗。

## 8. 引擎之外的两个应用侧 named module

与引擎同层、但彼此独立的还有两个 named module：`gldxwin`（`namespace gldx::win`，引擎无关
的 GLFW 窗口/帧循环：`App` 单例 + 多窗口 `Window` + `OnCreate`/`OnFrame`/`OnDestroy` 钩子，仅
链 glfw+GLAD，**不** import 任何 `gldx` 类型）与 `gldxcli`（`namespace gldx::cli::Flags`，纯标准
库、零链接依赖）。它们取代了旧的 `src/demo/demo_app.h`/`demo_cli.h` 头文件脚手架。因为
`gldxwin` 引擎无关，`RenderContext::MarkAsRenderThread()` 由 demo 在 `OnCreate` 首行自己调。

构建上，根 `CMakeLists.txt` 只留 orchestrator（project/标准/option → `include(cmake/Dependencies.cmake)`
与 `include(cmake/Assimp.cmake)` 做依赖探测 → `add_subdirectory(src)`）；`src/CMakeLists.txt` 再逐个
`add_subdirectory(gldx/gldxwin/gldxcli)` 并定义 `GLFW_Template` 与 `demo`，每个库/可执行有自己的
`CMakeLists.txt`（递归拆分，目标名不变）。

因为较新的标准库不再传递性地带上 `<ostream>`/`<cstdint>` 这类头，内置的 Assimp
构建会被强制包含其遗留 contrib 源码所缺的那几个头（见 `cmake/Assimp.cmake` 里的
`assimp` 块）——我们自己的源码则保持诚实、显式的 include。

## 9. OpenGL 调用的封装边界

“封装 OpenGL”不等于“给每个 `gl*` 都套一层方法”。本项目按**语义归属**决定封不封，
一条清晰的界线：

- **句柄对象的生命周期与对它自身的操作 → 封进对应的 RAII 包装类。** 一个 GL 对象
  （VAO/VBO/UBO/纹理/FBO/采样器）的生成、绑定、上传、删除，都收在 `core/`（及
  `render/Framebuffer`）里那几个 owning 类型内部，`RenderContext` 守卫渲染线程。
- **绘制原语 → 收进 `VertexArray`。** `glDrawArrays` / `glDrawElements` /
  `glDrawElementsInstanced` 的语义是“用当前绑定的这个 VAO 画”，天然属于 VAO。故
  `VertexArray` 提供 `DrawArrays` / `DrawElements` / `DrawElementsInstanced` 三个薄成
  员（不碰绑定，`Bind`/`Unbind` 仍归调用方）。全仓库的 `glDraw*` 只出现在
  `VertexArray.cpp` 一处——`Mesh`/`InstancedMesh`/`VoxelMeshGpu`/各 batch 内部、demo
  与 `src/main.cpp` 的 `TrianglePass` 都改走它，绘制因此有了单一真源，demo/app 层不
  再有任何裸 `glDraw`。
- **帧级 / pass 级状态 → 刻意保持裸调。** `glViewport` / `glBindFramebuffer` /
  `glClearColor` / `glClear` **不**封。它们作用于“这一帧绑到哪张目标、视口多大、怎
  么清”，语义属于**某个 pass 自身**而非任一 GL 句柄对象。`Renderer` 被刻意定位为“只
  管一次性全局状态 + pass 排序”，清屏下沉在各 pass 的 `Execute` 里（见 §3）：阴影 pass
  清的是级联深度数组的某一层、后期 pass 清的是离屏 target、`DebugHudPass` 反而**不能
  清**——把它们统一上收进 `Renderer` 既不可能语义正确、也只会得到一组 `glClear` 的转发
  壳，正是本项目红线要避开的“透传包装反模式”。离屏目标需要时由 `Framebuffer` 就近提供
  `Viewport`/`ClearColor`/`ClearDepth`，够用即止、不再多加壳。

一句话：**能表达“是谁的 GL 状态”的就封进那个对象，只属于“本 pass 这一帧怎么画”的保
持开放。** 这与 `gldxwin` 对 GLFW 的“薄适配器 + 选择性封装”是同一套判断（见 §8）。

## 10. 仓库布局

§1 的分层说的是**依赖方向**，目录则按“一个目录一个概念、一个 demo 一个可执行”组织。机器可读的版本在仓库根 [AGENTS.md](../../AGENTS.md) 的“文件地图”（比这份更细，带逐文件的约定），这里是给人读的全景：

```
.
├── cmake/                   # CMake 辅助模块（由根 CMakeLists include）：Dependencies.cmake(glfw/glad/glm/stb 探测) · Assimp.cmake(assimp 子项目)
├── src/                      # 三个库 + 全部可执行，各自独立 CMakeLists，经根 add_subdirectory(src) 递归编排
│   ├── CMakeLists.txt        #   编排：add_subdirectory(gldx/gldxwin/gldxcli) + GLFW_Template 可执行 + demo
│   ├── main.cpp              # 最简 hello-triangle：import gldx/gldxwin/gldxcli，自定义 RenderPass + 内联 GLSL + 高层 Mesh 直渲窗口（最小实现基线，目标 GLFW_Template）
│   ├── gldx/                 # 引擎子系统（分层，仅向下依赖），整体编译为 named module `gldx`（静态库）
│   │   ├── CMakeLists.txt    #   add_library(gldx STATIC) + FILE_SET CXX_MODULES(gldx.cppm) + PUBLIC 传递 glfw/glad/glm
│   │   ├── gldx.cppm         #   primary interface：export { #include } 聚合全部公共头
│   │   ├── gmf.hpp           #   共享 global module fragment（GLAD/GLM/std 预包含）
│   │   ├── core/             #   Platform.h(窗口/GLFW) · RenderContext(线程亲和)/GLBuffer/VertexArray/UBO/Sampler/Framebuffer
│   │   ├── geometry/         #   Mesh / InstancedMesh / GeometryFactory
│   │   ├── texture/          #   Texture2D / Texture2DArray / TextureCubeMap / RenderTexture
│   │   ├── material/         #   PbrMaterial
│   │   ├── shader/           #   ShaderProgram + 内嵌 GLSL（ShaderLib/… Shaders.h，单一真源）
│   │   ├── camera/ light/ shadow/   # Camera(orbit+飞行) / VoxelRay(DDA) / LightBuffer(UBO) / CascadedShadowMap / EnvironmentMap
│   │   ├── voxel/            #   BlockRegistry / Chunk / ChunkMesher / VoxelMeshGpu（纯 CPU 网格化 + 上传记录）
│   │   ├── util/             #   Noise（Perlin 2D/3D + fBm，确定性、无全局状态）
│   │   ├── scene/            #   Scene / SceneNode / Transform（纯 CPU 层级）
│   │   ├── render/           #   Renderer / RenderPass / RenderFrame / 后期链 / SpriteBatch / TextRenderer / ParticleBatch / Voxel 两 pass
│   │   ├── text/ assets/ debug/     # 字体·异步资源·DebugDraw/Profiler/Picking/Frustum
│   │   ├── ...               #   引擎各 .cpp 均为 `module gldx;` 实现单元
│   │   └── third_party/      #   stb_image_impl.cpp（唯一第三方实现 TU，非模块接口）
│   ├── gldxwin/              # 引擎无关窗口库：named module `gldxwin`（gldx::win::App 单例 + 多窗口 Window + 帧循环），仅链 glfw+GLAD，不 import gldx
│   │   ├── CMakeLists.txt    #   add_library(gldxwin STATIC App.cpp Window.cpp)
│   │   ├── gldxwin.cppm      #   App / Window / WindowDesc / FrameInfo / RunOptions（OnCreate/OnFrame/OnDestroy 钩子）
│   │   ├── App.cpp / Window.cpp / gmf.hpp
│   ├── gldxcli/              # 命令行开关库：named module `gldxcli`（gldx::cli::Flags），纯标准库、零链接依赖（原 demo_cli.h 迁入）
│   │   ├── CMakeLists.txt    #   add_library(gldxcli STATIC gldxcli.cpp)
│   │   ├── gldxcli.cppm / gldxcli.cpp
│   ├── demo/                 # 一个功能一个入门 demo；每个含 main.cpp 的子目录被 add_subdirectory 自动收为一个可执行（import gldx/gldxwin/gldxcli）
│   │   ├── CMakeLists.txt    #   add_gldx_demo(<name>) + GLOB 遍历含 main.cpp 的子目录（新增 demo 免改 CMake）
│   │   ├── pbr_showcase/     #   成品演示：HDR/PBR/CSM/IBL/Bloom/天空盒/实例化/HUD 全开的交互场景（main + Input.{h,cpp} + Scene.{h,cpp}）
│   │   ├── hello_triangle/   #   基线对照 demo：与 src/main.cpp 同一个三角形，但手搭 VertexArray/GLBuffer/AttachAttribute（Mesh 底层发生了什么）
│   │   ├── voxel_terrain/    #   成品演示：可玩体素世界（main + World.{h,cpp} + Input.{h,cpp} + Hud.{h,cpp} + Streaming.{h,cpp}，世界层在 demo 侧）
│   │   ├── glass_water/      #   成品演示：自由飞行的玻璃/水面场景，五层透明状态机（main + Scene.{h,cpp} + Input.{h,cpp} + BlendPass.{h,cpp} + Hud.{h,cpp} + Shaders.h），四道正确性闸门各自可关，材质是电介质 Cook-Torrance，`Tab` 切透视/正交
│   │   ├── pbr_lighting/ shadow_csm/ ibl_environment/ postprocess_bloom/ skybox/   # 各渲染子系统单功能 demo
│   │   ├── instancing/ particles/ text_hud/ camera_picking/ debug_draw/            # 各能力单功能 demo
│   │   ├── geometry_upload/ model_loading/                                       # 几何上传 / 异步模型 demo
│   │   ├── texture_samplers/ #   采样器 demo（main + SamplerPass.{h,cpp}：VertexArray/GLBuffer/Sampler RAII，无裸 GL 脚手架）
│   │   ├── textured_sphere/  #   贴图球体 demo：一条索引流、两种细分——CPU 侧 `segments`/`rings` 实时重网格，同一份 MeshData 以 `Mesh::DrawAs(GL_PATCHES)` 送进 TCS/TES 做硬件细分；三张程序化 equirectangular 贴图免外部文件
│   │   ├── shader_file/      #   从磁盘加载 GLSL（CreateFromFiles 两文件版）：坏路径 exit 1、无路径退回内嵌源
│   │   ├── geometry_shader/  #   选择性装配几何阶段：CreateFromSources({Vertex,Geometry,Fragment})，喂 GL_POINTS 扩成方块阵
│   │   ├── geometry_shader_file/ #  多阶段从文件加载：CreateFromFiles({{Vertex},{Geometry},{Fragment}}) 从磁盘装配含几何阶段的程序（无路径时退回内嵌源）
│   │   ├── shader_stages/    #   全 5 阶段集成：CreateFromSources 装配 Vertex+TessControl+TessEval+Geometry+Fragment，画 GL_PATCHES 细分线框（main + Stages.h）
│   │   ├── menger_sponge/    #   着色阶段生成几何：`gl_VertexID` 做 20 进制解码（`uLevel` 就是细分层级，调档不重建）+ transform feedback 自细分队列（GPU 决定还有多少块要分）（main + MengerShaders.h）
│   │   ├── multi_viewport/   #   多窗口同步视图：每窗独立 context 串行重渲同一场景（main 接线 + SharedState.{h,cpp} + View.{h,cpp}，atomic 共享态 + 后台 worker，线程/上下文安全验收场）
│   │   ├── render_passes/    #   多窗口 + for 循环装配不同 RenderPass 子类：ClearPass + Triangle/Quad/Line/Point（main + SharedState.{h,cpp} + Passes.{h,cpp} + View.{h,cpp}，一表多态、各窗独立 context 同步时钟）
│   │   └── multi_window_levels/  # 多窗口的同步↔异步、per-window 剔除、LOD/视距分档、完全无关的独立 context：五个“等级”窗（main + Profiles.{h,cpp} + SharedState.{h,cpp} + View.{h,cpp}）
│   └── assets/               # 内容资源（不被编译）：GLSL 参考镜像 + 模型投放目录，与 gldx 代码同级
│       ├── shaders/          #   内嵌 GLSL 的只读参考镜像（不被编译/加载）
│       └── models/           #   FBX/OBJ/glTF 投放目录（运行期经 AssetManager 异步加载）
├── doc/                      # 使用者(user/，含 Demo 画廊) + 开发者(developer/: 设计+线程安全+发布)；agent 速查见根 AGENTS.md
├── third_party/
│   ├── glad/                 # 子模块：glad2 生成器（OpenGL 4.1 Core 绑定）
│   ├── stb/                  # 子模块：header-only 图像/字体
│   └── assimp/               # 子模块：模型导入（源码内置构建）
├── scripts/                  # build.sh / run.sh / clean.sh（由 CMake 生成）+ release.sh（版本号单源同步）
├── .github/workflows/        # 手动触发的三平台构建 + 发布流水线（见 release.md）
└── CMakeLists.txt            # 根：~60 行 orchestrator（project/标准/option → include(cmake/) → add_subdirectory(src) → 脚本生成 + Summary）
```

逐 demo 演示什么、仍回 [使用者文档的 Demo 画廊](../user/10-demos.md)。
