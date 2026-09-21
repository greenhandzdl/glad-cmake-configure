# 线程安全

引擎从**恰好一个线程**触碰 GL，同时仍用后台线程做耗时的 CPU 工作。本文列出使这
一点成立的具体机制，并逐条映射到落实它的代码。承重的不变量是：

> **不变量。** 每一次 OpenGL 调用（创建、上传、绘制，*以及*删除）都发生在渲染
> 线程上。工作线程永远只做 CPU 工作（文件 IO、解码、解析、数学），针对的是那些
> 尚未——且可能永远不会——成为 GL 对象的数据。

下面每一条，都是守住这条线的办法。

## 1. GL 上下文的线程亲和（强制，而非假设）

`RenderContext`（`src/gldx/core/RenderContext.{h,cpp}`）是守门人：

- `MarkAsRenderThread()` 设一个 `thread_local bool g_isRenderThread = true`。
  `src/main.cpp` 在拥有 GL 上下文的那个线程上、紧跟上下文创建之后调用它一次。
- `IsRenderThread()` 报告该标志。
- `AssertRenderThread(where)` 在当前线程没有该标志时，往 stderr 打印一行 `FATAL`
  并 `std::abort()`。

每个拥有或驱动 GL 的类，都在其每个公有方法入口和析构里调
`AssertRenderThread(...)`。已确认的调用点包括：`GLBuffer`、`UniformBuffer`、
`VertexArray`、`Sampler`、`Framebuffer`、`Texture2D`、`TextureCubeMap`、
`RenderTexture`、`ShaderProgram`、`Mesh`、`InstancedMesh`、`EnvironmentMap`、
`CascadedShadowMap`、`PostProcessChain`、`SkyboxRenderer`、`SpriteBatch`、
`RenderPasses.cpp` 里的各 `RenderPass::Execute`，以及 `Renderer::Render/Init/AddPass`。
这个检查既廉价又 fail-fast：一次错线程的 GL 调用会带着具名位置立刻崩溃，而不是
悄悄把上下文搞坏。

## 2. 两阶段资源管线（CPU 离线线程，GL 在线线程）

在 GL 边界处切开——见 `AssetManager`（`src/gldx/assets/`）：

- **Stage A** —— `RequestModel()` / `RequestTexture()` 把*路径*交给一个
  `ThreadPool`；工作线程跑 Assimp/STB 解码并构建普通 CPU 结构体
  （`LoadedModelData`、`Texture2DDesc`）。此时还不存在任何 GL 类型，所以离线
  线程没有可被误用的东西。
- **Stage B** —— `ProcessUploads()` **在渲染线程上**每帧跑一次，抽取已完成的
  future，然后才创建 GL 对象（`Mesh::Upload`、纹理创建）——这是解码数据变成
  GPU 状态的唯一场所。

因为 Stage A 按值 /`shared_ptr<const>` 返回不可变描述，而 Stage B 仅限渲染线程，
两阶段绝不会在某个 GL 对象上竞争。

## 3. `AssetManager` 中的同步原语

管理器用两把不同的锁，分别守护两处不同的共享结构
（`src/gldx/assets/AssetManager.h`）：

- `mutable std::mutex queueMutex_` —— 守护在途的 future 向量（工作队列）。在
  入队 / 抽取周围是短临界区。
- `mutable std::shared_mutex storeMutex_` —— 守护已完成资源的 map。查询取
  **共享**（读）锁；发布一个已完成资源取**独占**（写）锁。多读少写——正是帧
  循环的访问模式。
- Stage A 结果以 `std::future<std::expected<T, std::string>>` 承载，所以完成是
  通过 future 观察的（没有共享的可变解码状态）。

## 4. 所有权：只暴露句柄，绝不暴露可变 GL 对象

- `AssetManager` 不可拷贝，资源查询返回 `shared_ptr<const …>` / 值句柄；调用方
  无法从缓存里拿到一个可写的 GL 包装。
- 场景节点（`SceneNode`）持**非拥有**的 `const Mesh*` / `const PbrMaterial*`。
  被拥有的对象活在应用的渲染线程资源作用域里（`src/main.cpp`），于是 GL 对象的
  生命周期被钉在渲染线程上，不会散落到 CPU 图里。
- `RenderFrame`（逐帧输入）用指针/引用持有各子系统，且**不**拥有任何 GL 资源；
  它是个数据包，所以把它传给 pass 不会复制或移动某个 owner。

## 5. 在渲染线程销毁（靠所有权 + 析构断言）

GL 删除和创建一样有线程亲和。每个 GL 包装的析构都调 `AssertRenderThread`（例如
`~Texture2D`），所以离线线程释放一个资源，会像错线程绘制一样立刻 abort。正确性
因此由"把所有权保持在渲染线程上"（见 §4）来保证——这样销毁自然就落在那里——并由
析构断言来验证。

