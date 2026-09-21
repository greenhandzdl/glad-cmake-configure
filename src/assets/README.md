# assets/

引擎的运行期数据与内容，与代码分开存放。这里有两类内容，各有各的约定：

| 目录 | 内容 |
| ---- | ---- |
| [`shaders/`](shaders/README.md) | 内嵌 GLSL 的只读**参考镜像**（不被加载）。 |
| [`models/`](models/README.md)   | 模型投放目录（FBX / OBJ / glTF），运行期经 `AssetManager` 异步加载。 |

## 源码隔离约定

整棵树的划分刻意让*引擎代码*、*应用代码*、*第三方实现*与*内容*互不混淆：

```
src/
  gldx/**            引擎子系统（core、geometry、texture、material、shader、
                    camera、light、shadow、scene、render、text、assets、debug）
                    —— 不含应用逻辑；整体以 C++20 named module `gldx`（gldx.cppm）交付
  gldx/core/Platform.h  demo 文本 include 的头（GLFW + 窗口常量）；
                       刻意不属于 module
  main.cpp          PBR 演示入口 —— 靠 `import gldx;` 接线
  voxel_main.cpp    体素演示入口（同理，世界层写在 demo 侧）
  gldx/third_party/stb_image_impl.cpp  唯一的第三方实现 TU
                       （STB image / truetype）；不泄漏到 module 接口
  gldx/shader/*Shaders.h   GLSL 以嵌入的 raw string 住在这里 —— 着色器代码的唯一真源
  assets/          内容资源，放在 src/ 内、与引擎代码同级（不另设顶层目录）：
    shaders/       上述 GLSL 的可浏览副本（永不编译、永不加载）
    models/        经 gldx::AssetManager 加载的内容
```

关键规则：

1. **GLSL 是嵌入的，不是加载的。** 引擎从 `gldx::shaders::k*` 字符串常量编译着色器；
   `src/assets/shaders/` 只是文档。这让运行不依赖工作目录/路径假设——三平台 CI 尤其需要。
2. **从磁盘读的内容只有两类：模型/贴图与 HUD 字体。**模型/贴图永远经 `gldx::AssetManager`
   异步加载（后台解码 + 渲染线程上传）；字体是 demo 侧同步的 `Font::LoadFromFile`
   （找系统 TTF，找不到则禁用文字叠加，不阻断运行）。大型二进制默认不入库——见
   [`models/README.md`](models/README.md) 的说明。
3. **`src/main.cpp` 是应用，不是引擎的一部分。** 它可以伸进 `gldx/**`，
   但 `gldx/**` 任何东西不许反向伸进 `main.cpp`。
