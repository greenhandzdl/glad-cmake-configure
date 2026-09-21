# 🟡 ③ 贴图与模型加载（异步两阶段）

前提：已完成 [② 几何与场景](3-geometry-scene.md)。这一章解决"把磁盘上的 PNG/FBX/OBJ/glTF 变成能画的东西"：慢的解码在后台线程，GL 上传留在渲染线程，两者由 `gfx::AssetManager` 隔开。

---

## 1. 基础用法：请求 → 每帧处理 → 取用

```cpp
gfx::AssetManager assets;                          // 内含线程池
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

---

## 下一步

- 加载好的贴图怎么"亮起来"（接 PBR 材质、光照） → [🟡 ④ 光照与 UBO 绑定](5-lighting-ubo.md)
- 想直接手写 `Texture2D` 上传（不经 AssetManager） → `Texture2D::Upload(desc)` 同样受上面的长度/尺寸先验约束，契约见 `src/gfx/texture/Texture2D.h`
