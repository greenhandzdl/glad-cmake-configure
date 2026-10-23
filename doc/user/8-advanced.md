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

### 1.1 透明层：一个 pass 里的四道闸门

透明是“自定义 pass”最常见的第一个真实需求，因为它几乎无法只靠改 `GeometryPass` 的一个开关做对：一个 blended 片段会写深度，于是 `GL_LESS` 把同一条视线上的后续东西全拒掉，“玻璃把后面的物体吃掉”就是这么来的。正确的修法不是一行，而是**四道各自独立的闸门**，每一道守一个不同的保证，所以每一道都得能单独关掉才能看出它在守什么（`../../src/demo/glass_water/` 的 `1`/`2`/`3`/`4` 键与 `--off sort` / `--on depthwrite` / `--off shell` / `--blend X` 就是这四道）：

| 闸门 | 做什么 | 单独关掉后的症状 |
| --- | --- | --- |
| `1` 远→近排序 | blend 层按平方距离降序提交 | 叠加顺序反了，透过度与预期不符；与下一道同时关掉时最易读（两者叠加才是“吃掉”） |
| `2` 不写深度 | blend 层 `glDepthMask(GL_FALSE)` | 先提交的透明物把后提交的整块拒掉——“物体直接消失”，而不是“颜色不对”。但别把它当成“一个键就能复现消失”：单独打开时先被拒掉的是最后画的那两盏 additive 灯（`Glow` 层自己不写深度，可深度测试一直接着），要看到水面 tile 吃掉水下箱子得让 `1` 先错序 |
| `3` 闭合壳双趟 | `GL_FRONT` 再 `GL_BACK`，均不写深度 | 一个闭合透明体只合成一层，读不出“玻璃有多厚”，也看不到内外表面叠加 |
| `4` 混合方程 | straight / premultiplied / additive | 着色器与 `glBlendFunc` 对“a=0.5 是什么”给出不同答案，整场透过度错一个量级 |

排序那道还有一个附条件，它不是第五个开关而是同一个开关的一半：**排序粒度必须匹配几何**。把水面当成“一个物体”排，一个跨几米的大 quad 内部依旧错序，而 HUD 会告诉你“排序已开”。所以 demo 把水面切成 36 个索引连续的 tile，与物体塞同一张排序表。

三条容易踩的坑，都是“API 从不报错”型：

- **受害者不可能是任何不透明物体**。不透明层先画且写深度，透明物在它之后通过深度测试——所以“玻璃把石柱吃掉”在正确的层序下压根不会发生。会被吃掉的只有 **blend 层内部的兄弟图元**（盖住水下箱子的那片水面 tile）。想验证自己修好了，得拿一个透明物体当受害者，而不是拿背景里的不透明物——后者永远绿。
- **混合必须发生在线性 HDR target 里**。8 位整数会把每个中间值舍入，并把 additive 的发光体截顶（`1 + 1` 不再是 2）；编码交给 `PostProcessPass` 的 exposure + ACES，所以透明 pass 自己**不要**再 `pow(1/2.2)`（与 [5 §3](5-lighting-ubo.md) 的直渲窗口情形正好相反，区别就在链上有没有后期）。`glBlendFuncSeparate` 的 alpha 通道用 `GL_ZERO, GL_ONE` 钉住目标 alpha，否则天空与地面的 alpha 会被逐层抹平，下游依赖它的 pass 全错。
- **cutout 不属于透明层**。用 `discard` 挖洞的格栅/栅栏既不写色也不写深度，因此它留在不透明侧、照常写深度、不需要排序——“有洞”不是“透明”，是“不存在”。把两者混为一谈是“栅栏把后面物体吃掉”的根源。本 demo 里它还有一个已知伪影：`Texture2D::Upload` 总是生成完整 mip 链，每级都把木条与孔洞平均，所以远处的栅栏会往不透明方向淡出（要修得做 coverage-aware mip，属另一个课题）。

一个反直觉但合法的例外：**additive 的发光层不需要排序**。加法可交换且与背景无关，所以它可以固定画在最后（`Glow` 层）。但“最后”意味着没有任何东西能再合成到它上面，所以一旦某个 glow 应该出现在透明面**之后**，它就必须回到 Blend 层参加排序——“顺序无关”只在层内成立，不是跨层的免罪符。

还有一条取证纪律，是这一节所有数字能读的前提：**“哪个开关在起作用”不等于“哪个开关单独就能复现症状”**。正确排序下箱子先于盖它的水面 tile 提交，先画的东西不会被后来的深度值拒掉，所以深度写与排序各管“吃掉”这件事的一半；报告像素差时必须把“是哪几个开关的组合”写在同一行，否则下一个人照着文档复现不出来。关掉单个闸门、与参考帧逐像素对比（`--off hud` + `--freeze-at` 不晚于 `--snap-at`，判据“灰度差 > 0.6% 满量程”，1600×1200 = 1,920,000 px）：

