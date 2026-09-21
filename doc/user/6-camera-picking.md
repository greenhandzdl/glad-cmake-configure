# 🟢 ⑤ 相机与拾取

前提：已完成 [④ 光照与 UBO](5-lighting-ubo.md)。相机（`gfx::Camera`）与拾取（`gfx::Picking.h`）都是**纯数学、无 GL** 的类，因此可以随便在 worker 线程读、在渲染线程改——这一章给出两套现成控制模型和"鼠标点中物体"的三步用法。

---

## 1. 一台相机、两种控制模型

```cpp
gfx::Camera cam;
cam.SetPerspective(45.0f, aspect, 0.1f, 200.0f);   // 透视：fov(度)/宽高比/near/far
cam.SetViewportAspect(aspect);                      // 窗口 resize 时更新
```

- **环绕（orbit）模型**（主 demo）：`cam.Orbit(yawDelta, pitchDelta)` 转视角、`cam.Dolly(distDelta)` 推拉。角度用弧度。
- **自由飞行（fly）模型**（体素 demo）：`cam.SetYawPitch(yaw, pitch)` 定朝向（yaw 0 朝 −Z、正 yaw 向左、正 pitch 向上，钳位 ±90° 由调用方负责），`MoveForward/MoveRight/MoveUp(d)` 或 `Translate(delta)` 移动。距离 = 速度 × dt 留在你的游戏层——只有你知道这一帧是走、飞还是传送。
- 两套共用同一份 `orientation_ + position_`，可以按 `F` 之类的键随时切换，不必重建相机。

## 2. 透视 ⇄ 正交

```cpp
cam.SetOrthographic(worldHeight, aspect, nearZ, farZ);  // worldHeight = 目标平面处视盒的纵向世界尺寸
auto p = cam.ToggleProjection();                        // 运行期互切（demo 绑 Tab），返回切完后的那个
```

切换时 near/far 保留，正交视盒按当前环绕半径取景。正交模式下天空盒仍用透视投影画（`Camera::SkyboxViewProj()` 内部处理，你不用管）；拾取在两种投影下都正确（见下）。矩阵经 `ViewMatrix()/ProjectionMatrix()/ViewProjection()` 取缓存值——只读访问器可安全共享给 worker 线程。

## 3. 视锥剔除：`Frustum`

```cpp
gfx::Frustum frustum;
frustum.Extract(cam.ViewProjection());          // 每帧一次，从列主序 viewProj 提取六个平面
if (frustum.SphereVisible(center, radius)) { /* 画 */ }
```

给球体（`SceneNode` 自带包围球）或 AABB 做保守测试：`IntersectSphere/IntersectAABB` 返回 `Result::Outside/Intersect/Inside`，`Outside` 只在"完全在某平面之外"时才返回——宁可多画不误剔。

## 4. 鼠标点中物体：三步

```cpp
// ① 屏幕像素 → 世界射线。注意先把 GLFW 光标(窗口坐标)换算成 framebuffer 像素：
//    Retina 缩放窗口下两个坐标系差一个 scale。
gfx::Ray ray = gfx::PickRay(cursorX * xscale, cursorY * yscale, fbW, fbH,
                            cam.InverseViewProjection());
// ② 与候选包围球求交：每个候选一对 (球心, 半径)，按"从前往后"取最近命中
std::vector<std::pair<glm::vec3, float>> spheres = /* 各物体的包围球 */;
int hit = gfx::PickNearest(ray, spheres);       // 无命中返回 -1；单球测试用 RaySphere
```

③ 把 `spheres[i]` 映射回你的节点、选中、换材质参数——这步是你的应用逻辑（demo 里选中后高亮 `metallic`）。`PickRay` 用近/远两平面反投影构造射线，**与投影类型无关**：透视给出 eye→远点方向，正交得到正确的过像素垂线；射线含非有限分量时下游按 miss 处理（不崩）。

---

## 下一步

- 用同一个相机玩体素世界（射线挖方块） → [🟢 ⑥ 体素世界](7-voxel-basics.md)
