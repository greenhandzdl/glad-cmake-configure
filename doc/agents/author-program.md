# Agent 运行手册 · 替用户写 / 改程序

面向 **AI coding agent**：环境已按 [`setup-environment.md`](setup-environment.md) 搭好后，用户要"用这个引擎写个程序 / 加个物体 / 改渲染"时照本手册做。权威示例永远是仓库里的 demo：最小基线 [`../../src/main.cpp`](../../src/main.cpp)（hello-triangle）、成品 [`../../src/demo/pbr_showcase/main.cpp`](../../src/demo/pbr_showcase/main.cpp)，以及 [`../../src/demo/`](../../src/demo/) 下一个功能一个入门 demo——**拿不准就照抄它们的用法**。人类向教程在 [../user/](../user/README.md)：骨架见 [① 核心骨架](../user/2-core-setup.md)，各 API 主题分章（[② 几何场景](../user/3-geometry-scene.md)/[③ 资源加载](../user/4-assets-loading.md)/[④ 光照 UBO](../user/5-lighting-ubo.md)/[⑤ 相机拾取](../user/6-camera-picking.md)/[⑥ 体素世界](../user/7-voxel-basics.md)），扩展见 [🔴 进阶](../user/8-advanced.md)。

---

## 1. 消费模型：一个可执行怎样用上 `gldx`

应用侧共三样东西，都是 **C++20 named module**（静态库）：引擎 `gldx`、引擎无关的窗口库 `gldxwin`、命令行开关库 `gldxcli`。任何消费者只需：

```cpp
#include "gldx/core/Platform.h"   // 文本 include：GLAD-before-GLFW 顺序 + GLFW_PLATFORM_* 宏（要在自己代码里调 glfw* 就需要它；无窗口/应用常量）
import gldx;                      // 整个引擎的公共接口；不要再 #include 任何 gldx/**.h
import gldxwin;                   // gldx::win::App / Window：建窗 + GL 4.1 上下文 + 帧生命周期（引擎无关，不 import 任何 gldx 类型）
import gldxcli;                   // gldx::cli::Flags：--quit-after/--on/--off/--help
#include <glm/glm.hpp>           // glm::vec3/mat4、GLuint 属 global module，用它们要自行 include
```

CMake（在**同一仓库**里加你自己的可执行目标；`gldx`/`gldxwin`/`gldxcli` 三个 target 已由 [`../../src/CMakeLists.txt`](../../src/CMakeLists.txt) 下的各自目录定义）：

**首选：放进 demo 目录，免写任何 CMake**。在 `src/demo/{feature}/` 放一个 `main.cpp`，`src/demo/CMakeLists.txt` 的 `add_subdirectory` 遍历会自动 `add_gldx_demo({feature})` 收为一个可执行（`d_{feature}` → `OUTPUT_NAME {feature}` → 落 `output/{feature}`、链接 `gldx`/`gldxwin`/`gldxcli`），重配即生效，无需改任何 CMake。demo 里 `import gldx; import gldxwin; import gldxcli;`：用 `gldx::win::Window window(desc)`（`WindowDesc` 给尺寸/标题）拿到建窗 / GL 4.1 上下文 / gladLoadGL，在 `window.OnCreate(...)` 首行调 `gldx::RenderContext::MarkAsRenderThread()` 并建 `gldx::Renderer`，`window.OnFrame(...)` 里逐帧填 `RenderFrame` 后 `renderer.Render(frame)`，`window.OnDestroy(...)` 里释放 GL 资源（早于 `glfwDestroyWindow`、上下文仍当前），末尾 `return gldx::win::App::Get().Run({flags.quitAfter()})` 驱动帧循环；用 `gldx::cli::Flags`（`import gldxcli`）拿 `--quit-after/--on/--off/--help`。

若要在别处独立建一个目标（不想进 `src/demo/` 遍历），手写即可：

```cmake
add_executable(my_app src/my_app.cpp)
target_link_libraries(my_app PRIVATE gldx gldxwin gldxcli)   # 三者各以 PUBLIC 传递 glfw/glad/glm 与 src 头路径；只要引擎不需窗口/CLI 时可只链 gldx
```

> 把 `gldx`/`gldxwin`/`gldxcli` 当依赖**链接**即可，**别**再手写 `find_package(OpenGL)` 或手动 include 引擎头。GL 依赖由 `gldx`/`gldxwin`→`glfw`/`glad` 传递。

---

## 2. 管线是"按需装配"，不是必须全家桶

`Renderer` 给两个预设：`BuildPbrPipeline()` 装演示用的全量链 `Shadow → Geometry → Skybox → PostProcess → DebugHud`；`BuildMinimalPipeline()` 只装 `Geometry → DebugHud`。二者都只是 `AddPass()` 之上的一行预设，你完全可以自己拼。

