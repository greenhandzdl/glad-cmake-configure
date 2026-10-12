# 🟢 ② 几何、材质与场景层级

前提：已完成 [① 核心骨架](2-core-setup.md)。这一章解决"往画面里放一个能看见的东西"：三个对象各司其职——**Mesh = 顶点数据（VBO/VAO/IBO）+ 可被引用；Transform = 位置；SceneNode = 把二者挂进层级**。

---

## 1. 造一个网格

```cpp
gldx::Mesh mesh;                              // move-only RAII，拥有 VAO/VBO/IBO
gldx::MeshData data = gldx::GeometryFactory::Sphere(0.5f, 48, 32);
mesh.Upload(std::move(data));                // Stage B：只能在渲染线程调用
```

- `GeometryFactory` 现成有 `Cube/Sphere/Plane`；要自定义就往 `MeshData` 里自填顶点/索引再 `Upload`。`Sphere` 的细分就是它的两个整数参数（`segments` 经线条数 / `rings` 纬线圈数），顶点数 `(segments+1)*(rings+1)`、索引数 `segments*rings*6`——想要“细分可控”只需把这两个数接给输入，不必自己写球体。
- chunk remesh 这类"几何变了但资源不重建"的场景用 `Mesh::Update(MeshData&&)`（整缓冲 orphan 重传、VAO 复用），不必销毁再 `Upload`。它也是交互式改网格能便宜到逐次按键生效的原因。

### 1.1 绕序：填索引前就要定下来的事

`Renderer::Init()` 设的是 `glEnable(GL_CULL_FACE) + glCullFace(GL_BACK) + glFrontFace(GL_CCW)`，而引擎着色器**无一使用** `gl_FrontFacing` 做双面翻转。所以自填索引时的约定是：**从顶点法向所指那一侧看，每个三角形都是逆时针**。工厂产出的 `Cube/Sphere/Plane/Quad` 都按这个约定缠绕；你自己填的时候拿一组具体顶点算 `cross(v1-v0, v2-v0)`，与那处的法向对一下符号就够了。

值得在入门阶段就把它看一眼，是因为**搞反了不会报错**：轻则少一半面，重则让你看见一个闭合形体**远侧的内表面**——而顶点法向、切线、纹理坐标全都看着正常，很容易当成光照或贴图问题去查。一个可靠的鉴别手法：关掉背面剔除，让 fragment 按 `gl_FrontFacing` 把背面压暗——**近侧亮、远侧暗**说明绕序对，明暗分布反过来就是反了。

### 1.2 两种细分：重网格 vs 硬件 patch

同一份 `MeshData` 可以走两条完全不同的细分通路，而且**不需要第二份几何**：

- **CPU 侧重网格**：改 `segments`/`rings` 重新生成 `MeshData` 并 `Mesh::Update`，顶点真的变多了，代价在显存与上传。
- **硬件细分（TCS/TES）**：`Mesh::DrawAs(GLenum mode)` 把索引流按 `glPatchParameteri(GL_PATCH_VERTICES)` 的分组交给细分阶段。`Sphere` 的索引流本来就是 3 索引一个三角形，所以 `mesh.Draw()` 与 `mesh.DrawAs(GL_PATCHES)` 画的是**同一个缓冲**——每个三角形成为一个 3 控制点的 patch，控制阶段逐边定 level，求值阶段把重心插值得到的弦上点重投影回球面。笼保持很粗（12×6 只有 91 顶点）也能渲染出圆轮廓，这是 CPU 通路做不到的。

> `DrawAs` 只为 `GL_PATCHES` 而存在（其余模式各有专属归属：实例化 → `InstancedMesh`，GPU 决定数量 → `VertexArray::DrawTransformFeedback`），且刻意忽略 `ranges_`：patch 网格永远是一个整体，中途截断会在非 patch 边界切开索引流。着色器侧的接线（`ShaderStage::TessControl/TessEvaluation` 与三段 GLSL）与自适应 level 的**防裂规矩**（共享边的 level 必须只由该边两端点**对称地**算出，否则相邻 patch 会在共享边上裂开）见 [8 §5.1](8-advanced.md)。

