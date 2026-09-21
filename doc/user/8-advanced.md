# 🔴 进阶：扩展渲染管线与资源系统

前提：已读完入门篇（[① 骨架](2-core-setup.md)～[⑥ 体素世界](7-voxel-basics.md)）。**动手写任何会拥有或调用 GL 的代码之前**，先通读 [../developer/thread-safety.md](../developer/thread-safety.md)——本篇的多数约束都源于"GL 只在渲染线程"这条不变量。

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

## 2. 后期链与运行期开关

HDR 后期是一条固定链：主场景渲染进**线性 HDR target** → bright-pass → 可分离高斯模糊 → **ACES tonemap + gamma 合成**（统一在 composite 一步完成色彩空间转换）。运行期可由 `RenderFrame` 上的一组布尔驱动：

| 开关字段 | 演示里对应键 | 作用 |
| --- | --- | --- |
| `useShadow` | `1` | 级联阴影 |
| `useIbl` | `2` | IBL 环境光 |
| `useBloom` | `3` | Bloom |
| `useDebug` | `4` | 调试线框 |
| `useInstances` | `5` | 实例化场 |
| `frame.skybox = nullptr` | `6` | 天空盒（不是布尔字段：置空指针即不画，背景回到 clear color） |
| `ortho` | `Tab` | 透视 ↔ 正交投影（相机侧还要 `ToggleProjection()` 换投影矩阵，见 `Camera`） |

把它们接你自己的 UI/配置即可。曝光用 `post.SetExposure(...)` 调。投影互切的相机侧细节见 [⑤ 相机与拾取](6-camera-picking.md)。

---

## 3. 改 GLSL：只有一个真源

着色器 GLSL **内嵌**在 `src/gfx/shader/*Shaders.h`（`ShaderLib.h` / `PostProcessShaders.h` / `IblShaders.h`），这是**单一真源**，随 `gfx` 模块一起编译进去。

- ⚠️ `src/assets/shaders/` 里的 `.glsl/.vert/.frag` **只是只读参考镜像，不被编译、也不被加载**。改它**不影响运行**。
- 要改着色器，改 `*Shaders.h`。理由：以 C++20 named module 交付时把 GLSL 作为字符串内联，避免运行期依赖磁盘路径/工作目录。详见 [../developer/design.md](../developer/design.md) §7。

---

## 4. 资源投放与产物目录约定

- **运行期内容**放 [`../../src/assets/`](../../src/assets/)（与 `src/gfx` 代码同级）：`src/assets/models/` 是 FBX/OBJ/glTF 投放目录，经 `AssetManager` 异步加载；材质里的纹理引用必须落在模型自己目录内，图片边长上限 `kMaxTextureSide`（16384）在解码前校验，约定细则见 [`../../src/assets/models/README.md`](../../src/assets/models/README.md)。
- **可执行文件**固定输出到 `output/`（被 gitignore）。
- **构建目录** `build/`、`cmake-build-*/`（CLion/预设用）均已 gitignore，可放心删。

---

## 下一步 / 参考

- 出问题了（黑屏、CLion 无配置、缺 `clang-scan-deps`、模型加载不出来）→ [🧰 排错](9-troubleshooting.md)
- 两阶段资源管线的用法与拒收规则在 [③ 贴图与模型加载](4-assets-loading.md)；macOS forward-compat 与"为什么 Platform.h 是 include"在 [① 核心骨架](2-core-setup.md)
- 要发版本 / 触发 CI → 根 [../../README.md](../../README.md) 与 [../../AGENTS.md](../../AGENTS.md) 的 CI 段
- 想读权威用法 → [`../../src/main.cpp`](../../src/main.cpp) 与 [`../../src/voxel_main.cpp`](../../src/voxel_main.cpp)
