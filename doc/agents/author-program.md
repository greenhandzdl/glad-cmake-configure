# Agent 运行手册 · 替用户写 / 改程序

面向 **AI coding agent**：环境已按 [`setup-environment.md`](setup-environment.md) 搭好后，用户要"用这个引擎写个程序 / 加个物体 / 改渲染"时照本手册做。权威示例永远是 [`../../src/main.cpp`](../../src/main.cpp)——**拿不准就照抄它的用法**。人类向教程在 [../user/](../user/README.md)：骨架见 [① 核心骨架](../user/2-core-setup.md)，各 API 主题分章（[② 几何场景](../user/3-geometry-scene.md)/[③ 资源加载](../user/4-assets-loading.md)/[④ 光照 UBO](../user/5-lighting-ubo.md)/[⑤ 相机拾取](../user/6-camera-picking.md)/[⑥ 体素世界](../user/7-voxel-basics.md)），扩展见 [🔴 进阶](../user/8-advanced.md)。

---

## 1. 消费模型：一个可执行怎样用上 `gfx`

引擎是名为 `gfx` 的 **C++20 named module**（静态库）。任何消费者只需三件事：

```cpp
#include "gfx/core/Platform.h"   // 文本 include：GLAD-before-GLFW 顺序 + GLFW_PLATFORM_* 宏 + 窗口常量
import gfx;                      // 整个引擎的公共接口；不要再 #include 任何 gfx/**.h
#include <glm/glm.hpp>           // glm::vec3/mat4、GLuint 属 global module，用它们要自行 include
```

CMake（在**同一仓库**里加你自己的可执行目标；`gfx` target 已在根 [`../../CMakeLists.txt`](../../CMakeLists.txt) 定义）：

```cmake
add_executable(my_app src/my_app.cpp)
target_link_libraries(my_app PRIVATE gfx)   # gfx 以 PUBLIC 传递 glfw/glad/glm 与 src 头路径
```

> 把 `gfx` 当依赖**链接**即可，**别**再手写 `find_package(OpenGL)` 或手动 include 引擎头。GL 依赖由 `gfx`→`glfw`/`glad` 传递。

---

## 2. ⚠️ 先纠正一个高频错误：默认管线是"全家桶"，不是 hello-triangle

`renderer.BuildDefaultPipeline()` 装上 `Shadow → Geometry → PostProcess → DebugHud` 四个 pass。其中 **`GeometryPass::Execute` 会无条件解引用**这些 `RenderFrame` 字段：

```
f.post  f.lights  f.shadow  f.env  f.skybox  f.pbr  f.camera  f.scene  f.frustum  f.viewProj  f.fbWidth/fbHeight
```

留空（`nullptr`）任意一个 → 解引用崩溃。**没有"少填几个字段的极简 RenderFrame"这条路**（PBR/CSM/IBL/HDR 是一体的）。所以写新程序有两种正道：

### 路线 A（推荐给"基于引擎做应用"）：以 `src/main.cpp` 为骨架，换内容

直接复制 [`../../src/main.cpp`](../../src/main.cpp) 的**生命周期骨架**（见 §3），只改"造物体"的部分。必须保留的子系统（缺一即崩）：`Renderer` + `PostProcessChain` + `LightBuffer` + `CascadedShadowMap` + `EnvironmentMap` + `SkyboxRenderer` + `pbr`/`depth` 着色器 + `Camera` + `Scene` + 每帧 `Frustum`。用不到的效果用开关关掉即可（`frame.useShadow=false` 等），**不要删对象**。

### 路线 B（真要极简 / 加后处理叠加）：自定义 `RenderPass`，不调 `BuildDefaultPipeline`

`RenderPass` 基类只有一个纯虚函数（`src/gfx/render/RenderPass.h`）：