| 对照 | 差异像素 | 对照 | 差异像素 |
| --- | --- | --- | --- |
| `--off sort` | 57,429（2.99%） | `--off shell` | 328,795（17.12%） |
| `--on depthwrite` | 24,827（1.29%） | `--off cull` | 389,302（20.28%） |
| 两者同开（“被吃掉”帧） | 29,551（1.54%） | `--blend additive` | 617,657（32.17%） |

同一批运行里 `--blend straight` 与默认帧**逐字节相同**（0 像素差）——不是开关坏了，是标量 alpha 下 premultiplied 与 straight 本来就是同一处算术的两种写法，见 §1.2。提交计数也在自证：默认 47 次 blend 提交 / 79 次绘制，关排序 12 / 44，关壳双趟 47 / 71。

🔗 **现场演示**：
- 一个 pass 装完全自定义链（不 `BuildPbrPipeline`，手工 `AddPass`）：[`../../src/demo/texture_samplers/SamplerPass.cpp`](../../src/demo/texture_samplers/SamplerPass.cpp)。
- **一个 for 循环给不同窗装配不同 `RenderPass` 子类**（Clear/Triangle/Quad/Line/Point）：[`../../src/demo/render_passes/Passes.cpp`](../../src/demo/render_passes/Passes.cpp) + [`View.cpp`](../../src/demo/render_passes/View.cpp)。
- pass 里回写 HUD（读 `f.visibleCount/f.selected`）：[`../../src/demo/camera_picking/main.cpp`](../../src/demo/camera_picking/main.cpp) 的 `PickHudPass`。
- **一个 pass 里装完整五层透明状态机**（Sky/Opaque/Cutout/Blend/Glow + 四道闸门 + HUD 回读真实 GL 状态）：[`../../src/demo/glass_water/BlendPass.cpp`](../../src/demo/glass_water/BlendPass.cpp)，配套 [`Scene.cpp`](../../src/demo/glass_water/Scene.cpp) 的水面 tile 划分与 [`Shaders.h`](../../src/demo/glass_water/Shaders.h) 的电介质 Cook-Torrance 透明度。

### 1.2 alpha 是守恒推出来的，不是“有人选的值”

四道闸门管的是“谁先画、写不写深度、怎么合”，但还留下一个更难的问题：一个 blended 片段的 alpha **到底该是多少**。如果 alpha 和着色器里的反射、高光各算各的，同一块玻璃可以既“高光很尖”又“菲涅耳很平”，于是一张透明度梯子只是四个互不相干的旋钮在互相拆台。`glass_water` 的取舍是：全场景只留一份**电介质 Cook-Torrance**，alpha 从能量守恒里掉出来：

```glsl
vec3  f0 = vec3(F0FromIor(uIor));                 // ((ior-1)/(ior+1))²：水 2.0%、玻璃 4.0%、肥皂膜 2.9%
vec3  F  = F_Schlick(NoV, f0);                    // 矢量菲涅耳，三个通道各自趋近 1
float mirror = clamp(dot(F, THIRD), 0.0, 1.0);
vec3  trans  = exp(-uAbsorption * (uPathLen / max(NoV, 0.12)));   // Beer-Lambert 斜程
float absorb = 1.0 - dot(trans, THIRD);
float coverage = mirror * uFresnelToAlpha + (1.0 - mirror) * absorb;
float alpha      = clamp(uBaseAlpha + coverage + NoV * NoV * NoV * uCoreToAlpha, 0.0, 1.0);
```

入射光里 F 被反射、`(1-F)` 穿进去后被吸收 `absorb`、剩下 `(1-F)·T` 打到背景，所以 `uFresnelToAlpha = 1` 时 coverage 严格等于 `1-(1-F)T`，两项之和恒为 1——**表面无法在合成时凭空造光**。这条性质就是五块板 + 湖面 + 泡泡叠在一起仍读作“玻璃”而不是“一堆灰色”的原因。`uFresnelToAlpha < 1` 也从此有了唯一含义：这块表面在谎报自己的反射率（磨砂板 0.12、彩色箱 0.4）。两个直接推论：水的 `baseAlpha` 从 0.34 掉到 0.05（那 0.34 里绝大部分本来在冒充反射和吸收）；斜程吸收一行就换掉一张贴图——正看读得出池底格子、斜看是一堵绿墙、远岸总比近处暗，三个现象都是同一次 `exp` 的结果。

剩下的是本项目踩过的最严一个坑：**`uPremultiply` 与混合方程是配对关系，不是两个独立选项**。

