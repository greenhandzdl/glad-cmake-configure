# 🔴 进阶：扩展渲染管线与资源系统

前提：已读完入门篇（[① 骨架](2-core-setup.md)～[⑥ 体素世界](7-voxel-basics.md)）。**动手写任何会拥有或调用 GL 的代码之前**，先通读 [../developer/thread-safety.md](../developer/thread-safety.md)——本篇的多数约束都源于"GL 只在渲染线程"这条不变量。

---

## 1. 自定义渲染 pass

引擎提供两个现成的 stage 顺序：`BuildPbrPipeline()` 装全套演示链 `Shadow → Geometry → Skybox → PostProcess → DebugHud`；`BuildMinimalPipeline()` 只装 `Geometry → DebugHud`——不申请后处理链时 `RenderFrame.post` 置空，几何 pass 会直接绑定默认帧缓冲、自行 clear 后渲到窗口，证明阴影/天空盒/泛光都不是出图的前提。你可以 `AddPass()` 插入自己的 pass，或干脆不调任一个 builder 完全自定义顺序。

一个**能直接编译**的 pass：持自己的 `ShaderProgram` + `VertexArray`（RAII，构造即建、析构即毁），`Execute` 里从 `RenderFrame` 取 `fbWidth/fbHeight` 裸调帧级 GL、再走成员画一笔：

```cpp
class TintPass : public gldx::RenderPass {
public:
    TintPass() : gldx::RenderPass("Tint") {          // 构造在 OnCreate 里跑：上下文已 current
        if (auto p = gldx::ShaderProgram::CreateFromSource(kVert, kFrag)) prog_ = std::move(*p);
        vao_.Create();                                 // 空 VAO 足够画 gl_VertexID 驱动的全屏三角
    }
    void Execute(gldx::RenderFrame& f) override {     // f 里能拿到 camera/scene/post/... 每帧输入
        glBindFramebuffer(GL_FRAMEBUFFER, 0);         // 这里的任何 GL 调用都在渲染线程（Renderer::Render 已保证）
        glViewport(0, 0, f.fbWidth, f.fbHeight);
        if (!prog_.valid()) return;
        prog_.Use();
        prog_.Set("uTime", f.smoothedFps);            // uniform 逐个设，无需绑定程序
        vao_.Bind();
        vao_.DrawArrays(GL_TRIANGLES, 0, 3);
        vao_.Unbind();
    }
private:
    gldx::ShaderProgram prog_;
    gldx::VertexArray   vao_;
    static constexpr const char* kVert = R"GLSL(#version 410 core /* ... */)GLSL";
    static constexpr const char* kFrag = R"GLSL(#version 410 core /* ... */)GLSL";
};

renderer.AddPass(std::make_unique<TintPass>());      // 追加到链尾；不 builder 也能出图
```

- `RenderFrame` 只是"这一帧的输入打包"（按指针/引用持子系统，**不拥有**任何 GL 资源），传给 pass 不会复制或移动 owner。
- **帧级/pass 级 GL 刻意裸调**（`glBindFramebuffer/glViewport/glClear`），但**句柄走 RAII**（`ShaderProgram/VertexArray/GLBuffer/Texture2D`）、**绘制走 `VertexArray` 成员**——不要自己 `glGenBuffers`。
- 为什么是**线性链而非完整 render graph**：本项目只有一个离屏 HDR target + 少量固定后处理，依赖关系简单，显式线性顺序比 graph 的调度开销与心智负担更划算。取舍详见 [../developer/design.md](../developer/design.md) §3。

🔗 **现场演示**：
- 一个 pass 装完全自定义链（不 `BuildPbrPipeline`，手工 `AddPass`）：[`../../src/demo/texture_samplers/SamplerPass.cpp`](../../src/demo/texture_samplers/SamplerPass.cpp)。
- **一个 for 循环给不同窗装配不同 `RenderPass` 子类**（Clear/Triangle/Quad/Line/Point）：[`../../src/demo/render_passes/Passes.cpp`](../../src/demo/render_passes/Passes.cpp) + [`View.cpp`](../../src/demo/render_passes/View.cpp)。
- pass 里回写 HUD（读 `f.visibleCount/f.selected`）：[`../../src/demo/camera_picking/main.cpp`](../../src/demo/camera_picking/main.cpp) 的 `PickHudPass`。

---

## 2. 后期链与运行期开关

HDR 后期是一条固定链：主场景渲染进**线性 HDR target** → bright-pass → 可分离高斯模糊 → **ACES tonemap + gamma 合成**（统一在 composite 一步完成色彩空间转换）。它是可选的：不想要就把 `frame.post` 置空、并不装 `PostProcessPass`，几何 pass 会直渲窗口（代价是不经 ACES/伽马合成，颜色空间即着色器输出）。要想要，则运行期由 `RenderFrame` 上分属各可选记录的开关驱动：