> 路线图说明：目前**没有**自动的延迟销毁队列（把一个该死的 GL id 交给渲染线程
> 稍后释放）。如今靠所有权设计使它不必要；若将来需要从一个非渲染线程释放资源，
> 那个队列就是预期的扩展。

## 6. 只可移动的 RAII（杜绝句柄被意外复制）

每个 GL 包装以及管理器/pass 都删除了拷贝构造与拷贝赋值
（`X(const X&) = delete; X& operator=(const X&) = delete;`）——见 `GLBuffer`、
`VertexArray`、`UniformBuffer`、`Sampler`、`Framebuffer`、`Texture2D`、
`TextureCubeMap`、`RenderTexture`、`ShaderProgram`、`Mesh`、`InstancedMesh`、
`SceneNode`、`RenderPass`、`ThreadPool`、`AssetManager`。于是一个 GL id 只有单一
owner；它不可能被悄悄复制、被双重释放，或从两处同时驱动。移动只能通过
`std::move`/`unique_ptr`，所以所有权转移始终显式。

## 7. 不跨线程抛异常的错误传播

可能失败的操作返回 `std::expected<T, E>` 而不抛异常——在 GL 边界（`ShaderProgram`
编译/链接）、在资源解码（`ModelLoader`、`ImageLoader`、`AssetManager` 的
future）、在环境生成（`EnvironmentMap`）里都是如此。这让失败处理保持显式，并避免
跨 `std::thread`/future 边界抛异常——在那里一个未捕获的异常会调用 `std::terminate`。
调用方检查 `has_value()` / `error()` 并自行决定策略。

作为最后一道安全网，`ThreadPool::WorkerMain` 把每个出队的 `task()` 包在
`try { … } catch (…) { }` 里：即使一个提交的任务抛了异常（如 `bad_alloc`），异常
也无法逃出工作线程的顶层把进程搞崩。惹事的那个任务被丢弃，与其关联的 `std::future`
以 `broken_promise` 呈现，由渲染线程上的消费者处理。这是在 `std::expected` 约定之
上的双保险，而非它的替代品——代码仍不应*依赖*"跨线程抛异常"。

## 8. 纯 CPU 对象天然可共享

有些类型既不持 GL、也不持可变的共享状态，所以无论哪个线程都安全：`Frustum`、
`Picking`、`Transform` 里的数学/工具，以及 `SceneNode` 的只读侧（缓存的世界矩阵 /
包围球）。它们每帧在渲染线程上算出（`Scene::Update`），供各 pass 读取；因为是值
语义且无 GL，它们不带任何亲和约束，可以在没有上下文的情况下检查或测试。场景图正是
刻意放在这一层的。

## 9. named module 不改变以上任何一条

自 Phase 6 起，引擎以 C++20 named module `gldx` 交付（见
[design.md](design.md) §7）。那是一个*链接/打包*层面的改动，不是并发层面的，线程
亲和的保证原封不动地挺过它：

- **单一渲染线程 TLS。** `g_isRenderThread` 活在 `RenderContext.cpp` 内的一个匿名
  namespace 里——一个单一的 `module gldx;` 实现单元——所以它在整个库里恰有一个
  定义。每个单元只能通过导出的 `RenderContext` 成员函数触达它，绝无第二份拷贝。
- **没有头文件内联的可变状态。** 没有任何带线程亲和的东西定义在头文件里，所以把
  头文件包进 `gldx.cppm` 不会分裂出一个单例或静态量。仅有的头作用域对象是
  `inline constexpr`/`inline const` 的值数据（GLSL 源码、布局常量）——不可变，
  所以跨模块边界共享它们构造上就无竞争。
- **断言覆盖完好。** 那次"仅把 include 改成 import"的重构没动过任何函数体；横跨
  23 个触碰 GL 的单元里那 90+ 个 `AssertRenderThread` 调用点原封未动。
- **global-module 类型是被共享、而非被复制。** GLAD/GLM 实体坐在每个单元的 global
  module fragment 里（经 `src/gldx/gmf.hpp`），所以 `GLuint`、`glm::vec3`、
  `std::mutex`… 在 `gldx` 里和在 `main.cpp` 里是同一批 global-module 类型——不存在
  第二份可能失同步的模块局部定义。

---

### 新代码速查清单

- 拥有或调用 GL？ → 在渲染线程上跑，并在每个公有入口（和析构）加一个
  `AssertRenderThread`。
- 共享缓存？ → 把读侧（`shared_mutex`）与写侧（`mutex`）状态分开；临界区保持
  短小。
- 公共资源类型？ → 删除拷贝，偏好 move + `unique_ptr`，暴露 `const` 句柄。
- 可能失败？ → 返回 `std::expected`，不要跨线程抛异常。