```glsl
vec3 rgbOut = (uPremultiply > 0.0) ? c * alpha : c;   // 与 (ONE, 1-SRC_ALPHA) 配对
FragColor   = vec4(rgbOut, alpha);
```

`(ONE, 1-SRC_ALPHA)` **不缩放源色**，所以 shader 必须自己 `rgb *= alpha`；否则每一层都把**全额**辐射叠到刚被它放行的 `(1-a)` 背景上，五块“10% 玻璃”就刷白一帧——这不是调参问题，是把衰减写成了相加。反过来 Straight `(SRC_ALPHA, …)` 是**同一处算术写在固定功能单元里**，所以 shader 侧要把 `uPremultiply` 关掉。两个坑对称：两边都不乘→刷白，两边都乘→a² 衰减（最先被抹掉的是远岸镜带）。标量 alpha 下两种写法**逐像素相同**（按 `4` 切过去 `cmp` 能直接确认），所以“straight 也能过”不能拿来证明 premultiply 没在用——要让 rgb 与 alpha 用不同权重时才分得出。

因此 `premul_` 必须由 pass **按层给**（`BlendPass::Execute` 里只有 Blend 层为 1，Sky/Opaque/Cutout/Glow 为 0），而不是一个全局开关；把它做成全局就是上面那个过曝 bug 的成因。另外镜面高光的峰值在 roughness 0.06 时高达 ~2.4e4，而波面法线只在**顶点**上采样，MSAA 会把亚像素火点摊成一池白雾；修法是两个解析步骤缺一不可：用 `fwidth(n)` 测出三角形内缺失的斜率方差、按平方和展宽 lobe，再按“lobe 比太阳圆盘宽就按两者立体角之比缩放贡献”裁剪峰值（否则表面越粗糙高光反而越亮而不是越铺开）。靠把 roughness 调大蒙混会一并毁掉镜面，也就毁掉这一节要演示的菲涅耳。

### 1.3 透视 ↔ 正交：换的是 `uViewProj`，而 shader 还得知道视线方向

`glass_water` 用 `Tab` 互切两种投影，每帧按开关重设 `cam.SetPerspective(...)` / `cam.SetOrthographic(...)`（相机侧 API 见 [⑤ 相机与拾取 §2](6-camera-picking.md)），顶点阶段**没有任何分支**——“改顶点的 projection 投影方式”到这里就结束了。但有一个量是投影矩阵本身带不过去的：

```glsl
vec3 viewDir = uOrthographic > 0.5
                 ? -normalize(uCamForward)     // 正交：每条光线平行，无眼位
                 : normalize(uEye - vWorldPos); // 透视：眼位到像素
```

正交 frustum 里没有“眼位到像素的向量”——这不是一个粗糙近似，而是**未定义**。硬套透视式会让 NoV→菲涅耳→守恒 alpha 随屏幕位置漂移（边缘最亮），纯属投影伪影；而 §1.2 整节的说服力都建在“NoV 决定一切”上，伪影会直接把它演示的东西抹掉。于是 `orthographic` 与 `camForward` 随 `RenderFrame` 透传，掠射镜带在正交下变均匀——那不是需要打的 bug，那就是“去掉透视”这件事的含义本身。

切换本身还有一个容易被忽略的交接：**取景要跟着过**。`Tab` 翻转那一帧按 `orthoHeight = 2·d·tan(fov/2)`（d = 相机到场景中心的距离）把透视取景换算成正交盒高，反向时 fov 保持原值；不做这一步，一个“只换标签”的切换看起来就像把场景搞坏了。滚轮缩放归投影不归飞行：乘法式（×0.9/×1.1）只缩放当前生效的那个量，各自夹区间（fov 8..110、盒高 2..80）。CLI 侧对应 feature `ortho` 与选项 `--ortho-height`，`--fov` 也从“只设初值”变成与滚轮共用的活量。HUD 要打印的是真正定义投影的那两个数（`fov 55.0 deg` 还是 `box height 12.4 m`），于是取景交接可以直接读出来核对。

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

### 5.1 细分阶段真正难的是“不裂”，不是接线

上面的五阶段装配只证明管线能拼起来。一旦你要按屏幕细节**自适应**地改 level，就会撞上细分阶段唯一会静默出错的地方：相邻 patch 在共享边上裂开（T-junction），画面上是网格里一道道随视角变化的破口，而 GL 什么都没报。三条必须同时成立的规矩：

