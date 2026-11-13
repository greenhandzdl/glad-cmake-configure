# 🟡 ③ 贴图与模型加载（异步两阶段）

前提：已完成 [② 几何与场景](3-geometry-scene.md)。这一章解决"把磁盘上的 PNG/FBX/OBJ/glTF 变成能画的东西"：慢的解码在后台线程，GL 上传留在渲染线程，两者由 `gldx::AssetManager` 隔开。

---

## 1. 基础用法：请求 → 每帧处理 → 取用

```cpp
gldx::AssetManager assets;                          // 内含线程池
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

按"可能还没好"写代码：`Get*` 在就绪前永远返回空，绝不暴露半成品；模型同理用 `GetModel(key)`。想知道还有多少在途，用 `PendingCount()`。

🔗 **现场演示**：[`../../src/demo/model_loading/main.cpp`](../../src/demo/model_loading/main.cpp) 走完整链路 `RequestModel` → 每帧 `ProcessUploads` → `GetModel` 拿到后入场景绘制（并演示上面的三条拒收规则）。

## 2. 为什么必须两阶段

| 阶段 | 在哪个线程 | 做什么 | 产出 |
| --- | --- | --- | --- |
| **Stage A** | 后台工作线程（`ThreadPool`） | 只做 **CPU 解码**（stb 解图、assimp 解模型），绝不碰 GL | `Texture2DDesc` / `LoadedModelData`（纯 CPU 数据） |
| **Stage B** | 渲染线程，在 `ProcessUploads()` 里 | 把已完成的解码结果 `glGenTextures/glBufferData` 上传 | 真正的 GL 对象 |

**不要在后台线程里自己 `glGenTextures`**——那是引擎用 `AssertRenderThread` 强制执行的规则（一碰就 `abort`），背景见 [../developer/thread-safety.md](../developer/thread-safety.md)。错误用 `std::expected<T,E>` 传回，**不跨线程抛异常**；线程池顶层另有 `try/catch` 兜底，防止任务异常逃逸出 `std::thread` 触发 `std::terminate`——但那是安全网，你的代码不应依赖"抛异常跨线程"。解码异步、上传永远排在渲染线程：`Request*` 之后不能立刻假设资源可用，要跨帧轮询。

## 3. 资产文件本身的三条拒收规则（已验证）

加载器不信任资产文件里的自称，违反时模型照常加载、问题资源被拒：

- **纹理引用不许走出模型目录。** 材质（如 `.mtl` 的 `map_Kd`）里的绝对路径、盘符或含 `..` 的引用被 `ModelLoader` 拒绝，stderr 留一行 `names a path outside the model's own directory`。贴图请与模型同目录（子目录可以），用相对引用。
- **图片边长 ≤ `kMaxTextureSide`（16384），解码前读头部即校验。** 防"小文件解出巨量内存"的解压炸弹；错误以 `std::expected` 串返回（`image too large: ... is WxH`），由调用方自行落日志。
- **面索引越出本 mesh 顶点数的三角形整面丢弃**（保住"索引只指向自己顶点"的不变式，不打诊断）；顶点位置含 NaN/inf 会有 `[ModelLoader] ... positions are not finite` 的 stderr 行——NaN 顶点合法上传但永不光栅化，症状是"模型不见了"。

