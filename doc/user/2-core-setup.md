# 🟢 ① 核心骨架：窗口、渲染线程、Renderer

前提：已完成 [入门](1-getting-started.md)（项目能构建、demo 窗口能弹出来）。这一章教你用**最少的代码**把 `gfx` 接进自己的 `main.cpp`：建窗口、声明渲染线程、初始化 `Renderer`。只讲骨架，几何/材质/资源/光照分别在 [②](3-geometry-scene.md)/[③](4-assets-loading.md)/[④](5-lighting-ubo.md)。

> 涉及线程与 GL 对象生命周期的硬规则见 [../developer/thread-safety.md](../developer/thread-safety.md)；"为什么这样设计"见 [../developer/design.md](../developer/design.md)。

---

## 1. 两行引入，一个骨架

引擎以单一 C++20 named module `gfx` 交付，你的 `main.cpp` 只需两行引入：

```cpp
#include "gfx/core/Platform.h"   // 文本 include：GLAD-before-GLFW 顺序 + GLFW_PLATFORM_* 宏 + 窗口常量
import gfx;                      // 整个引擎；无需再 #include 任何 gfx/** 头

int main() {
    if (!glfwInit()) return 1;
    GLFWwindow* win = glfwCreateWindow(gfx::kWindowWidth, gfx::kWindowHeight, gfx::kWindowTitle, nullptr, nullptr);
    glfwMakeContextCurrent(win);
    gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress));

    gfx::RenderContext::MarkAsRenderThread();   // 声明：本线程是唯一渲染线程

    gfx::Renderer renderer;
    renderer.Init();
    renderer.BuildPbrPipeline();                // 装全套演示链 Shadow→Geometry→Skybox→PostProcess→DebugHud
    // renderer.BuildMinimalPipeline();         // 或极简 Geometry→DebugHud：不申请后处理/阴影/天空盒也能出图
    // ... 建资源（②③④章）、每帧组装 RenderFrame、renderer.Render(frame) ...
}
```

macOS 上建窗口还差一个 hint（Core Profile 必需，否则拿不到上下文）：

```cpp
#if GLFW_PLATFORM_MACOS
glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
```

## 2. 为什么 `Platform.h` 是 `#include` 而不是 `import`

预处理宏（`GLFW_PLATFORM_MACOS`、`GL_VERSION` 等）与 `<GLFW/glfw3.h>` **无法跨模块边界传递**，`#if` 在预处理阶段求值早于 `import`。同理，`glm::vec3` / `GLuint` 这些出现在 `gfx` 接口里的类型属于 global module，你的代码要用它们时也得**自行文本 include** `<glm/...>`。完整取舍见 [../developer/design.md](../developer/design.md) §7。

## 3. 为什么 `MarkAsRenderThread()` 必须有

引擎假设**所有 GL 调用（创建与删除）都发生在同一个渲染线程**。`main` 里调用一次 `MarkAsRenderThread()` 声明"当前就是这个线程"；之后每个拥有 GL 对象的类在公有入口和析构里做 `AssertRenderThread(...)`，一旦你在别的线程碰 GL 就会立即 `abort`（而不是留下难查的花屏/崩溃）。规则细则见 [../developer/thread-safety.md](../developer/thread-safety.md)。

## 4. 每帧的形状：RenderFrame + Render

绘制不是一堆内联 `gl*` 调用，而是逐帧组装一个 `RenderFrame`（"这一帧的输入打包"：核心是相机、视锥、场景、光照 UBO 与一个着色器程序，其余子系统——阴影 / 天空盒 / IBL / 泛光 / 实例化 / 覆盖层——各归一个可选记录，按指针/引用持用、**不拥有**任何 GL 资源），然后一句 `renderer.Render(frame)` 按 pass 顺序执行。子系统只填你带来的那些即可，缺省记录 = 该效果不参与本帧。开关字段怎么用见 [④ 光照与 UBO](5-lighting-ubo.md)，极简/自定义 pass 见 [🔴 进阶](8-advanced.md)。

---

## 下一步

- 往场景里放东西：几何、材质、变换层级 → [🟡 ② 几何与场景](3-geometry-scene.md)
- 黑屏了 → 大概率是 UBO 没绑，见 [④ 光照与 UBO](5-lighting-ubo.md) 与 [🧰 排错](9-troubleshooting.md)