- `gl_TessLevelOuter[i]` 是**顶点 i 的对边**（不包含顶点 i 的那条边），不是“与顶点 i 相连的边”。配对算错不会报错，只会让密度分布跟你想的反一层。
- 一条边被两个 patch 共用，而两个控制点**各自独立地**跑自己的 TCS：所以这条边的 level 必须只依赖**该边两端点**的**对称**函数（长度、或 `z0+z1` 这类不区分顺序的深度项）。一旦把“当前顶点自己的深度”写进判据，两个邻居就会算出不同值，缝必然裂。
- 选 `equal_spacing`：它把边上的生成点放在边的**精确分数处**，因此只要两端 level 一致就闭合。`fractional_odd/even_spacing` 的内部分布更平滑，但匹配 level 时不总能补上。

`layout(vertices = N) out` 的 `N` 必须等于 `glPatchParameteri(GL_PATCH_VERTICES, N)`；两个值一个在 GLSL 里一个在 C++ 里，改其一忘其二是“patch 画不出东西”最常见的原因。

两个容易漏的运行时事实：

- **level 有上限**。`glGetIntegerv(GL_MAX_TESS_GEN_LEVEL, &cap)` 启动时读一次，CPU 侧夹住后再传给 TCS——直接写一个超出驱动承诺的值是 UB，不是“被静默丢弃”。
- **patch 管线里顶点阶段不能写最终的 `gl_Position`**（那是 TES 的产物）。这意味着 VS/TES 共用不了一个 VS，“一个程序既画三角形又画 patch”做不到——要两条通路实时对照就得建两个 `ShaderProgram` 按模式选。

把已有网格交给细分不需要第二份几何：三角形拓扑的索引流（如 `GeometryFactory::Sphere`，3 索引一个三角形）本身就是合法的 patch 流，`Mesh::DrawAs(GL_PATCHES)` + `glPatchParameteri(GL_PATCH_VERTICES, 3)` 即可。求值阶段拿到的是重心坐标，对球类形体应该把插值得到的**弦上点**重投影回表面（`normalize(p) * radius`）而不是直接用插值位置——后者只会把粗笼“拉平”，而法向也应取重投影方向而非插值粗笼法向。

🔗 **现场演示**：
- ① 两文件加载：[`../../src/demo/shader_file/main.cpp`](../../src/demo/shader_file/main.cpp)（GLSL 在 `src/demo/shader_file/assets/shaders/`）。
- ② 选择性几何阶段：[`../../src/demo/geometry_shader/main.cpp`](../../src/demo/geometry_shader/main.cpp)（V+Geom+F 内嵌）。
- ②' 多阶段**从文件**加载（V+Geom+F 全走 `CreateFromFiles({...})`）：[`../../src/demo/geometry_shader_file/main.cpp`](../../src/demo/geometry_shader_file/main.cpp)。
- ③ 全 5 阶段：[`../../src/demo/shader_stages/main.cpp`](../../src/demo/shader_stages/main.cpp)（GLSL 在 `Stages.h`）。
- ④ 阶段不只变换、还能**生成**几何：[`../../src/demo/menger_sponge/main.cpp`](../../src/demo/menger_sponge/main.cpp)（GLSL 在 `MengerShaders.h`）。门格海绵 level-L 恰有 20^L 个等大子立方体 ⇒ 索引就是一个 20 进制数，`gl_VertexID` 解码出中心、几何阶段展成立方体，`uniform int uLevel` **就是**解码循环的上界——调层级不重建任何东西，CPU 每帧只上传一个循环次数，全仓零顶点缓冲。同一 demo 的另一条臂用 transform feedback 把工作队列放在 GPU 上自行细分（`gldx::TransformFeedback` + `VertexArray::DrawTransformFeedback`）：捕获目标只能用 `CreateFromSources({...}, TransformFeedbackDesc{...})` 在链接前声明，因为 `layout(xfb_buffer)` 是 GLSL 4.30、超出 4.1 core 基线。
- ⑤ 把**现成的 `Mesh`** 当 patch 流画，并与 CPU 重网格在同一份缓冲上实时对照（`G`）：[`../../src/demo/textured_sphere/main.cpp`](../../src/demo/textured_sphere/main.cpp)——三段细分 GLSL 全内嵌在 `main.cpp`，`A` 键就是 §5.1 那三条防裂规矩的开关，`GL_MAX_TESS_GEN_LEVEL` 的读取与夹住也在其中；patch 程序链不出来时只打一条 `stderr` 并降级到 CPU 网格，而不是画空。

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
- 要发版本 / 触发 CI → [开发者文档的发布手册](../developer/release.md) 与 [AGENTS.md](../../AGENTS.md) 的 CI 段
- 想读权威用法 → [`../../src/main.cpp`](../../src/main.cpp)（hello-triangle）与 [`../../src/demo/`](../../src/demo/) 下的 `pbr_showcase` / `voxel_terrain` / 单功能 demo