| 开关字段 | 演示里对应键 | 作用 |
| --- | --- | --- |
| `shadow.enabled` | `1` | 级联阴影 |
| `ibl.enabled` | `2` | IBL 环境光（数据在 `sky.env`，开关独立） |
| `useBloom` | `3` | Bloom（挂在 `post` 上，无独立记录） |
| `overlay.useDebug` | `4` | 调试线框 |
| `instances.enabled` | `5` | 实例化场 |
| `sky.box = nullptr` | `6` | 天空盒（不是布尔字段：置空指针即不画，背景回到 clear color） |
| `ortho` | `Tab` | 透视 ↔ 正交投影（相机侧还要 `ToggleProjection()` 换投影矩阵，见 `Camera`） |

把它们接你自己的 UI/配置即可。曝光用 `post.SetExposure(...)` 调。投影互切的相机侧细节见 [⑤ 相机与拾取](6-camera-picking.md)。

---

## 3. 改 GLSL：只有一个真源

着色器 GLSL **内嵌**在 `src/gldx/shader/*Shaders.h`（`ShaderLib.h` / `PostProcessShaders.h` / `IblShaders.h`），这是**单一真源**，随 `gldx` 模块一起编译进去。

- ⚠️ `src/assets/shaders/` 顶层的 `.glsl` **是已废除的镜像，只剩一行指向真源的注释，不被编译、也不被加载**。改它**不影响运行**。（真正从磁盘加载的 demo 着色器已搬到各 demo 的 `assets/shaders/` 下，见 §5。）
- 要改着色器，改 `*Shaders.h`。理由：以 C++20 named module 交付时把 GLSL 作为字符串内联，避免运行期依赖磁盘路径/工作目录。详见 [../developer/design.md](../developer/design.md) §7。

---

## 4. 资源投放与产物目录约定

- **运行期内容**放 [`../../src/assets/`](../../src/assets/)（与 `src/gldx` 代码同级）：`src/assets/models/` 是 FBX/OBJ/glTF 投放目录，经 `AssetManager` 异步加载；材质里的纹理引用必须落在模型自己目录内，图片边长上限 `kMaxTextureSide`（16384）在解码前校验，约定细则见 [`../../src/assets/models/README.md`](../../src/assets/models/README.md)。
- **可执行文件**固定输出到 `output/`（被 gitignore）。
- **构建目录** `build/`、`cmake-build-*/`（CLion/预设用）均已 gitignore，可放心删。

---

## 5. 写你自己的着色器：ShaderProgram 四入口与全阶段装配

引擎自带着色器是内嵌字符串（§3）。但当你**自己的**效果需要手写 GLSL——从内存源、从磁盘文件、或挂上几何/细分等可选阶段——都走 `gldx::ShaderProgram` 的四个装配入口。它们都在**渲染线程**建（编译/链接是 GL 调用），失败一律以 `std::expected<ShaderProgram, std::string>` 返回错误串（info log），**从不抛异常**；部分创建的 shader 会被逐个释放。

| 入口 | 输入 | 用途 |
| --- | --- | --- |
| `CreateFromSource(vert, frag)` | 两段内嵌源 | 最经典的 V+F 对 |
| `CreateFromFiles(vertPath, fragPath)` | 两个磁盘路径 | 从文件加载 V+F（热加载） |
| `CreateFromSources({ShaderSource,...})` | `{stage, 源}` 花括号列表 | **选择性多阶段（内嵌）**：加几何/细分就各列一条 |
| `CreateFromFiles({ShaderFile,...})` | `{stage, 路径}` 花括号列表 | **多阶段从磁盘**：先把每个文件读全再装配，缺文件在任何 GL 前就 `unexpected` |

`enum class ShaderStage : GLenum { Vertex, Fragment, Geometry, TessControl, TessEvaluation }`——**无 Compute**（`GL_COMPUTE_SHADER` 是 4.3+，超本引擎 4.1 基线）。4.1 图形程序**至少要有 Vertex + Fragment**，中间阶段可选、**每个至多出现一次、列表顺序无关**。

**① 两文件从磁盘加载**（`--vert/--frag` 指到 `src/demo/shader_file/assets/shaders/triangle.{vert,frag}`）：

```cpp
auto loaded = gldx::ShaderProgram::CreateFromFiles(vertPath, fragPath);
if (!loaded) { std::fprintf(stderr, "compile failed: %s\n", loaded.error().c_str()); return 1; }
gldx::ShaderProgram program = std::move(*loaded);
program.Use();
program.SetBlockBinding("LightingBlock", gldx::LightBuffer::kBinding);  // uniform block 在 C++ 侧绑（GLSL 4.10 不允许 layout(binding=N)）
```

