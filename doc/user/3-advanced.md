# 🔴 进阶：扩展渲染管线与资源系统

前提：已读完 [🟡 基础](2-basic-usage.md)。**动手写任何会拥有或调用 GL 的代码之前**，先通读 [../developer/thread-safety.md](../developer/thread-safety.md)——本篇的多数约束都源于"GL 只在渲染线程"这条不变量。

---

## 1. 自定义渲染 pass

引擎的默认管线是四个 `RenderPass` 的线性链：`Shadow → Geometry → PostProcess → DebugHud`。你可以插入自己的 pass，或干脆不调 `BuildDefaultPipeline()` 完全自定义顺序。

```cpp
class MyPass : public gfx::RenderPass {
public:
    MyPass() : gfx::RenderPass("MyPass") {}
    void Execute(gfx::RenderFrame& f) override {   // f 里能拿到 camera/scene/post/... 每帧输入
        // 这里做的任何 GL 调用都在渲染线程上（Renderer::Render 已保证）
    }
};
renderer.AddPass(std::make_unique<MyPass>());      // 追加
```

- `RenderFrame` 只是"这一帧的输入打包"（按指针/引用持子系统，**不拥有**任何 GL 资源），传给 pass 不会复制或移动 owner。
- 为什么是**线性链而非完整 render graph**：本项目只有一个离屏 HDR target + 少量固定后处理，依赖关系简单，显式线性顺序比 graph 的调度开销与心智负担更划算。取舍详见 [../developer/design.md](../developer/design.md) §3。

---

## 2. 两阶段资源管线（进阶）

[🟡 基础 §3](2-basic-usage.md#3-加载一张贴图--一个模型最简用法) 的 `Request* + ProcessUploads` 背后是明确的阶段划分：

| 阶段 | 在哪个线程 | 做什么 | 产出 |
| --- | --- | --- | --- |
| **Stage A** | 后台工作线程（`ThreadPool`） | 只做 **CPU 解码**（stb 解图、assimp 解模型），绝不碰 GL | `Texture2DDesc` / `LoadedModelData`（纯 CPU 数据） |
| **Stage B** | 渲染线程，在 `AssetManager::ProcessUploads()` 里 | 把已完成的解码结果 `glGenTextures/glBufferData` 上传 | 真正的 GL 对象 |

含义与约束：

- 请求后**就绪前** `Find*` 返回 `nullptr`/空句柄，绝不暴露半成品——按"可能还没好"写代码（`if (auto t = assets.FindTexture(...))`）。
- 解码是异步的，但**上传永远排在渲染线程**，所以你在 `Request*` 之后不能立刻假设资源可用，要跨帧轮询。
- 错误用 `std::expected<T,E>` 传回，**不跨线程抛异常**。线程池顶层另有 `try/catch` 兜底，防止任务抛异常逃逸出 `std::thread` 触发 `std::terminate`——但这是安全网，你的代码不应依赖"抛异常跨线程"。详见 [../developer/thread-safety.md](../developer/thread-safety.md)。

---

## 3. 后期链与运行期开关

HDR 后期是一条固定链：主场景渲染进**线性 HDR target** → bright-pass → 可分离高斯模糊 → **ACES tonemap + gamma 合成**（统一在 composite 一步完成色彩空间转换）。运行期可由 `RenderFrame` 上的一组布尔驱动：

| 开关字段 | 演示里对应键 | 作用 |
| --- | --- | --- |
| `useShadow` | `1` | 级联阴影 |
| `useIbl` | `2` | IBL 环境光 |
| `useBloom` | `3` | Bloom |
| `useDebug` | `4` | 调试线框 |
| `useInstances` | `5` | 实例化场 |

把它们接你自己的 UI/配置即可。曝光用 `post.SetExposure(...)` 调。

---

## 4. 改 GLSL：只有一个真源

着色器 GLSL **内嵌**在 `src/gfx/shader/*Shaders.h`（`ShaderLib.h` / `PostProcessShaders.h` / `IblShaders.h`），这是**单一真源**，随 `gfx` 模块一起编译进去。

- ⚠️ `src/assets/shaders/` 里的 `.glsl/.vert/.frag` **只是只读参考镜像，不被编译、也不被加载**。改它**不影响运行**。
- 要改着色器，改 `*Shaders.h`。理由：以 C++20 named module 交付时把 GLSL 作为字符串内联，避免运行期依赖磁盘路径/工作目录。详见 [../developer/design.md](../developer/design.md) §7。

---

## 5. macOS Core Profile 需要 forward-compat

在 macOS 上建窗口必须开 forward-compatible hint，否则拿不到 Core Profile 上下文：

```cpp
#if GLFW_PLATFORM_MACOS
glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
```

`GLFW_PLATFORM_MACOS` 来自文本 include 的 `Platform.h`（宏不跨模块边界，见 [🟡 基础 §1](2-basic-usage.md#为什么-platformh-还是-include-而不是-import)）。

---

## 6. 资源投放与产物目录约定

- **运行期内容**放 [`../../src/assets/`](../../src/assets/)（与 `src/gfx` 代码同级）：`src/assets/models/` 是 FBX/OBJ/glTF 投放目录，经 `AssetManager` 异步加载。
- **可执行文件**固定输出到 `output/`（被 gitignore）。
- **构建目录** `build/`、`cmake-build-*/`（CLion/预设用）均已 gitignore，可放心删。

---

## 下一步 / 参考

- 出问题了（黑屏、CLion 无配置、缺 `clang-scan-deps`、模型加载不出来）→ [🧰 排错](4-troubleshooting.md)
- 要发版本 / 触发 CI → 根 [../../README.md](../../README.md) 与 [../../AGENTS.md](../../AGENTS.md) 的 CI 段
- 想读权威用法 → [`../../src/main.cpp`](../../src/main.cpp)