🔗 **现场演示**：
- [`../../src/demo/geometry_upload/main.cpp`](../../src/demo/geometry_upload/main.cpp) 专测 `Mesh::Upload` / `Update` 与自定义顶点缓冲的上传路径。
- 细分实时可调（键盘改 `segments`/`rings` → `Update` 原地重网格，HUD 打印换算出的 verts/idx）：[`../../src/demo/textured_sphere/main.cpp`](../../src/demo/textured_sphere/main.cpp)。
- 两条细分通路在同一份缓冲上对照（`G` 切换、`1`–`9` 设 level、`A` 屏幕空间自适应）+ 用 `C` 关剔除读绕序：同上 [`../../src/demo/textured_sphere/main.cpp`](../../src/demo/textured_sphere/main.cpp)。把笼压到 12×6 后“轮廓是多边形 ↔ 是真圆”的差别就是 TES 重投影的证据。
- `GL_PATCHES` 的五阶段完整装配（细分 + 几何阶段共用一条管线）：[`../../src/demo/shader_stages/main.cpp`](../../src/demo/shader_stages/main.cpp)。

## 2. 给一个材质

```cpp
gldx::PbrMaterial mat;
mat.baseColor  = glm::vec4(0.9f, 0.5f, 0.25f, 1.0f);
mat.metallic   = 0.6f;
mat.roughness  = 0.3f;
```

PBR 走金属/粗糙工作流；没有贴图时这几个标量就足够出画面（贴图的加载见 [③ 资源加载](4-assets-loading.md)）。

## 3. 挂进场景层级

```cpp
gldx::Transform t; t.translation = glm::vec3(0, 0.5f, 0);
gldx::SceneNode& node = scene.CreateRoot(t);
node.SetRenderable(&mesh, &mat);             // 节点持"非拥有"指针：mesh/mat 的生命周期归你
```

要点：

- `Scene` 是**纯 CPU** 的变换层级，每帧 `scene.Update()` 刷新世界矩阵与包围球，再展平成 `Renderables()/ShadowCasters()/PickTargets()` 供各 pass 消费。
- `SceneNode` 只存 `const Mesh*` / `const PbrMaterial*` 等**非拥有指针**——GL 对象的生命周期归你（通常等价于 `main.cpp` 里的成员/局部变量）。别把 `Mesh` 塞进临时表达式，一帧结束就被析构。
- 子层级（demo 里旋转的 carousel）用 `node.AddChild(transform)` 挂接（返回 `SceneNode*`），子节点的世界矩阵自动带上父链；顶层节点用 `scene.CreateRoot(transform)`。

## 4. 批量的同一网格：实例化

同一个 `MeshData` 画几百份、每份不同变换/颜色，用 `gldx::InstancedMesh`，别建几百个 `SceneNode`：

```cpp
gldx::InstancedMesh field;
field.Create(std::move(geo), std::move(instances));   // 返回 bool；gldx::Instance{model, color}
```

🔗 **现场演示**：
- 实例化场（几百个不同变换/颜色）：[`../../src/demo/instancing/main.cpp`](../../src/demo/instancing/main.cpp)。
- 网格+材质+旋转子层级（carousel，演示场景变换传播）的完整装配：[`../../src/demo/pbr_showcase/main.cpp`](../../src/demo/pbr_showcase/main.cpp)。

---

## 下一步

- 东西放好了，让它带上贴图 / 加载你自己的模型 → [🟡 ③ 贴图与模型加载](4-assets-loading.md)
- 画面是纯色但没明暗 → 还没接光照，见 [🟡 ④ 光照与 UBO 绑定](5-lighting-ubo.md)