**② 选择性挂一个几何阶段**（GL_POINTS → 每个点扩成一个方块；缺了 GS 就画不出方块，这是"阶段真的链进去了"的证据）：

```cpp
auto loaded = gldx::ShaderProgram::CreateFromSources({
    {gldx::ShaderStage::Vertex,   kVert},
    {gldx::ShaderStage::Geometry, kGeom},   // layout(points) in; layout(triangle_strip, max_vertices=4) out;
    {gldx::ShaderStage::Fragment, kFrag},
});
// ...
st.program.Set("uHalf", 0.14f);              // 避免 glm 依赖：uniform 用逐个标量
st.vao.DrawArrays(GL_POINTS, 0, pointCount); // GS 把每个点扩成 4 顶点的 triangle_strip
```

**③ 一次装配全 5 个图形阶段**（GL_PATCHES quad 细分 + 几何转线框，证明整条 4.1 管线可组装）：

```cpp
auto loaded = gldx::ShaderProgram::CreateFromSources({
    {gldx::ShaderStage::Vertex,         kVertex},
    {gldx::ShaderStage::TessControl,    kTessControl},    // layout(vertices=4) out; 设 gl_TessLevelOuter/Inner
    {gldx::ShaderStage::TessEvaluation, kTessEval},       // layout(quads, equal_spacing, ccw) in; 用 gl_TessCoord 插值
    {gldx::ShaderStage::Geometry,       kGeometry},       // 把细分三角形重发为线框
    {gldx::ShaderStage::Fragment,       kFragment},
});
// draw：细分补丁的配置是 pass 级 GL，刻意裸调
glPatchParameteri(GL_PATCH_VERTICES, 4);
st.vao.DrawArrays(GL_PATCHES, 0, 4);         // 4 个控制点
```

> ⚠️ macOS 上同时挂细分 + 几何会触发驱动的 `SW vertex processing for EVAL_PROG + GEOM_PROG` 提示——这是**良性的软件回退说明、不是错误**，画面照常出。

🔗 **现场演示**：
- ① 两文件加载：[`../../src/demo/shader_file/main.cpp`](../../src/demo/shader_file/main.cpp)（GLSL 在 `src/demo/shader_file/assets/shaders/`）。
- ② 选择性几何阶段：[`../../src/demo/geometry_shader/main.cpp`](../../src/demo/geometry_shader/main.cpp)（V+Geom+F 内嵌）。
- ②' 多阶段**从文件**加载（V+Geom+F 全走 `CreateFromFiles({...})`）：[`../../src/demo/geometry_shader_file/main.cpp`](../../src/demo/geometry_shader_file/main.cpp)。
- ③ 全 5 阶段：[`../../src/demo/shader_stages/main.cpp`](../../src/demo/shader_stages/main.cpp)（GLSL 在 `Stages.h`）。
- ④ 阶段不只变换、还能**生成**几何：[`../../src/demo/menger_sponge/main.cpp`](../../src/demo/menger_sponge/main.cpp)（GLSL 在 `MengerShaders.h`）。门格海绵 level-L 恰有 20^L 个等大子立方体 ⇒ 索引就是一个 20 进制数，`gl_VertexID` 解码出中心、几何阶段展成立方体，`uniform int uLevel` **就是**解码循环的上界——调层级不重建任何东西，CPU 每帧只上传一个循环次数，全仓零顶点缓冲。同一 demo 的另一条臂用 transform feedback 把工作队列放在 GPU 上自行细分（`gldx::TransformFeedback` + `VertexArray::DrawTransformFeedback`）：捕获目标只能用 `CreateFromSources({...}, TransformFeedbackDesc{...})` 在链接前声明，因为 `layout(xfb_buffer)` 是 GLSL 4.30、超出 4.1 core 基线。

---

## 6. 文字 HUD 与调试绘制

三个协作件覆盖"往画面上叠 2D 信息"：**`Font`** 把系统 TTF 用 stb_truetype 烘焙成一张 RGBA 覆盖图集（单个 `Texture2D`）；**`SpriteBatch`** 是左上角像素坐标、自带正交投影的即时模式四边形批；**`TextRenderer`** 无状态，逐字形查图集矩形 + advance、按（目标尺寸 / 烘焙尺寸）缩放后把带色四边形推进共享批——整个 HUD 收敛成几次 draw call。

完整可用的 HUD pass（找不到字体会自动禁用文字叠加，绝不致命）：