**`GeometryPass::Execute` 只在渲染前无条件要求这四项**（缺一即 `return`，不崩）：

```
f.camera  f.lights  f.pbr  f.scene
```

其余子系统都是**可选**，各自停在守卫后面，应用没带就不参与：

- `f.post`（`PostProcessChain*`）：**可空**。为空时几何 / 体素 pass 直接绑定默认帧缓冲、自行 `glClear` 后渲到窗口——不必为了出图去构造整条 HDR/MSAA/泛光链。
- `f.shadow`（`{map,depth,sunToward,enabled}`）、`f.sky`（`{box,env}`）、`f.ibl.enabled`、`f.instances`、`f.overlay`、`f.particles`：整块缺省构造 = 该效果本帧不存在。着色器开关取的是 `f.shadow.enabled && f.shadow.map` 这类"数据与开关都在"的组合，所以光置空指针不会误采样。

因此写新程序有三条正道，按投入从轻到重：

### 路线 A（推荐：要 PBR/阴影/泛光）：以 `src/main.cpp` 为骨架，换内容

直接复制 [`../../src/main.cpp`](../../src/main.cpp) 的**生命周期骨架**（见 §3），只改"造物体"的部分。用到的子系统才建对象并填进 `RenderFrame` 对应记录，用不到的效果把该记录的 `enabled` 留 `false`（或指针留空）即可，**不必为"怕缺字段崩"而构造你根本不用的链**。

### 路线 B（只要出图 / 自定义效果）：`BuildMinimalPipeline()` + 空 `post`

```cpp
gldx::Renderer renderer; renderer.Init(); renderer.BuildMinimalPipeline();
// 每帧 frame 只需 camera/lights/pbr/scene + fbWidth/fbHeight；frame.post = nullptr。
gldx::RenderFrame frame;
frame.camera = &camera; frame.lights = &lightBuffer; frame.pbr = &*pbr; frame.scene = &scene;
frame.viewProj = viewProj; frame.fbWidth = w; frame.fbHeight = h;   // post 与各可选记录全缺省
renderer.Render(frame);   // 几何 pass 直渲窗口，无需 shadow/sky/post
```

注意：这条路径不经 ACES/伽马合成，出图色彩空间即着色器输出。PBR 着色器要正确出图仍需 `LightingBlock` UBO 已 `SetBlockBinding`（见 §4）；若想更纯粹，配一个自带简单着色器的自定义 pass（路线 C）。

### 路线 C（真正 hello-triangle / 加后处理叠加）：自定义 `RenderPass`，两个 builder 都不调

`RenderPass` 基类只有一个纯虚函数（`src/gldx/render/RenderPass.h`）：

```cpp
class MyPass : public gldx::RenderPass {
public:
    MyPass() : gldx::RenderPass("MyPass") {}
    void Execute(gldx::RenderFrame& f) override {   // 只在渲染线程被调用（Renderer::Render 保证）
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glClearColor(0.02f, 0.02f, 0.03f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        // 用你自己的 ShaderProgram + Mesh 画东西；此处 GL 全部合法（渲染线程）
    }
};
gldx::Renderer renderer; renderer.Init();
renderer.AddPass(std::make_unique<MyPass>());      // 只跑你的 pass，连 GeometryPass 都不需要
renderer.Render(frame);                            // frame 可极简（你的 pass 用啥填啥）
```

---

## 3. 生命周期骨架（务必照抄这个结构）

窗口生命周期整个交给 `gldxwin`：`App` 单例管 `glfwInit`/`glfwTerminate` + GL 4.1 core hints（含 macOS forward-compat），`Window` 构造时管建窗 + `glfwMakeContextCurrent` + `gladLoadGL`，`App::Run` 管帧循环。你只提供帧回调并保证**析构顺序**。下面就是 `src/main.cpp` 验证过的形状：

```cpp
int main(int argc, char** argv) {
    const gldx::cli::Flags flags(argc, argv, /*features*/ {}, /*options*/ {"yaw","pitch"}, "my_app");
    if (flags.wantsHelp()) { flags.printUsage(); return 0; }

    gldx::win::WindowDesc desc;                 // 尺寸/标题，缺省 800×600 / "gldx"
    desc.title = "my_app";
    gldx::win::Window window(desc);             // 构造即建窗 + MakeContextCurrent + gladLoadGL
    if (!window.Ok()) return 1;                 // 建窗失败退化为"没东西可画"，不崩

    gldx::RenderContext::MarkAsRenderThread();  // 声明本线程是唯一渲染线程（gldxwin 引擎无关，这行归你）

    // 关键：把所有 GL 资源 owner 声明在 window 之后，于是它们在 window 析构
    // （-> OnDestroy -> glfwDestroyWindow）之前先析构，此时 context 仍 current。
    gldx::Renderer renderer;                    // owner 在 window 之后 => 先于 window 销毁
    renderer.Init();
    renderer.BuildPbrPipeline();
    // ... 建着色器/子系统/场景；要用原生窗口装输入回调：window.Handle() 拿 GLFWwindow* ...

    window.OnFrame([&](const gldx::win::FrameInfo& info) {
        gldx::RenderFrame frame;
        frame.fbWidth = info.fbWidth; frame.fbHeight = info.fbHeight;
        frame.smoothedFps = info.smoothedFps;   // + camera/lights/pbr/scene 等每帧填
        renderer.Render(frame);
    });

    return gldx::win::App::Get().Run({flags.quitAfter()});   // 驱动帧循环，全部窗口关闭/到点即返回 0
}
```

