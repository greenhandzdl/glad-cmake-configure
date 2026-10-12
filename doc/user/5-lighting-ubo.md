# 🟡 ④ 光照、UBO 绑定与"黑屏第一课"

前提：已完成 [② 几何与场景](3-geometry-scene.md)。这一章解决"场景是纯色、没有明暗"和"什么都没画出来"两件事：怎么填光照数据，以及为什么绑 UBO 是第一道坎。

---

## 1. 描述这一帧的光：`LightSetup`

```cpp
gldx::LightSetup setup;
setup.sun.color     = {1.0f, 0.98f, 0.94f};   // DirectionalLight：color/intensity/direction（传播方向）
setup.sun.intensity = 3.2f;
setup.sun.direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.25f));
setup.points.push_back({{2, 1, 0}, 6.0f, {1, 0.5f, 0.2f}, 8.0f});  // PointLight{position,range,color,intensity}，打包时截到 32 个
setup.ambient      = {0.03f};                  // 没有 IBL 时的兜底环境光
setup.fogColor     = {0.35f, 0.5f, 0.65f};     // 线性雾；fogStart<=0 或 fogEnd<=fogStart 即禁用
setup.fogStart     = 40.0f;
setup.fogEnd       = 120.0f;
```

雾与阴影是**数据驱动**的（`LightSetup` + `RenderFrame` 字段），不是编译期分支；运行期开关怎么关见 [🔴 进阶](8-advanced.md)。

## 2. 灌进 UBO 并绑定（黑屏第一课）

```cpp
gldx::LightBuffer lights;            // binding = 1，引擎内部固定
lights.Init();                      // 渲染线程：按 sizeof(LightingBlockGpu) 建动态 UBO
lights.Update(setup, cameraPos);    // 每帧：CPU 端打包 std140 块并整块替换
lights.Bind();                      // glBindBufferBase(GL_UNIFORM_BUFFER, 1, handle)
```

`lighting` uniform block 是**全局共享接口**。若你写自己的 PBR pass 直接 `Renderer::Render()`，忘了绑定制服块，片段着色器读到 `nullptr` UBO → **全黑且无 GL error**（`glGetError` 抓不到）。GLSL 4.10 不允许在 block 上写 `layout(binding=N)`，所以绑定在 C++ 侧做——把自己的 block 映射到引擎的固定 binding，再每帧 `Bind()`：

```cpp
myProgram.SetBlockBinding("LightingBlock", gldx::LightBuffer::kBinding);  // 链接后设一次，binding = 1
lights.Bind();                                                           // 每帧，在渲染线程
// 阴影同理：SetBlockBinding("ShadowBlock", gldx::CascadedShadowMap::kShadowBinding)
```

`LightBuffer` 只暴露 `Init/Update/Bind/valid`——没有也不需要 `handle()`，别拿裸句柄自己 `glBindBufferBase`。当然，走 `BuildPbrPipeline()` 或 `BuildMinimalPipeline()` 时，只要几何 pass 在链里，引擎已把这两步都做了。

