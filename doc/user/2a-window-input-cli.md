# 🟢 ①·补 窗口·输入·命令行：gldxwin 与 gldxcli

前提：已读完 [① 核心骨架](2-core-setup.md)（知道三行引入、`MarkAsRenderThread`、`Renderer`、`RenderFrame` 骨架）。这一章把 [① 核心骨架](2-core-setup.md) 里一笔带过的**窗口库 `gldxwin`** 和**命令行库 `gldxcli`** 摊开讲透——建窗与生命周期、按键/字符/鼠标/滚轮输入、光标控制、窗口几何、截图、多窗口，以及脚本化的功能开关。每个特性都给**能直接编译的完整代码**，并链到仓库里**正在这么用**的 demo，边讲边跑。

> 这一章只管"窗口和输入怎么用"，不碰渲染细节。`gldxwin` 刻意 **engine-agnostic**：它不 `import gldx`、没有 `Renderer`、只链接 GLFW + GLAD。你自己在 demo 里 `import gldx;` 并把 renderer 挂在 `Window` 上。为什么这么分层见 [../developer/design.md](../developer/design.md)。

目录：
- [§1 建窗与生命周期钩子](#1-建窗与生命周期钩子)
- [§2 帧循环：App::Run 与 FrameInfo](#2-帧循环apprun-与-frameinfo)
- [§3 输入：事件回调](#3-输入事件回调)
- [§4 输入：轮询当前状态](#4-输入轮询当前状态)
- [§5 光标控制与窗口几何](#5-光标控制与窗口几何)
- [§6 截图、用户数据与 Handle 逃生舱](#6-截图用户数据与-handle-逃生舱)
- [§7 多窗口：一个循环驱动 N 个独立 context](#7-多窗口一个循环驱动-n-个独立-context)
- [§8 命令行：gldxcli::Flags](#8-命令行gldxcliflags)

---

## 1. 建窗与生命周期钩子

`WindowDesc` 是"怎么把窗开起来"的纯数据，字段都有合理缺省（800×600、标题 `"gldx"`、可缩放），你只改在意的几个。`Window` **构造即**建 GLFW 窗 → `MakeContextCurrent` → `gladLoadGL`；建不出来时 `Ok()` 为 `false`，此后 `Run()` 会跳过它，退化成了"没事可画"而非崩溃。

三个生命周期钩子把"GL 资源的生老病死"框死在**上下文 current 且处于渲染线程**的窗口里：

| 钩子 | 触发时机 | 该做什么 |
| --- | --- | --- |
| `OnCreate` | 首帧之前**仅一次** | `MarkAsRenderThread()`、建全部 GL 资源（program/VAO/FBO/UBO/纹理） |
| `OnFrame` | 每一帧 | 填 `RenderFrame` 并 `renderer.Render(frame)` |
| `OnDestroy` | GLFW 窗销毁之前、上下文仍 current | 释放 GL 资源（此刻还能安全地 `glDelete*`） |

完整可编译的最小骨架：

```cpp
#include "gldx/core/Platform.h"   // 文本 include：GLAD-before-GLFW 顺序 + GLFW_PLATFORM_* 宏
import gldxwin;                   // 只用窗口/输入时其实只要它；下面同时 import gldx 是为了真渲染

gldx::win::WindowDesc desc;
desc.width     = 1024;
desc.height    = 720;
desc.title     = "my window";
desc.resizable = true;
gldx::win::Window window(desc);
if (!window.Ok()) return 1;                    // 建不出窗：干净退出

window.SetCloseOnEsc(true);                    // 默认即 true；用 Esc 做别的事（指针锁定/菜单）时设 false

window.OnCreate([](gldx::win::Window&) {
    gldx::RenderContext::MarkAsRenderThread();  // 声明本线程为唯一渲染线程（引擎侧规则）
    // ... 在这里建你的 program / VAO / FBO / UBO / 纹理 ...
});
window.OnDestroy([](gldx::win::Window&) {
    // ... 在这里释放上面建的所有 GL 资源，此刻上下文仍 current ...
});
```

> `OnCreate` 顶部那句 `MarkAsRenderThread()` 由**你**调，不是 `gldxwin` 调——它必须保持 engine-agnostic。规则细则见 [../developer/thread-safety.md](../developer/thread-safety.md)。
>
> **别自己碰 GLFW 回调**：`gldxwin` 已接管 key/char/mouse/cursor/scroll 的 GLFW trampoline，并占用了窗口的 GLFW user-pointer。不要对 `Handle()` 调 `glfwSetWindowUserPointer` / `glfwSet*Callback`；要挂自己的数据用 §6 的 `SetUserData`，或在 lambda 里直接捕获。

🔗 **现场演示**：几乎所有 demo 都是这个骨架；单窗最干净的例子是 [`../../src/demo/shader_file/main.cpp`](../../src/demo/shader_file/main.cpp)（`OnCreate` 建 program、`OnFrame` 裸调 GL 画全屏三角）。

---

## 2. 帧循环：App::Run 与 FrameInfo

`App` 是进程级 Meyers 单例：第一次 `Get()` 跑 `glfwInit` 并设好 GL 4.1 core hints（macOS 自动带 forward-compat）；`glfwTerminate` 放在静态析构里，保证晚于所有 `Window` 析构，GL 拆解不会撞上一个已死的上下文。构造 `Window` 会自动把单例拉起来，顺序无需你操心。

`App::Run()` 在**拥有窗口的那个线程**上按序驱动每一个存活窗口，`return 0` 表示正常退出、非 0 表示 `glfwInit` 从未成功。`RunOptions.quitAfterSeconds > 0` 会让循环到点自动关掉所有窗——这是无头回归扫描（`--quit-after`）能自行结束的原因。

每帧回调收到一个 `FrameInfo`，把大家各自重复推导的东西都备好了：

```cpp
struct FrameInfo {
    Window* window;         // 就是本帧这个窗（拿原生句柄用 window->Handle()）
    int     fbWidth;        // framebuffer 像素宽（Retina 下 ≈ 逻辑宽的 2 倍）
    int     fbHeight;       // framebuffer 像素高
    double  time;           // 自 Run() 起流的秒数
    double  dt;             // 距本窗上一帧的秒数
    double  smoothedFps;    // 固定 0.5s 窗口内的平均帧率
};
```

于是主循环末尾只需：

```cpp
window.OnFrame([&](gldx::win::FrameInfo& info) {
    gldx::RenderFrame frame;
    frame.fbWidth     = info.fbWidth;      // 视口用像素尺寸，不是逻辑尺寸
    frame.fbHeight    = info.fbHeight;
    frame.smoothedFps = info.smoothedFps;  // 想显示 FPS 直接拿来用
    // const double t = info.time;         // 驱动动画：秒级连续时钟
    // const float  dt = (float)info.dt;   // 帧率无关的移动：位移 = 速度 × dt
    renderer.Render(frame);
});

return gldx::win::App::Get().Run({flags.quitAfter()});
```

想要一个不随帧走的连续时钟（例如传给 shader 的 `uPhase`），用 `App::Now()`（`glfwGetTime` 的封装，秒）：

```cpp
const double phase = gldx::win::App::Now();   // 免去 demo 各自直接 glfwGetTime
```

🔗 **现场演示**：[`../../src/demo/debug_draw/main.cpp`](../../src/demo/debug_draw/main.cpp) 用 `info.time` 驱动网格起伏与自动环绕；[`../../src/demo/shader_stages/main.cpp`](../../src/demo/shader_stages/main.cpp) 用 `App::Now()` 作动画相位。

---

## 3. 输入：事件回调

`gldxwin` 把 GLFW 的输入事件翻译成**可移植枚举**（`Key` / `KeyAction` / `MouseButton` / `Vec2d`），所以 demo 代码里**不出现任何 `GLFW_*` 常量、不 `#include <GLFW/glfw3.h>`**。回调都在渲染线程、`Run()` 的 `glfwPollEvents` 内触发；**每类事件后设的赢**，传一个空 `std::function` 即取消订阅。

| 回调 | 签名（lambda 参数） | 用途 |
| --- | --- | --- |
| `OnKey` | `(Window&, Key, KeyAction, int mods)` | 按键按下/释放/重复 |
| `OnChar` | `(Window&, unsigned codepoint)` | 文本输入（Unicode 码点） |
| `OnMouseButton` | `(Window&, MouseButton, KeyAction, int mods)` | 鼠标键 |
| `OnCursor` | `(Window&, Vec2d)` | 光标移动（**窗口逻辑坐标**） |
| `OnScroll` | `(Window&, double dx, double dy)` | 滚轮 |

`KeyAction` 有 `Press / Release / Repeat`。`Key` 覆盖 `A..Z`、`Num0..Num9`、`Escape/Tab/Space/Enter/Backspace`、四方向键、`LeftBracket/RightBracket/Minus/Equal`、`LeftControl/LeftShift`，其余用 `Unknown`。完整可用的拖拽环绕 + 右键拾取（含 FPS 无关的 `mods` 忽略写法）：

```cpp
window.OnKey([](gldx::win::Window& w, gldx::win::Key key,
                gldx::win::KeyAction action, int /*mods*/) {
    if (action != gldx::win::KeyAction::Press) return;
    switch (key) {
        case gldx::win::Key::R: renderer.rebuild(); break;   // 按 R 重建资源
        case gldx::win::Key::Escape: w.Close(); break;        // 也可交给默认的 SetCloseOnEsc
        default: break;
    }
});

window.OnChar([](gldx::win::Window&, unsigned int codepoint) {
    if (codepoint >= '0' && codepoint <= '9') selectTool(codepoint - '0');   // 数字键选工具
});

window.OnScroll([](gldx::win::Window&, double /*dx*/, double dy) {
    view.dolly((float)dy * -0.5f);              // 滚轮推拉相机
});

window.OnCursor([&view](gldx::win::Window&, gldx::win::Vec2d pos) {
    if (!view.dragging) return;
    view.yaw   -= (float)(pos.x - view.lastX) * 0.006f;   // 拖动改变朝向
    view.lastX = pos.x; view.lastY = pos.y;
});
```

鼠标键按下/抬起判定的完整例子（拖拽开始=左键 Press，拾取=右键 Press）：

```cpp
window.OnMouseButton([&view](gldx::win::Window& w, gldx::win::MouseButton button,
                              gldx::win::KeyAction action, int) {
    const gldx::win::Vec2d c = w.CursorPos();
    if (button == gldx::win::MouseButton::Right) {
        if (action == gldx::win::KeyAction::Press) {           // 右键点下：记下归一化坐标待拾取
            const gldx::win::Vec2d sz = w.Size();
            view.pickX = (float)(c.x / sz.x);
            view.pickY = (float)(c.y / sz.y);
            view.pickPending = true;
        }
        return;
    }
    if (button != gldx::win::MouseButton::Left) return;
    view.dragging = (action == gldx::win::KeyAction::Press);   // 左键按住=拖拽环绕
    view.lastX = c.x; view.lastY = c.y;
});
```

> **坐标系提醒**：`OnCursor` 给的是**窗口逻辑坐标**，而拾取射线要的是 **framebuffer 像素**。Retina 缩放窗口下两者差一个 content scale，所以 demo 习惯在点击时把坐标归一化到 `[0,1]`，建射线时再乘 `fbWidth/fbHeight`（见 [⑤ 相机与拾取](6-camera-picking.md#4-鼠标点中物体三步)）。

🔗 **现场演示**：[`../../src/demo/camera_picking/main.cpp`](../../src/demo/camera_picking/main.cpp) 的 `OnCursor` + `OnMouseButton`（第 125–150 行）就是"拖拽环绕 + 右键拾取"的完整实现，全程零 GLFW 常量。

---

## 4. 输入：轮询当前状态

事件回调适合"某一刻发生了什么"；持续按住的状态（每帧都想知道）用轮询更省心：

```cpp
window.OnFrame([&](gldx::win::FrameInfo& info) {
    using enum gldx::win::Key;
    if (window.KeyIsDown(W))      move(0.0f,  +1.0f * info.dt);   // 按住 W 前进
    if (window.KeyIsDown(S))      move(0.0f,  -1.0f * info.dt);
    if (window.MouseIsDown(gldx::win::MouseButton::Left)) {
        const gldx::win::Vec2d m = window.CursorPos();             // 当前光标位置
        look(m);
    }
    renderer.Render(buildFrame(info));
});
```

可用轮询：`KeyIsDown(Key)`、`MouseIsDown(MouseButton)`、`CursorPos()`（返回窗口坐标 `Vec2d`）。

> **多窗口的坑**：轮询读的是"**当前上下文所属窗**"的状态（GLFW 的规则）。`Run()` 在每窗帧之前把它设为 current，所以**单窗 demo** 与**任意窗的 `OnFrame` 内**轮询本窗都正确；但在多窗循环里，只有当前 current 的那个窗答得对——拿不准就改用 §3 的事件回调。

🔗 **现场演示**：[`../../src/demo/voxel_terrain/main.cpp`](../../src/demo/voxel_terrain/main.cpp) 用 `KeyIsDown(W/S/A)` 轮询驱动第一人称持续移动（按住才走、松手就停），并用 `edge()` 助手把 `KeyIsDown(F/Tab/…)` 的轮询转成"按下瞬间触发一次"的边沿。

---

## 5. 光标控制与窗口几何

光标的可见/锁定，以及窗口位置尺寸，都在 `Window` 上：

```cpp
window.SetCursorVisible(false);            // 隐藏系统光标
window.SetCursorCaptured(true);            // 隐藏 + 锁定到中心：FPS 式视角控制（配合 RawMouseInput）
window.SetRawMouseInput(true);             // 开启 GLFW raw 光标移动模式（若平台支持）
window.SetCursorPos({window.Width() / 2.0, window.Height() / 2.0});   // 重置回中心（指针锁定后消漂移）
```

指针锁定的典型完整用法（用 §3 的 `OnKey` 里按 `F` 切换）：

```cpp
bool captured = false;
window.OnKey([&](gldx::win::Window& w, gldx::win::Key key,
                  gldx::win::KeyAction action, int) {
    if (action == gldx::win::KeyAction::Press && key == gldx::win::Key::F) {
        captured = !captured;
        w.SetCursorCaptured(captured);
        if (captured) w.SetRawMouseInput(true);
    }
});
```

几何查询/动作（`Vec2d{x,y}` 是 `double`；刻意不用 `glm::vec2`，因为 `gldxwin` 不依赖引擎）：

```cpp
const gldx::win::Vec2d p   = window.Pos();              // 屏幕上的左上角
window.SetPos({100.0, 80.0});                           // 摆放窗口（多窗平铺常用）
const gldx::win::Vec2d lg = window.Size();              // 逻辑（内容）尺寸
const gldx::win::Vec2d fb = window.FramebufferSize();   // 像素尺寸（Retina ≈ 2x）
window.SetTitle("now with 100 blocks");                 // 运行期改标题
window.Focus();                                         // 抢焦点
```

> 建多窗时把每个 `SetPos` 平铺开，能让它们同屏互不遮挡——见 §7 的例子。

---

## 6. 截图、用户数据与 Handle 逃生舱

**截图**：把上一个 pass 留在 `GL_VIEWPORT` 里的区域读成 PNG。引擎无关的 `gldxwin` 只负责"设为 current + 读像素"，PNG 编码交给引擎侧的 `gldx::EncodeScreenshot`。

```cpp
window.OnFrame([&](gldx::win::FrameInfo& info) {
    renderer.Render(buildFrame(info));
    static bool done = false;
    if (!done && info.time >= 2.0) {                 // 第 2 秒抓一帧
        done = true;
        if (info.window->CaptureScreenshot("shot.png"))
            std::printf("captured shot.png\n");
        info.window->Close();
    }
});
```

**用户数据槽**：一个不拥有的 `void*`，把 demo 自己的 renderer/资源挂回窗口，配 `UserDataAs<T>()` 取回：

```cpp
struct MyApp { gldx::Renderer renderer; /* ... */ };
auto app = std::make_unique<MyApp>();
window.SetUserData(app.get());
window.OnCreate([](gldx::win::Window& w) { w.UserDataAs<MyApp>()->renderer.Init(); });
```

**`Handle()`**：只为你确实需要、而 `gldxwin` 故意不代理的窗口系统能力留的逃生舱（原始鼠标移动、剪贴板、手柄、文件拖放）。判断准则是"**低频且 GLFW 类型化 → 别代理；高频且类型干净 → 代理到方法上**"。常规输入/几何/上下文检查/截图都有专门方法，正常 demo 不该用到 `Handle()`。

`ContextIsCurrent()` 是多窗循环里每帧的自证——断言"我这个窗的上下文确实是当前线程 current"：

```cpp
if (!info.window->ContextIsCurrent())
    std::fprintf(stderr, "context mismatch on window %d\n", index);
```

🔗 **现场演示**：截图连拍见 [`../../src/demo/multi_viewport/main.cpp`](../../src/demo/multi_viewport/main.cpp)（第 119–128 行，冻结共享时钟后逐窗 `CaptureScreenshot`）；`ContextIsCurrent` 每帧断言见 [`../../src/demo/render_passes/View.cpp`](../../src/demo/render_passes/View.cpp)。

---

## 7. 多窗口：一个循环驱动 N 个独立 context

一个 GLFW 窗 = 一个 GL context = 一个**私有对象命名空间**。`App::Run` 每帧先 `glfwMakeContextCurrent(该窗)` 再触发它的 `OnFrame`。因此跨窗**不能共享任何 GPU 对象**（program/VAO/纹理/FBO/UBO 都得各窗在**自己的** `OnCreate` 建、`OnDestroy` 毁）；能共享的只有 **CPU 真值**（`std::atomic` + 后台 worker 推进动画时钟）。

> 下面 §7 的骨架是**同步、同场景、同等级**这一种最典型的用法；§7.1–§7.4 再把窗口拉向彼此不同的四个正交维度——异步时钟、per-window 剔除、LOD/视距分档、以及一个与其余窗完全无关的 GL 上下文。

一个能跑的完整多窗骨架（N 个窗、每窗私有 renderer、共享一个原子时钟，逐窗截图能像素对齐）：

```cpp
#include <atomic>
#include <memory>
#include <vector>

struct Shared { std::atomic<double> phase{0.0}; };   // 跨窗只共享 CPU 真值

int main(int argc, char** argv) {
    const gldx::cli::Flags flags(argc, argv, {}, {"windows"}, "multi");
    const int total = std::max(1, flags.integer("windows", 3));

    Shared shared;
    // 析构顺序关键：Window::~Window 会回调进各自的 View，所以窗口必须先于 View 死。
    std::vector<std::unique_ptr<gldx::win::Window>> windows;
    std::vector<std::unique_ptr<MyView>>           views;   // MyView 每窗一个，持自己那套 GL 资源

    for (int i = 0; i < total; ++i) {
        auto view  = std::make_unique<MyView>();            // index=i, total, &shared ...
        view->state = &shared;
        MyView* v = view.get();

        gldx::win::WindowDesc desc;
        desc.width = 640; desc.height = 420;
        const std::string title = "win " + std::to_string(i + 1) + "/" + std::to_string(total);
        desc.title = title.c_str();                         // title 仅构造期被读
        auto window = std::make_unique<gldx::win::Window>(desc);
        if (!window->Ok()) return 1;

        window->SetPos({60.0 + i * 660.0, 120.0});         // 平铺让 N 个窗同屏
        window->OnCreate   ([v](gldx::win::Window&)       { v->Create(); });        // 内部先 MarkAsRenderThread
        window->OnFrame    ([v](gldx::win::FrameInfo& f)  { v->Frame(f); });
        window->OnDestroy  ([v](gldx::win::Window&)       { v->Release(); });
        window->OnMouseButton([v](gldx::win::Window&, gldx::win::MouseButton b,
                                    gldx::win::KeyAction a, int) { v->Button(b, a); });
        window->OnCursor   ([v](gldx::win::Window&, gldx::win::Vec2d p) { v->Drag(p); });

        windows.push_back(std::move(window));
        views.push_back(std::move(view));
    }

    const int rc = gldx::win::App::Get().Run({flags.quitAfter()});
    windows.clear();   // 先毁窗（触发 OnDestroy→Release），确认 View 还活着再毁它
    views.clear();
    return rc;
}
```

要点复盘：
- **每窗的 GL 资源在各自 `OnCreate` 建、`OnDestroy` 毁**，`OnCreate` 顶部 `MarkAsRenderThread()`；两个 demo 里的 `View::Create/Release` 就是照此办理。
- **析构顺序**：`Window` 必须先于它所依赖的 `View`/renderer 死掉（所以 demo 显式 `windows.clear()` 早于 `views.clear()`）。
- **同步靠 CPU 原子**：worker 线程推进 `std::atomic<double> phase`，各窗读同一真值渲染，因此逐窗 `CaptureScreenshot` 能像素一致（`multi_viewport --windows 3 --shot-at …` 实测三张截图逐字节相同）。

🔗 **现场演示**：
- 多窗同步渲染**同一场景**：[`../../src/demo/multi_viewport/`](../../src/demo/multi_viewport/)（`main.cpp` 只做连线，`View.{h,cpp}` 是每窗私有资源，`SharedState.{h,cpp}` 是原子时钟 + worker）。
- 多窗 + **一个 for 循环给不同窗装配不同 `RenderPass` 子类**：[`../../src/demo/render_passes/`](../../src/demo/render_passes/)（每窗一个 context，`View::Create` 里遍历工厂表 `AddPass`）。

### 7.1 同步 ↔ 异步：共享时钟只是选项之一

上面骨架里"所有窗读同一个 atomic phase"是**同步**这一种策略，不是唯一策略。窗口完全可以各跑各的：同步窗读共享原子，异步窗用 `FrameInfo::dt` 累积自己的私有相位——两者能在同一个 `App::Run` 里共存。

```cpp
// 同步窗：读那一个共享原子（各同步窗每帧拿到同一个数）
const bool sync = (profile.clock == Clock::Sync);
if (sync) phase = state->syncPhase.load(std::memory_order_relaxed);
// 异步窗：谁都不读，按本窗 dt 自己走，还能各自不同速率
else      phase += info.dt * kSpinSpeed * profile.rate;
```

证据就在 HUD：`flagship` 与 `rear-guard` 两个同步窗每帧打印**相同**的 `phase`（同一原子），而 `scout`（`rate 0.35`）与 `drifter`（`rate 1.0`）两个异步窗的 `phase` 既不同于同步对、也彼此不同。实测 `--shot-at 2`：同步两窗 phase 都是 `3.84`，异步窗是 `2.24` / `3.24`。

### 7.2 不同视角 → 不同 culling：同一批物体，各窗看到的不是同一批

把 `Frustum` 接进 `GeometryPass`，每窗用自己的相机算视锥，**同一份 CPU 物体场**就被剔成不同可见集——"有些窗显示、有些窗不显示"是逐窗可数的：

```cpp
const glm::mat4 viewProj = camera.ViewProjection();
gldx::Frustum frustum; frustum.Extract(viewProj);
gldx::RenderFrame f;
f.frustum = &frustum; f.scene = &scene;   // GeometryPass 逐节点 SphereVisible
renderer->Render(f);
// 回来后 f.visibleCount / f.totalNodes 就是"本窗这一帧看到几个 / 场里共几个"
```

demo 里 `rear-guard` 与 `flagship` **共享同一时钟、同一场**，只把眼睛绕到对面 180°，`visible` 就从 `23/38` 变成 `24/38`——保留的恰是对方剔掉的那批。再把远平面 / FOV 当"视距档位"：`scout` 用 `far 12 / fov 28` 只剩 `6/38`，`drifter` 用 `far 60` 有 `28/38`。

### 7.3 不同等级：LOD 与视距按窗分档

"建窗意味着不同等级"可以就是**每窗一套渲染档位**：同一场，`lodSeg`（球体细分）与 `far`（视距）按窗给不同值，画质与可见量一起分档。demo 的五档从 `lod 32 / far 300` 的旗舰一路降到 `lod 8 / far 12` 的斥候（低模球体肉眼可见地"起棱"）。

### 7.4 完全无关的 GL 上下文：island 窗

最强的隔离：某个窗与其它窗**什么都不共享**——不共享场、不共享 pass 链、不共享时钟，甚至不建 `Renderer`。它只 `glClear` 自己的默认帧缓冲、用 `DebugDraw` 画一套自己的线框：

```cpp
if (profile.independent) {
    glClearColor(0.05f, 0.05f, 0.07f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    debug->Clear();
    const glm::quat q = glm::angleAxis((float)phase,
                                       glm::normalize(glm::vec3(0.3f, 1.0f, 0.15f)));
    for (const auto& e : kCubeEdges)                 // 自己转动的线框立方
        debug->PushLine(q * (kCubeCorners[e[0]] * 2.0f),
                        q * (kCubeCorners[e[1]] * 2.0f), col);
    debug->Draw(camera.ViewProjection());
    DrawHud(info, -1, -1);
    return;                                          // 不进任何共享渲染路径
}
```

它证明：一进程、一渲染线程里，两个 context 可以真正互不相干——除了你主动经 `SharedState` 传过去的那点 CPU 原子，GPU 侧没有任何东西泄漏到别的窗。

🔗 **现场演示（本节全部维度）**：[`../../src/demo/multi_window_levels/`](../../src/demo/multi_window_levels/)——`Profiles.{h,cpp}` 是五个"等级"档位的定义表，`View.{h,cpp}` 按 `Profile` 在 `Create/Frame` 里分同步/异步、接 `Frustum` 逐窗剔除、按 LOD 建场、并特判 island 独立上下文，`SharedState.{h,cpp}` 是只有同步窗才读的那一个原子时钟。跑 `./output/multi_window_levels --shot-at 2 --shot-prefix mwl_` 一次抓五张，HUD 上 `SYNC/ASYNC`、`phase`、`visible x/38`、`lod`、`far` 逐窗对照。

---

## 8. 命令行：gldxcli::Flags

`gldx::cli::Flags` 是纯标准库的参数解析器，不依赖 `gldx` 也不依赖 `gldxwin`。它存在的意义是：**让一个特性能从脚本里强制开/关**，于是"逐个 toggle、比对画面"的回归扫描可自动化：

```bash
./output/voxel_terrain --off fog,water --quit-after 11
./output/pbr_showcase  --on debug     --quit-after 6
```

构造时声明这个可执行能接受的两类名字——`features`（`--off/--on` 逗号列表能操作的名字）和 `options`（带值的 `--name value`）：

```cpp
const gldx::cli::Flags flags(argc, argv,
    /*features*/ {"shadow", "ibl", "bloom", "debug", "instances", "sky"},
    /*options */ {"windows", "yaw", "pitch", "radius", "shot-at", "shot-prefix"},
    /*program*/ "my_app");
if (flags.wantsHelp()) { flags.printUsage(); return 0; }   // --help 打印真正被识别的名字
```

读取（`on` 的优先级：`--on` 覆盖 `--off` 覆盖内建缺省，脚本能从列表里翻回单个名字）：

```cpp
const bool shadow = flags.on("shadow", /*defaultOn=*/true);   // 特性开关：给缺省态
frame.shadow.enabled = shadow;

const int    winN = flags.integer("windows", 3);              // 整数（越界饱和，NaN→下界）
const double sec  = flags.number ("shot-at", -1.0);           // 双精度
const float  yaw  = flags.real    ("yaw", 0.7f, -6.28f, 6.28f);  // 角度/距离，带钳位，NaN/inf→缺省
const std::string model = flags.string("model", "robot.fbx"); // 原样字符串（路径、模式名）
const double quit = flags.quitAfter();                        // --quit-after 秒数，0=不自关
```

> `integer`/`real` 的边界处理是**刻意的**：`argv` 是用户输入，`strtod` 会乐意吃下 `"inf"`/`"nan"`/`"1e300"`，把它们强转 `int` 是 UB；所以钳位发生在值还是 `double` 的时候，NaN/越界会**饱和或回落缺省**而非污染相机。一个 option 名要么当数值、要么当字符串声明，别二者都试。

未识别的开关/特性会往 stderr 打一行提示（`--help lists the switches`），拼错的脚本参数不会静默通过。`--quit-after` 与 `run.sh`、CI 无头扫描配套：`Run({flags.quitAfter()})` 到点自关。

🔗 **现场演示**：`--help` 看全表在 [`../../src/demo/pbr_showcase/main.cpp`](../../src/demo/pbr_showcase/main.cpp)（features/options 全列表）；`--windows/--shot-at/--shot-prefix` 用法见 [`../../src/demo/multi_viewport/main.cpp`](../../src/demo/multi_viewport/main.cpp)（第 61–69 行）。命令行开关与键位总表见 [根 README](../../README.md#命令行功能开关无键盘自检)。

---

## 下一步

- 窗口开好了、输入接上了，往画面里放东西 → [🟢 ② 几何与场景](3-geometry-scene.md)
- 想给每窗装配自己的渲染 pass / 后期 / HUD → [🔴 进阶](8-advanced.md)
- 建窗失败 / 上下文为空（macOS forward-compat）→ [🧰 排错](9-troubleshooting.md)