```cpp
class MyPass : public gfx::RenderPass {
public:
    MyPass() : gfx::RenderPass("MyPass") {}
    void Execute(gfx::RenderFrame& f) override {   // 只在渲染线程被调用（Renderer::Render 保证）
        glClearColor(0.02f, 0.02f, 0.03f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        // 用你自己的 ShaderProgram + Mesh 画东西；此处 GL 全部合法（渲染线程）
    }
};
gfx::Renderer renderer; renderer.Init();
renderer.AddPass(std::make_unique<MyPass>());      // 只跑你的 pass，绕开 GeometryPass 的字段要求
renderer.Render(frame);                             // frame 可极简（但你的 pass 用啥填啥）
```

注意：PBR 着色器要出图仍需 `LightingBlock` UBO 与 IBL 采样器（见 §6），所以纯极简路线一般配**自带简单着色器**，或仍复用路线 A 的子系统。

---

## 3. 生命周期骨架（务必照抄这个结构）

`src/main.cpp` 的顺序是被验证过的，尤其**析构时机**：

```cpp
int main() {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if GLFW_PLATFORM_MACOS
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    GLFWwindow* win = glfwCreateWindow(gfx::kWindowWidth, gfx::kWindowHeight, gfx::kWindowTitle, nullptr, nullptr);
    if (!win) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win);
    if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress))) { /* teardown */ return 1; }

    gfx::RenderContext::MarkAsRenderThread();          // 声明本线程是唯一渲染线程

    // 关键：把所有 GL 资源 owner 放进一个 lambda，让它在本函数返回、
    // glfwDestroyWindow/Terminate 之前析构（此时 context 仍 current）。
    auto run = [&]() -> int {
        gfx::Renderer renderer; renderer.Init(); renderer.BuildDefaultPipeline();
        // ... 建着色器/子系统/场景，组 RenderFrame，while 循环 renderer.Render(frame) ...
        return 0;   // 所有 owner 在此、在渲染线程上析构
    };
    const int rc = run();
    glfwDestroyWindow(win);
    glfwTerminate();
    return rc;
}
```

**别**把 `Mesh`/`Texture2D`/`ShaderProgram` 等 owner 泄漏到 `glfwTerminate()` 之后析构——那会在丢上下文后发 GL 删除调用。

---

## 4. 已验证 API 速查（签名取自源码，勿臆造）

| 目的 | 调用 | 备注 |
| --- | --- | --- |
| 着色器 | `gfx::ShaderProgram::CreateFromSource(vs, fs)` → 有 `operator bool`/`.error()` | 常量在 `gfx::shaders::kPbrVertex/kPbrFragment/kDepthVertex/kDepthFragment/kInstancedVertex/kInstancedFragment` |
| UBO 绑定 | `prog->SetBlockBinding("LightingBlock", gfx::LightBuffer::kBinding)`；`"ShadowBlock", gfx::CascadedShadowMap::kShadowBinding` | **链接后设一次**；不绑=全黑无报错 |
| 采样器 | `prog->Set("uShadowMap", (int)gfx::texunit::shadowArray)` 等 | `texunit::{shadowArray,irradiance,prefilter,brdfLut,skybox}` |
| 几何 | `gfx::GeometryFactory::Cube(size)` / `Sphere(radius,segments,rings)` / `Plane(halfExtent)` → `gfx::MeshData` | |
| 上传网格 | `gfx::Mesh m; m.Upload(std::move(data));` | 仅渲染线程；move-only |
| 材质 | `gfx::PbrMaterial{ baseColor(vec4), metallic, roughness, albedo(Texture2D*), ... }` | `albedo` 可空 |
| 变换 | `gfx::Transform t; t.translation/t.scale; t.SetAxisAngle(axis, rad);` | |
| 场景 | `gfx::Scene s; auto& n=s.CreateRoot(t);` `parent->AddChild(t)` `n.SetRenderable(&mesh,&mat)` `n.SetLocalBounds(c,r)` `n.castsShadow` `s.Update()` | 节点存**非拥有**指针 |
| 相机 | `gfx::Camera c; c.SetPerspective(fov,aspect,near,far); c.LookAt(eye,target,up); c.SetViewportAspect(a); c.ViewProjection(); c.Position();` | |
| 视锥剔除 | `gfx::Frustum fr; fr.Extract(viewProj);` | GeometryPass 用它剔可见 |
| 后期 | `gfx::PostProcessChain post; post.Init(); post.SetExposure(x);` | |
| 阴影 | `gfx::CascadedShadowMap csm; csm.Init(2048);` | |
| 环境/天空 | `gfx::EnvironmentMap env; env.Generate(travel,256,32,256);` `gfx::SkyboxRenderer sky; sky.Init();` | |
| 光照 | `gfx::LightBuffer lb; lb.Init(); gfx::LightSetup s; s.sun.{direction,color,intensity}; s.ambient; lb.Update(s, camPos);` | |
| 实例化 | `gfx::InstancedMesh im; if (!im.Create(std::move(geo), std::move(insts))) {...}` | **成员函数非静态**，返回 `bool`；`gfx::Instance{model(mat4),color(vec4)}` |
| 拾取 | `gfx::PickRay(px,py,fbw,fbh,invViewProj)` → `gfx::Ray`；`gfx::PickNearest(ray, spheres)`→index | 像素是 framebuffer 坐标（Retina 下先从 GLFW 窗口坐标乘 scale）；`spheres: vector<pair<vec3,float>>`，取自 `scene.PickTargets()` |
| 常量 | `gfx::kWindowWidth/kWindowHeight/kWindowTitle/kAppName/kAppVersion` | 来自 `Platform.h` |