```cpp
class HudPass : public gldx::RenderPass {
public:
    HudPass() : RenderPass("Hud") {
        sprite_.Init();                                 // 渲染线程：构造在 OnCreate 里跑
        const char* candidates[] = {                    // 依次试系统字体，命中即用
            "/System/Library/Fonts/Menlo.ttc", "/System/Library/Fonts/Helvetica.ttc",
            "C:/Windows/Fonts/consola.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        };
        for (const char* c : candidates) if (font_.LoadFromFile(c, 48.0f)) break;   // 烘焙尺寸 48 > 任何绘制尺寸 → 下采样仍清晰
        gldx::Texture2DDesc w; w.width = w.height = 1; w.channels = 4; w.pixels = {255,255,255,255};
        white_.Upload(w);                                // 1×1 纯白，给 SpriteBatch 着色用
    }
    void Execute(gldx::RenderFrame& f) override {
        if (!font_.loaded() || !white_.valid()) return;
        sprite_.Begin(white_, f.fbWidth, f.fbHeight);    // 像素坐标、左上角原点
        sprite_.Draw(white_, 16, 16, 320, 64, 0, 0, 1, 1, glm::vec4(0, 0, 0, 0.4f));   // 半透面板底
        gldx::TextRenderer::Draw(sprite_, font_, "gldx demo",  28, 26, 26.0f, glm::vec4(1.0f));
        char line[96];
        std::snprintf(line, sizeof(line), "%.0f fps  fb %dx%d", f.smoothedFps, f.fbWidth, f.fbHeight);
        gldx::TextRenderer::Draw(sprite_, font_, line, 28, 60, 20.0f, glm::vec4(0.75f, 0.85f, 1.0f, 1.0f));
        const float w = gldx::TextRenderer::Measure(font_, line, 20.0f);   // 量宽以右对齐/自适应面板
        sprite_.End();
    }
private:
    gldx::SpriteBatch sprite_;
    gldx::Font        font_;
    gldx::Texture2D   white_;
};
```

要点：`'\n'` 即换行（按字体行距推进 y）；颜色就是一个 RGBA tint；`Measure` 用来按最宽行给面板定尺寸，而不是写死魔法数。

**调试线框与帧耗时**：`DebugDraw` 在 CPU 上累积彩色线段（`Clear()` → 多次 `Push*` → `Draw(viewProj)`），一次 `GL_LINES` 提交、**关深度测试**，所以坐标轴/包围盒/选中轮廓永远浮在上层；固定容量、溢出即丢（是诊断输出、不值得中途重分配）。`Profiler` 用 `GL_TIME_ELAPSED`（4.1 核心）+ CPU 时钟夹住一段 GL 工作，GPU 耗时晚一帧到（异步查询的正常行为）。

```cpp
debug_.Clear();
debug_.PushAxes(glm::vec3(0.0f), 2.0f);                              // 原点三色坐标轴
debug_.PushBoxCenter(center, halfExtent, glm::vec4(0.2f, 0.55f, 0.9f, 0.6f));  // 中心+半边的线框盒
debug_.PushBox(minCorner, maxCorner, glm::vec4(1.0f, 0.85f, 0.2f, 1.0f));      // 角点式
debug_.Draw(f.viewProj);                                             // 复用场景同一 viewProj
profiler_.BeginFrame();  /* ...这段的 GL 工作... */  profiler_.EndFrame();
// profiler_.CpuMs() / profiler_.GpuMs() / debug_.lineCount() 交给 HUD 显示
```

🔗 **现场演示**：
- 纯 2D HUD（Font + SpriteBatch + TextRenderer）：[`../../src/demo/text_hud/main.cpp`](../../src/demo/text_hud/main.cpp)。
- 调试线框 + 帧耗时 + 文字读数：[`../../src/demo/debug_draw/main.cpp`](../../src/demo/debug_draw/main.cpp)。
- HUD 回读剔除/拾取结果（`visibleCount/selected`）：[`../../src/demo/camera_picking/main.cpp`](../../src/demo/camera_picking/main.cpp)。

---

## 下一步 / 参考

- 出问题了（黑屏、CLion 无配置、缺 `clang-scan-deps`、模型加载不出来）→ [🧰 排错](9-troubleshooting.md)
- 两阶段资源管线的用法与拒收规则在 [③ 贴图与模型加载](4-assets-loading.md)；macOS forward-compat 与"为什么 Platform.h 是 include"在 [① 核心骨架](2-core-setup.md)
- 要发版本 / 触发 CI → 根 [../../README.md](../../README.md) 与 [../../AGENTS.md](../../AGENTS.md) 的 CI 段
- 想读权威用法 → [`../../src/main.cpp`](../../src/main.cpp)（hello-triangle）与 [`../../src/demo/`](../../src/demo/) 下的 `pbr_showcase` / `voxel_terrain` / 单功能 demo
