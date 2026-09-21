# 🟡 基础：把 `gfx` 接入你自己的程序

前提：已完成 [🟢 入门](1-getting-started.md)，项目能跑起来。这一篇教你把引擎当作一个 **C++20 named module `gfx`** 用在自己的 `main.cpp` 里——建窗口、初始化、造几何/材质、挂场景图、每帧渲染。

> 深入"为什么这样设计"见 [../developer/design.md](../developer/design.md)；涉及线程与 GL 对象生命周期的硬规则见 [../developer/thread-safety.md](../developer/thread-safety.md)。

---

## 1. 最小可运行骨架

引擎以单一 named module 交付，你的 `main.cpp` 只需两行引入：

```cpp
#include "gfx/core/Platform.h"   // 文本 include：GLAD-before-GLFW 顺序 + GLFW_PLATFORM_* 宏 + 窗口常量
import gfx;                      // 整个引擎；无需再 #include 任何 gfx/** 头

int main() {
    if (!glfwInit()) return 1;
    GLFWwindow* win = glfwCreateWindow(gfx::kWindowWidth, gfx::kWindowHeight, gfx::kWindowTitle, nullptr, nullptr);
    glfwMakeContextCurrent(win);
    gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress));

    gfx::RenderContext::MarkAsRenderThread();   // 声明：本线程是唯一渲染线程（见 thread-safety.md）

    gfx::Renderer renderer;
    renderer.Init();
    renderer.BuildDefaultPipeline();            // 安装 Shadow→Geometry→PostProcess→DebugHud 四个 pass
    // ... 建资源、组 RenderFrame、每帧 renderer.Render(frame) ...
}
```

### 为什么 `Platform.h` 还是 `#include` 而不是 `import`

预处理宏（`GLFW_PLATFORM_MACOS`、`GL_VERSION` 等）与 `<GLFW/glfw3.h>` **无法跨模块边界传递**，`#if` 在预处理阶段求值早于 `import`。同理，`glm::vec3` / `GLuint` 这些出现在 `gfx` 接口里的类型属于 global module，你的代码要用它们时也得**自行文本 include** `<glm/...>`。完整取舍见 [../developer/design.md](../developer/design.md) §7。

### `MarkAsRenderThread()` 为什么必须有

引擎假设**所有 GL 调用（创建与删除）都发生在同一个渲染线程**。`main` 里调用一次 `MarkAsRenderThread()` 声明"当前就是这个线程"；之后每个拥有 GL 对象的类在入口和析构里做 `AssertRenderThread(...)`，一旦你在别的线程碰 GL 就会立即 `abort`（而不是留下难查的花屏/崩溃）。规则细则见 [../developer/thread-safety.md](../developer/thread-safety.md)。

---

## 2. 几何、材质、场景节点

三个对象各司其职：**Mesh = 顶点数据（VBO/VAO/IBO）+ 材质；Transform = 位置；SceneNode = 把二者挂进层级**。

```cpp
gfx::Mesh mesh;                              // move-only RAII，拥有 VAO/VBO/IBO
gfx::MeshData data = gfx::GeometryFactory::Sphere(0.5f, 48, 32);
mesh.Upload(std::move(data));                // Stage B：只能在渲染线程调用

gfx::PbrMaterial mat;
mat.baseColor  = glm::vec4(0.9f, 0.5f, 0.25f, 1.0f);
mat.metallic   = 0.6f;
mat.roughness  = 0.3f;

gfx::Transform t; t.translation = glm::vec3(0, 0.5f, 0);
gfx::SceneNode& node = scene.CreateRoot(t);
node.SetRenderable(&mesh, &mat);             // 节点持"非拥有"指针：mesh/mat 的生命周期归你
```

要点：

- `Scene` 是**纯 CPU** 的变换层级，每帧 `scene.Update()` 刷新世界矩阵与包围球，再展平成 `Renderables()/ShadowCasters()/PickTargets()` 供各 pass 消费。
- `SceneNode` 只存 `const Mesh*` / `const PbrMaterial*` 等**非拥有指针**——GL 对象的生命周期归你（通常等价于 `main.cpp` 里的成员/局部变量）。别把 `Mesh` 塞进临时表达式，一帧结束就被析构。
- `GeometryFactory` 现成有 `Cube/Sphere/Plane`；要自定义就往 `MeshData` 里填顶点/索引再 `Upload`。

---

## 3. 加载一张贴图 / 一个模型（最简用法）

慢的解码在后台线程，GL 上传留在渲染线程，两者由 `AssetManager` 隔开。基础用法只需"请求 → 每帧处理 → 取用"：

```cpp
gfx::AssetManager assets;                          // 内含线程池
// Request* 是 (key, path) 两个参数：key 由调用方命名，重复的 key 会被忽略
assets.RequestTexture("robot.albedo", "src/assets/models/robot/albedo.png");
assets.RequestModel  ("robot",       "src/assets/models/robot.fbx");

while (running) {
    assets.ProcessUploads();                       // 每帧一次，在渲染线程把已完成的解码结果上传为 GL 对象
    if (auto tex = assets.GetTexture("robot.albedo")) {
        // 就绪前 Get* 返回 nullptr（shared_ptr）；拿到后才可用 *tex
    }
}
```

模型文件里的贴图引用受两条硬规则约束：纹理路径必须解析在模型自己所在目录内（绝对路径与 `..` 越界会被拒，stderr 留一行诊断），图片边长不得超过 `kMaxTextureSide`（16384，解码前读头部即校验，超限走 `std::expected` 错误串）。投放目录的完整约定见 [`src/assets/models/README.md`](../../src/assets/models/README.md)。

**不要在后台线程里自己 `glGenTextures`**——异步加载的两阶段模型细节见 [🔴 进阶 §2](3-advanced.md#2-两阶段资源管线进阶)。

---

## 4. UBO 绑定：新代码最常踩的"黑屏第一课"

用 PBR + 光照时，若忘了给 **uniform block 绑定**，画面会**全黑且没有任何 GL 报错**——因为 GLSL 4.10 不支持在 uniform block 上写 `layout(binding=N)`，绑定必须在 C++ 侧显式做一次：

```cpp
pbr->SetBlockBinding("LightingBlock", gfx::LightBuffer::kBinding);              // binding 1
pbr->SetBlockBinding("ShadowBlock",   gfx::CascadedShadowMap::kShadowBinding);  // binding 2
pbr->Set("uShadowMap", (int)gfx::texunit::shadowArray);                        // 采样器同理，链接后设一次
```

漏掉这步是经典的"场景全黑但没有报错"。完整排错见 [🧰 排错 §3](4-troubleshooting.md#3-黑屏且无任何报错忘了给-ubouniform-block-绑定)。

---

## 下一步

- 想加自定义渲染 pass、吃透异步管线、调后期与开关 → [🔴 进阶](3-advanced.md)
- 想理解这套 API 背后的分层与取舍 → [../developer/design.md](../developer/design.md)