---

## 5. 异步加载模型 / 贴图（两阶段；就绪前是空的）

```cpp
gfx::AssetManager assets;                 // 内含线程池
assets.RequestTexture("albedo", "src/assets/models/robot/albedo.png");   // 签名是 (key, path)
assets.RequestModel  ("robot",  "src/assets/models/robot.fbx");          // 重复 key 会被忽略
while (running) {
    assets.ProcessUploads();              // 每帧一次，在渲染线程把后台解码结果上传
    if (auto t = assets.GetTexture("albedo")) { /* shared_ptr<Texture2D>，用它 */ }
    // 就绪前 Get* 返回 nullptr：解在后台线程，上传在渲染线程 —— 绝不能在工作线程发 gl*
}
```

投放目录是 [`../../src/assets/models/`](../../src/assets/models/)（FBX/OBJ/glTF）。两条已验证的拒收规则：材质里的纹理引用必须解析在模型自己目录内（绝对路径/`..` 被 `ModelLoader` 拒，stderr 一行诊断）；图片边长超 `kMaxTextureSide`（16384）在解码前读头部即拒（防解压炸弹），错误走 `std::expected` 串。线程与阶段不变量见 [../developer/thread-safety.md](../developer/thread-safety.md)。

---

## 6. 改着色器：只有一个真源

GLSL **内嵌**在 `src/gfx/shader/*Shaders.h`（`ShaderLib.h`/`PostProcessShaders.h`/`IblShaders.h`）——**只改这里**。`src/assets/shaders/` 是**只读镜像，不编译不加载**，改它无效果。

---

## 7. 交付前自检（对用户的每次代码改动）

```bash
cmake --build build 2>&1 | grep -E 'src/(main|gfx)' | grep -iE 'warning|error'   # 期望：空（自有代码零告警）
./output/<你的可执行> >/tmp/o 2>/tmp/e & p=$!; sleep 6; kill $p 2>/dev/null; wc -c /tmp/e   # 期望：stderr 0 字节
```

- 全黑且无报错 → 十有八九漏了 §4 的 `SetBlockBinding`。
- 崩在 `AssertRenderThread` → 你在非渲染线程碰了 GL（或 owner 在 `glfwTerminate` 后才析构）。
- macOS 建不出窗 → 缺 `GLFW_OPENGL_FORWARD_COMPAT`（§3）。

硬约束（GL 单线程、move-only RAII、`std::expected` 不跨线程抛、场景持非拥有指针、依赖只向下）动手前先读 [../developer/thread-safety.md](../developer/thread-safety.md) 与仓库根 [`../../AGENTS.md`](../../AGENTS.md) §硬约束。
