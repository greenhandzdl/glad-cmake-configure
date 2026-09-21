# 模型投放目录

把网格资产放在这里——**FBX / OBJ / glTF / GLB**（[Assimp][assimp] v5.4.x
能导入的任意格式均可）。本目录在仓库中通过 `.gitkeep` 刻意保持为空；真实文件按你的常规工作流
加入（体积大，建议 Git LFS，或参考文末的仓库卫生约定）。

[assimp]: https://assimp.org

## 模型如何到达 GPU

模型经 `gldx::AssetManager::RequestModel(key, path)` 加载（见
[`src/gldx/assets`](../../../src/gldx/assets)），走**两阶段管线**，保证任何 GL 调用都不发生在渲染线程之外：

1. **Stage A（后台线程，不碰 GL）：** Assimp 解析文件，importer 在 CPU 侧构建
   `MeshData`（位置、法线、UV、切线）与材质描述，结果以不可变共享数据返回。
2. **Stage B（渲染线程）：** 在 `Mesh::Upload(...)` 里把已上传的 `MeshData` 变成
   GL 缓冲 / 顶点数组。

`AssetManager` 从任意线程调用都安全：内部有 worker 队列（`std::mutex` +
`std::condition_variable`）和 `std::shared_mutex` 保护的结果缓存；调用方拿到的是
句柄 / `shared_ptr<const …>`，绝不是可变的 GL 对象。完整不变量清单见
[`doc/developer/thread-safety.md`](../../../doc/developer/thread-safety.md)。

## 纹理 / 材质（两条硬性拒收规则）

模型内嵌与独立贴图都按路径引用，用 STB 解码（实现单元在
`src/gldx/third_party/stb_image_impl.cpp`）。加载器对资产文件本身有两道已验证的防线：

- **纹理引用不许走出模型所在目录。** 材质（如 `.mtl` 的 `map_Kd`）里的引用只接受
  模型目录内的相对路径（子目录可以）；绝对路径、盘符或含 `..` 的引用会被
  `ModelLoader` 拒绝并留一行 stderr 诊断（`names a path outside the model's own
  directory`）——此时模型照常加载，只是那块贴图不生效。**所以请把贴图与模型放在同一
  目录（例如 `models/<name>/` 子文件夹），材质里用相对引用。**
- **图片尺寸在解码前就用头部校验。** 边长超过 `kMaxTextureSide`（16384）的图片在
  分配像素之前就被拒（防"小文件解出巨量内存"的解压炸弹），错误以 `std::expected`
  错误串返回，带真实尺寸与上限。

另外：面索引越出本 mesh 顶点数的三角形会被**整面丢弃**（保住"索引只指向自己顶点"的
不变式，不打诊断）；位置含 NaN/inf 时会有 `[ModelLoader] ... positions are not finite`
的 stderr 行——NaN 顶点合法上传但永不光栅化，症状是"模型不见了"，看到该行请修模型本身。
排错速查见 [`doc/user/9-troubleshooting.md`](../../../doc/user/9-troubleshooting.md) §6；加载用法与两阶段管线的教程见 [`doc/user/4-assets-loading.md`](../../../doc/user/4-assets-loading.md)。

## 仓库卫生

大型二进制默认不入库。若要加入重量级资产，请扩展 `.gitignore`（或启用 Git LFS），
不要直接提交——引擎与 `main.cpp` 必须保持三平台 CI "克隆即可构建"。