投放目录约定见 [`src/assets/models/README.md`](../../src/assets/models/README.md)；对应症状的速查在 [🧰 排错 §6](9-troubleshooting.md#6-报错--修复速查表)。

## 4. 模型导入是可选依赖（`GLDX_ENABLE_ASSIMP`）

整条模型导入路径背后只有一个重依赖：assimp。它由 CMake 选项 `GLDX_ENABLE_ASSIMP`（默认 `ON`）控制。用 `-DGLDX_ENABLE_ASSIMP=OFF` 配置时，assimp 子模块根本不拉取/不构建，`gldx` 链接时不带 assimp：

- 导出的 `gldx` 模块接口不变，`ModelLoader::Load` / `AssetManager::RequestModel` 签名照旧，故你的代码无需改一行就能在两种构建下编译。
- 但 `RequestModel` 拿到的会是空结果：`ModelLoader::Load` 返回一个 `std::unexpected("gldx built without Assimp: model import unavailable")` 并在 stderr 打一行 `[ModelLoader] <path>: model import is unavailable ...`，`GetModel(key)` 永远返回空。**纹理/图片加载（stb）不受影响**，体素、程序化几何、PBR 也完全照常。
- 换句话说：不要模型的纯体素/程序化产品可以关掉这个选项省掉一次重型子模块构建；要导 FBX/OBJ/glTF 就保持默认 `ON`。

## 5. 带骨架的资产（蒙皮）：`LoadSkinned` → `RiggedModel`

静态模型走 `Load`，**带骨架 + 蒙皮权重**的走 `LoadSkinned`。两者的两阶段划分完全一样（工作线程只做 CPU 解码、产出 `LoadedSkinnedModelData`；渲染线程才建 GL 对象），区别在第二阶段谁持有结果：静态模型是 `Mesh` + `Texture2D`，蒙皮模型是引擎的 `gldx::RiggedModel`。

```cpp
// Stage A（工作线程，绝不碰 GL）：只解码
auto loaded = gldx::ModelLoader::LoadSkinned(path);   // std::expected<LoadedSkinnedModelData, std::string>
if (!loaded) { /* HUD 打红字；无骨骼的文件在这里被拒，而不是画一具静态模型假装成功 */ }

// Stage B（渲染线程）：一次吃下骨架、全部 skinned mesh、贴图、材质
gldx::RiggedModel model;
model.Upload(std::move(*loaded));
if (!model.ready()) { /* 文件里没有可画的 skinned mesh */ }

// 每个 sampler 都必须是完整贴图，这些访问器就是那条保证
albedo.Bind(0);  // model.AlbedoOrFallback()
normal.Bind(1);  // model.NormalOrFallback()
model.BindPalette(skinProgram, palette);   // 一次性把 uBones[i] 传进去
for (const gldx::SkinnedMesh& mesh : model.meshes()) mesh.Draw();
```

三个**必须知道、否则会静默坏掉**的点：

1. **1×1 哑元贴图由引擎兜住，你不需要写 null 判断。** `AlbedoOrFallback()` / `NormalOrFallback()` / `MetallicOrFallback()` / `RoughnessOrFallback()` 永远返回一张完整的贴图（白 albedo、平法线 `(128,128,255)`、白标量）。这不是省事：Apple 的 GL 驱动遇到**指向不完整贴图的 active sampler 会丢掉整个 draw call**，症状是"模型凭空消失"且没有任何 GL 报错。`hasAlbedo()` / `hasNormal()` 等四个布尔量告诉你的才是"这个值是真是假"，着色器按它们分支。
2. **骨架分类不在 `RiggedModel` 里，在 `gldx::RigProfile`。** 拿到 `model.skeleton()` 后调 `RigProfile::Classify(skeleton)`，得到 21 个语义槽位（`hips`/`spine`/`head`/`legUp[2]`/`foot[2]`/`hand[2]`…）和**量出来**的 `side`/`up`/`forward` 三轴（髋→头、左右大腿相减，所以不假设导出器是 Y-up 还是 Z-up）。把三轴交给 `model.SetOrientation(rig.side, rig.up, rig.forward)`。**为什么要分开放**：骨名表只能有一份，`RiggedModel` 自己也猜一遍就等于两份表，而两份表一旦不一致就会把一具骨架**绑一半**——这是最难查的失败形态。同理，槽位没认出来时保持 `-1`、旋转变成静默空操作，所以请把 `rig.BindingReport()`（一行 `"20/20 slots bound"`）打进日志或 HUD：**静默降级比崩溃难查，降级路径必须能出声**。
3. **地面接触用 `gldx::GroundContact`，不要手写 bob。** 四个鞋底接触点（左右脚踝、左右脚趾）在 bind pose 下投到鞋底平面上，摆完姿势后 `Solve(palette)` 反解出骨盆该被抬/压多少，`ApplyLift(palette, lift)` 一次性挪整具角色。这样"两只脚交替踩地"就和腿的曲线绑在一起，不会漂。注意它的 `bindLowestOnUp` 参数要的是**最低 bind 顶点的投影**，不是关节位置——脚踝在鞋底之上好几厘米，用关节会整具角色浮起来且无人报告。

姿态本身（世界轴转角度折进骨骼局部系）用 `gldx::Pose` 的自由函数：`ResetToRest(skeleton, pose)` 起手、`RotateBone(pose, model.restWorld(), bone, axis, radians)` 逐关节叠加。`PoseRotation` 只取 rest world 的旋转部分——`restWorld⁻¹·Rw·restWorld` 字面展开会带出平移项，让关节点从父骨末端滑脱；这条反直觉但必须遵守。

完整可跑的范例是 [`anim-rigid`](10-demos.md)（它把步态曲线留在了 demo 里，只把骨架/姿态/地面/持有四件事交给引擎）。

---

## 下一步

- 加载好的贴图怎么"亮起来"（接 PBR 材质、光照） → [🟡 ④ 光照与 UBO 绑定](5-lighting-ubo.md)
- 想直接手写 `Texture2D` 上传（不经 AssetManager） → `Texture2D::Upload(desc)` 同样受上面的长度/尺寸先验约束，契约见 `src/gldx/texture/Texture2D.h`