`Window` 另提供 `OnCreate`/`OnDestroy` 钩子（都在渲染线程、上下文当前时跑，`OnDestroy` 早于 `glfwDestroyWindow`）：多窗口或想把 GL 资源的建/拆严格收进钩子时用它们更清晰；单窗口下照上例把 owner 声明在 `window` 之后即可，效果等价。`App::Run` 内部逐窗 `glfwPollEvents`→回调→`glfwSwapBuffers`，默认 Esc 关窗（`window.SetCloseOnEsc(false)` 可关），`quitAfterSeconds`（来自 `flags.quitAfter()`）到点自动关所有窗。

**别**把 `Mesh`/`Texture2D`/`ShaderProgram` 等 owner 泄漏到 `glfwTerminate()` 之后析构——那会在丢上下文后发 GL 删除调用。`App` 的静态析构在所有 `Window`（main 里构造的）销毁之后才 `glfwTerminate`，所以只要 owner 与 `Window` 同作域且声明在其后即可。

---

## 4. 已验证 API 速查（签名取自源码，勿臆造）

| 目的 | 调用 | 备注 |
| --- | --- | --- |
| 着色器 | `gldx::ShaderProgram::CreateFromSource(vs, fs)` → 有 `operator bool`/`.error()` | 常量在 `gldx::shaders::kPbrVertex/kPbrFragment/kDepthVertex/kDepthFragment/kInstancedVertex/kInstancedFragment` |
| UBO 绑定 | `prog->SetBlockBinding("LightingBlock", gldx::LightBuffer::kBinding)`；`"ShadowBlock", gldx::CascadedShadowMap::kShadowBinding` | **链接后设一次**；不绑=全黑无报错 |
| 采样器 | `prog->Set("uShadowMap", (int)gldx::texunit::shadowArray)` 等 | `texunit::{shadowArray,irradiance,prefilter,brdfLut,skybox}` |
| 几何 | `gldx::GeometryFactory::Cube(size)` / `Sphere(radius,segments,rings)` / `Plane(halfExtent)` → `gldx::MeshData` | |
| 上传网格 | `gldx::Mesh m; m.Upload(std::move(data));` | 仅渲染线程；move-only |
| 材质 | `gldx::PbrMaterial{ baseColor(vec4), metallic, roughness, albedo(Texture2D*), ... }` | `albedo` 可空 |
| 变换 | `gldx::Transform t; t.translation/t.scale; t.SetAxisAngle(axis, rad);` | |
| 场景 | `gldx::Scene s; auto& n=s.CreateRoot(t);` `parent->AddChild(t)` `n.SetRenderable(&mesh,&mat)` `n.SetLocalBounds(c,r)` `n.castsShadow` `s.Update()` | 节点存**非拥有**指针 |
| 相机 | `gldx::Camera c; c.SetPerspective(fov,aspect,near,far); c.LookAt(eye,target,up); c.SetViewportAspect(a); c.ViewProjection(); c.Position();` | |
| 视锥剔除 | `gldx::Frustum fr; fr.Extract(viewProj);` | GeometryPass 用它剔可见 |
| 后期 | `gldx::PostProcessChain post; post.Init(); post.SetExposure(x);` | |
| 阴影 | `gldx::CascadedShadowMap csm; csm.Init(2048);` | |
| 环境/天空 | `gldx::EnvironmentMap env; env.Generate(travel,256,32,256);` `gldx::SkyboxRenderer sky; sky.Init();` | |
| 光照 | `gldx::LightBuffer lb; lb.Init(); gldx::LightSetup s; s.sun.{direction,color,intensity}; s.ambient; lb.Update(s, camPos);` | |
| 实例化 | `gldx::InstancedMesh im; if (!im.Create(std::move(geo), std::move(insts))) {...}` | **成员函数非静态**，返回 `bool`；`gldx::Instance{model(mat4),color(vec4)}` |
| 拾取 | `gldx::PickRay(px,py,fbw,fbh,invViewProj)` → `gldx::Ray`；`gldx::PickNearest(ray, spheres)`→index | 像素是 framebuffer 坐标（Retina 下先从 GLFW 窗口坐标乘 scale）；`spheres: vector<pair<vec3,float>>`，取自 `scene.PickTargets()` |
| 体素碰撞 | `gldx::VoxelMoveResult r = gldx::MoveVoxelAabb(feet, gldx::VoxelBody{radius,height}, delta, solidFn)` | 纯 CPU。`feet`=脚底中心（盒占 `x±radius × [y, y+height] × z±radius`）；逐轴解算→贴墙滑行；`r.grounded`=下落被拦。**单帧 delta 别过一格**，否则调用方子步进（见 `src/demo/voxel_terrain/main.cpp` 按 ≤ 0.5 格切）；`VoxelAabbSolid(pos,body,fn)` 做纯包含测试 |
| 窗口 | `gldx::win::WindowDesc{width,height,title,resizable}`（缺省 800×600/"gldx"/true）；`Window(desc)` 后 `Ok()`/`Handle()`/`OnCreate`/`OnFrame`/`OnDestroy`/`SetCloseOnEsc`；`App::Get().Run({quitAfterSeconds})` | 来自 `gldxwin`；引擎不再提供任何窗口/应用常量（`Platform.h` 只剩 `GLFW_PLATFORM_*` 宏），尺寸/标题写进 `WindowDesc`，应用身份串由各 demo 自己定 |

