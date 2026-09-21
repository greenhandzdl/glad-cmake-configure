# 🟢 ② 几何、材质与场景层级

前提：已完成 [① 核心骨架](2-core-setup.md)。这一章解决"往画面里放一个能看见的东西"：三个对象各司其职——**Mesh = 顶点数据（VBO/VAO/IBO）+ 可被引用；Transform = 位置；SceneNode = 把二者挂进层级**。

---

## 1. 造一个网格

```cpp
gldx::Mesh mesh;                              // move-only RAII，拥有 VAO/VBO/IBO
gldx::MeshData data = gldx::GeometryFactory::Sphere(0.5f, 48, 32);
mesh.Upload(std::move(data));                // Stage B：只能在渲染线程调用
```

- `GeometryFactory` 现成有 `Cube/Sphere/Plane`；要自定义就往 `MeshData` 里自填顶点/索引再 `Upload`。
- chunk remesh 这类"几何变了但资源不重建"的场景用 `Mesh::Update(MeshData&&)`（整缓冲 orphan 重传、VAO 复用），不必销毁再 `Upload`。

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

---

## 下一步

- 东西放好了，让它带上贴图 / 加载你自己的模型 → [🟡 ③ 贴图与模型加载](4-assets-loading.md)
- 画面是纯色但没明暗 → 还没接光照，见 [🟡 ④ 光照与 UBO 绑定](5-lighting-ubo.md)
