# 设计

本文说明 `gfx` 这一层是如何组织、以及为什么这样组织。文中引用的文件名与符号
都真实存在于 `src/gfx/**`。

## 1. 分层的子系统

引擎是 `src/gfx/` 下的一组小子系统，每个只负责一个概念。依赖方向**只向下**——
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

## 2. 场景图（`src/gfx/scene/`）

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

## 3. 渲染管线（`src/gfx/render/`）

一条**线性 pass 链**，不是完整的 render graph。

- `RenderFrame` 是逐帧的值类型结构，用指针/引用打包各 pass 所需的一切
  （相机、视锥、场景、后期链、光照 buffer、阴影图、环境贴图、PBR/depth/skybox/
  instanced 着色器程序、各开关、被拾取的节点、HUD 资源、性能分析器，以及若干
  输出计数如 `visibleCount`）。它不拥有任何 GL 资源——只是"这一帧的输入"。
- `RenderPass` 是抽象基类：`virtual void Execute(RenderFrame&) = 0;` 外加一个
  `name`。它不可拷贝。
- 具体 pass（在 `RenderPasses.h/.cpp`），按序执行：
  1. `ShadowPass` —— 更新 CSM，逐层联级把投影者渲进深度数组。
  2. `GeometryPass` —— 把 HDR 场景渲进 MSAA target：绑定 UBO/阴影/环境，绘制
     经视锥剔除的可渲染物（对被拾取节点套一层高亮材质），可选的实例化场，最后
     画天空盒。
  3. `PostProcessPass` —— resolve MSAA，可选的 bright/blur bloom，然后把 ACES
     合成输出到默认帧缓冲。
  4. `DebugHudPass` —— `DebugDraw` 线框 + `SpriteBatch`/`Font` 的 HUD 文字，并
     收束 `Profiler` 的这一帧。
- `Renderer` 持有有序的 `std::vector<std::unique_ptr<RenderPass>>`；
  `BuildDefaultPipeline()` 装上上面四个 pass，`AddPass()` 让应用自定义，
  `Render(frame)` 只是按序跑一遍。`Init()` 设置一次性全局 GL 状态（深度测试、
  面剔除）。

### 为什么是线性链，而不是 render graph？

通用的 render graph（瞬态资源、自动 barrier/资源状态管理、sub-pass 合并）只在
你有大量 pass、且在异步 compute 或基于 tile 的 GPU 上有不平凡的读/写依赖时，才
对得起它的复杂度。本管线只是把手上几个长寿命 render target 过四遍固定顺序的
pass。手写这个顺序更短、更好调，也契合 GL 状态的实际用法。`RenderFrame` +
`RenderPass` 已经预留了将来要长成 graph 所需的接缝（每个 pass 在 `Execute` 里
声明自己的活，`Renderer` 负责编排顺序），所以我们在抽象"值回票价"之前先不做它。

## 4. 材质、光照与 UBO

光照与阴影状态通过 **uniform buffer block** 共享：

- `LightingBlock` → 绑定点 **1**，`ShadowBlock` → 绑定点 **2**（分别由
  `src/gfx/light` 与 `src/gfx/shadow` 下的 `LightBuffer` 和 `CascadedShadowMap`
  管理）。
- block 用 `layout(std140)`；C++ 侧的镜像结构体按同样布局打包。
- **GLSL 4.10 不支持在 uniform *block* 上写 `layout(binding=N)`**（那是更晚的
  GLSL 特性），所以绑定点要从 C++ 侧用 `glUniformBlockBinding` /
  `ShaderProgram::SetBlockBinding` 指派。忘了这步是经典的"全黑但无报错"陷阱，
  因此绑定这一步在程序链接时**显式**写出，而不是从着色器里想当然。

材质（`PbrMaterial`）暴露 albedo / metallic-roughness / normal / AO 贴图并带
标量回退，绘制前把自己作为 uniform 应用一遍；PBR 程序从已绑定的 block 读取
光照与阴影，所以材质只设每对象的纹理/uniform 状态。

## 5. 两阶段资源管线（`src/gfx/assets/`）

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

## 6. HDR 后期链（`src/gfx/render/` + `PostProcessShaders.h`）

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

整个引擎是一个 C++20 **named module `gfx`**，以静态库构建
（`add_library(gfx STATIC)`）。应用对它的唯一耦合，就是 `src/main.cpp` 里的
`import gfx;`——不再逐个 `#include` `gfx/**` 头。这取代了早先的布局：那时
`main.cpp` 文本包含约 27 个引擎头，所有东西直接编进一个可执行文件。

- **主接口** —— `src/gfx/gfx.cppm`：用 `export { #include "..." }` 把每个公共引擎
  头各包一次，于是模块再导出了完整的 API。用单一接口单元（而非每个子系统一个
  partition）契合这套以头为中心的代码库，并避开了跨厂商的 partition 排序隐患。
- **实现单元** —— 那 31 个 `src/gfx/**/*.cpp` 各是一个 `module gfx;` 单元。它们
  通过对主接口的隐式 import 看到全部引擎声明，因此**不带**任何
  `#include "gfx/…"`。
- **global module fragment** —— GLAD、GLM 和常用标准库头都收在 `src/gfx/gmf.hpp`
  里，在每个单元 `module;` 片段的顶部被文本包含。这把 `GLuint` / `glm::vec3`
  等附到*global module*（而非 `gfx`）上，于是导出的签名引用的是 global-module
  类型，而文本包含同样这些头的 `main.cpp` 解析到的也是一模一样的实体，不会撞上
  一个冲突的模块作用域重复声明。
- **第三方策略** —— GLAD + GLM 泄漏进了公共 API，所以它们是 global fragment 的
  include（绝不作为实体被 `export`）。**STB 与 Assimp 仅存在于实现**：它们留在
  各单元自己的片段里（如 `ModelLoader.cpp`、`third_party/stb_image_impl.cpp` 这个
  TU），从不出现在模块接口上。引擎头也不再 `#include` 它们分毫。
- **窗口部分留在模块之外** —— `src/gfx/core/Platform.h`（GLAD-before-GLFW 顺序、
  `GLFW_PLATFORM_*` 宏、`gfx::kApp*`/`kWindow*` 常量）刻意只是给 `main.cpp` 用的
  普通文本 include，因为 `<GLFW/glfw3.h>` 和 `#if` 可见的宏没法干净地跨过模块
  边界。

因为较新的标准库不再传递性地带上 `<ostream>`/`<cstdint>` 这类头，内置的 Assimp
构建会被强制包含其遗留 contrib 源码所缺的那几个头（见 `CMakeLists.txt` 里的
`assimp` 块）——我们自己的源码则保持诚实、显式的 include。