---

## 5. 异步加载模型 / 贴图（两阶段；就绪前是空的）

```cpp
gldx::AssetManager assets;                 // 内含线程池
assets.RequestTexture("albedo", "src/assets/models/robot/albedo.png");   // 签名是 (key, path)
assets.RequestModel  ("robot",  "src/assets/models/robot.fbx");          // 重复 key 会被忽略
while (running) {
    assets.ProcessUploads();              // 每帧一次，在渲染线程把后台解码结果上传
    if (auto t = assets.GetTexture("albedo")) { /* shared_ptr<Texture2D>，用它 */ }
    // 就绪前 Get* 返回 nullptr：解在后台线程，上传在渲染线程 —— 绝不能在工作线程发 gl*
}
```

投放目录是 [`../../src/assets/models/`](../../src/assets/models/)（FBX/OBJ/glTF）。两条已验证的拒收规则：材质里的纹理引用必须解析在模型自己目录内（绝对路径/`..` 被 `ModelLoader` 拒，stderr 一行诊断）；图片边长超 `kMaxTextureSide`（16384）在解码前读头部即拒（防解压炸弹），错误走 `std::expected` 串。线程与阶段不变量见 [../developer/thread-safety.md](../developer/thread-safety.md)。

> 模型导入背后只有一个重依赖 assimp，由 CMake 选项 `GLDX_ENABLE_ASSIMP`（默认 `ON`）控制。`OFF` 时 `gldx` 不带 assimp 构建，`RequestModel`/`GetModel` 签名不变但永远拿不到模型（`Load` 返回 `std::unexpected` 并一行 stderr 提示），纹理/体素/几何/PBR 不受影响。

---

## 6. 改着色器：只有一个真源

GLSL **内嵌**在 `src/gldx/shader/*Shaders.h`（`ShaderLib.h`/`PostProcessShaders.h`/`IblShaders.h`）——**只改这里**。`src/assets/shaders/` 是**只读镜像，不编译不加载**，改它无效果。

---

## 7. 交付前自检（对用户的每次代码改动）

```bash
cmake --build build 2>&1 | grep -E 'src/(main|gldx)' | grep -iE 'warning|error'   # 期望：空（自有代码零告警）
./output/<你的可执行> >/tmp/o 2>/tmp/e & p=$!; sleep 6; kill $p 2>/dev/null; wc -c /tmp/e   # 期望：stderr 0 字节
```

- 全黑且无报错 → 十有八九漏了 §4 的 `SetBlockBinding`。
- 崩在 `AssertRenderThread` → 你在非渲染线程碰了 GL（或 owner 在 `glfwTerminate` 后才析构）。
- 建不出窗（`window.Ok()` 为 false）→ 多为 `glfwInit` 失败或拿不到 GL 4.1 core 上下文；GL 4.1 core hints + macOS forward-compat 已由 `gldxwin` 的 `App` 统一设好，无需自设（§3）。

硬约束（GL 单线程、move-only RAII、`std::expected` 不跨线程抛、场景持非拥有指针、依赖只向下）动手前先读 [../developer/thread-safety.md](../developer/thread-safety.md) 与仓库根 [`../../AGENTS.md`](../../AGENTS.md) §硬约束。