**遇到"全黑且无报错"，先查这一步。** 同类"忘了一步就静默无输出"的坑记在 [排错 §3](9-troubleshooting.md#3-黑屏且无任何报错忘了给-ubouniform-block-绑定)。

🔗 **现场演示**：[`../../src/demo/pbr_lighting/main.cpp`](../../src/demo/pbr_lighting/main.cpp) 把 `LightSetup` → `LightBuffer` → 绑定 → `BuildPbrPipeline` 跑成一张带明暗的球阵；级联阴影见 [`../../src/demo/shadow_csm/main.cpp`](../../src/demo/shadow_csm/main.cpp)。

## 3. 采样贴图：走 `Sampler`，别硬写过滤

```cpp
gldx::Sampler albedo;  albedo.wrap = gldx::Sampler::Wrap::Repeat;
                      albedo.minFilter = gldx::Sampler::MinFilter::LinearMipmapLinear;
                      albedo.maxAnisotropy = 8.0f;   // 各向异性；4.0/8.0/16.0 会向下钳制
albedo.Apply(0);      // 绑到 texture unit 0
```

引擎的 `PbrPass` 对 albedo/metal-rough/normal/emissive **一律按 sRGB 采样**；数据贴图（法线/遮罩等）要精确数值时须走自己的 pass 并显式配 `GL_LINEAR`。

自己写 lit pass 且**直渲到窗口**时记住 sRGB 是个往返：`Texture2DDesc::srgb = true` 上传后，`texture()` 出来的是线性辐射，而 glfw 没请求 `GLFW_SRGB_CAPABLE` 的默认帧缓冲并不做编码，所以在线性域算完光照要自己 `pow(c, vec3(1.0/2.2))` 再写 `FragColor`。少掉这步不会报错、也不会“看起来不对得显而易见”——只会整个画面发闷发暗，于是很容易去改贴图颜色或提亮度，把坑越埋越深（`PostProcessChain` 那条路径已由 ACES + gamma 收尾，所以只有绕过它的 pass 需要自己编码）。

但“发闷发暗”**常有两个独立原因**，而另一个量级更大：绕序。`Renderer::Init()` 设 `glFrontFace(GL_CCW) + glCullFace(GL_BACK)`，一个从外侧看呈顺时针的网格会把整个可见面剔掉，或者让你画出闭合形体**远侧的内表面**——症状同样是“没报错但闷暗”，而顶点法向看着完全正常。正确的鉴别顺序是**先查绕序、后改 gamma**：把背面剔除关掉，让 fragment 按 `gl_FrontFacing` 压暗背面——近侧亮远侧暗说明绕序对，反过来就是反了；确认绕序无问题再动色彩链路。两边都可能是真原因（本仓的 `textured_sphere` 就是先只修了 sRGB、发现球阵仍然不对，才用关掉剔除的手法定位到 `GeometryFactory::Sphere` 的旧索引序是 inside-out 的），所以别只查一半就下结论。绕序约定本身见 [② 几何 §1.1](3-geometry-scene.md)。

🔗 **现场演示**：
- 采样器对比：[`../../src/demo/texture_samplers/main.cpp`](../../src/demo/texture_samplers/main.cpp)——同一张 `Texture2D` 绑不同 `Sampler`（NEAREST vs LINEAR）分屏对比，Sampler 对象在 `SamplerPass.{h,cpp}`。
- 程序化生成贴图（不读盘）+ 完整 sRGB 往返的直渲 pass：[`../../src/demo/textured_sphere/main.cpp`](../../src/demo/textured_sphere/main.cpp)（三张 equirectangular 贴图含用 `Noise::Fbm3` 沿圆周采样以消除经度接缝的 planet 图）。
- 用 `gl_FrontFacing` 读绕序（上面那个鉴别手法的可操作版本，`C` 键开关背面剔除）：同上 [`../../src/demo/textured_sphere/main.cpp`](../../src/demo/textured_sphere/main.cpp)。

## 4. 环境光（IBL）与天空：`EnvironmentMap`

想摆脱"只有太阳 + 点光"的塑料感，程序化烘一套天空 IBL（自测可当天空盒背景）：

```cpp
gldx::EnvironmentMap env;
env.Generate(sunDir);       // 渲染线程烘焙 4 张贴图：HDR 天空 cube / 漫反射 irradiance /
                            // GGX prefilter 镜面（粗糙度存进 mip）/ BRDF 积分 LUT
frame.sky.env = &env;       // 之后每帧随 RenderFrame 交给引擎（同一份 env 既喂 IBL 也喂天空盒）
```

返回 `false` 表示有 shader 编译失败（会打日志）；`env.valid()` 为真后把 `frame.sky.box`（`SkyboxRenderer`）一并接上、并装一个 `SkyboxPass`，它就兼作背景（IBL 与天空盒两个用途共享 `frame.sky.env`，但由 `frame.ibl.enabled` 与是否装 `SkyboxPass` 各自独立开关）。

🔗 **现场演示**：
- IBL 环境光烘焙：[`../../src/demo/ibl_environment/main.cpp`](../../src/demo/ibl_environment/main.cpp)。
- 天空盒背景：[`../../src/demo/skybox/main.cpp`](../../src/demo/skybox/main.cpp)。

---

## 下一步

- 相机怎么动、鼠标怎么点中场景物体 → [🟢 ⑤ 相机与拾取](6-camera-picking.md)
- 想玩体素地形 → [🟢 ⑥ 体素世界](7-voxel-basics.md)
