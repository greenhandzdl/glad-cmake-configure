# 更新日志 / Changelog

本文件记录 `GLFW_Template`（`gfx` 引擎 + 演示应用）各版本的变更。
版本标签遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

## v1.3.1

一轮“把每个开关都亲手关掉再打开、拿截图对比”的验证带出来的东西。验证方法本身也进了仓库（`src/demo_cli.h`）。

### 修复

- **粒子在 macOS 上实际上看不见**：`ParticleBatch::Draw` 未开 `GL_PROGRAM_POINT_SIZE`，而该 cap 在 macOS 默认就是关的，
  驱动因此忽略着色器里的 `gl_PointSize`，每个粒子塌成一个物理像素（Retina 下几乎不可见）。
  现在与 depth/blend 一起成对保存、开启、恢复，并在 `ParticleBatch.h` 的契约注释里写明。
  截图取证：同一固定画面下 `--off particles` 从 0.005%（与噪声同量级）变为 3.59% 画面变化。
- **`scripts/build.sh` 在 macOS 上根本配不起来**：生成的脚本用默认的 Unix Makefiles（不支持 C++20 modules）
  且不挑编译器（AppleClang 没有 `clang-scan-deps`）。现在自动选 Ninja、在 macOS 上自动导出 Homebrew LLVM 的
  `CC/CXX`（已显式给 `$CXX` 则不覆盖）、生成器/编译器与旧缓存不符时加 `--fresh` 重配，结尾提示两个可执行文件。
  这三件事 CI 与 README 早就做对了，只有生成的脚本漏了。
- **文档与 HUD 的键位文案对不上实际**：体素 README 写着按住 `Q`+`E` 减速，而代码里根本没有这两个键
  （实为按住 `Shift` 减速、`Ctrl` 下降），也没提 `Tab` 切投影；PBR 的 HUD 提示串少了天空盒的 `6`，
  README 则漏写 `6` 与 `Tab`。逐条对着 `glfwGetKey` 的调用点核过再改。
- **天空盒没有键盘开关**：HUD 会显示 `Sky ON/OFF`，但只有命令行能改。新增 `6` 键。
- **HUD 的 fps 读数与它所在的那一帧无关**：`EMA(1/dt)` 被最后卡顿的一帧主导，两个 demo 均改为固定窗口（≥ 0.5 s）计数。
- **demo 的时间驱动内容不可复现**（导致截图对比本身不可信）：carousel 用 `glfwGetTime()` 绝对值做相位（随开机时间漂移，
  且 float 化后精度损失已可比见）；体素碎屑用变步长积分（同参数两跑差 2% 画面）；脚本挖掘节拍相对上一次触发排序
  （首帧跨多步则整体平移，最后一炮落在 `--freeze-at` 内外全看帧率）。现在动画时钟是“距启动秒数”、
  碎屑走固定 120 Hz 步长、节拍锁在绝对步数边界上：**同参数两跑逐像素全等**（实测噪声底 0.000%）。

### 加固

- `Texture2D::Upload` / `Texture2DArray::Upload`：`glTexImage*` 会按 `width*height*channels` 从调用方 buffer 里读，
  所以先证明 `pixels` 至少那么长。通道数限定在 1..4（`DataFormat` 把其他值映射成 RGBA，上传格式与长度计算会不一致），
  边长/层数上限 16384 / 4096（使乘法不回绕），长度校验改用除法而不是可能回绕的乘积。
- `GeometryPass` / `PostProcessPass` / `DebugHudPass` / `ShadowPass`：只使用应用真正交出的子系统，
  入口一次判全必需指针（与 `VoxelOpaquePass` 已有契约对齐），免得一个可选特性为 null 时解空指针。
- `BlockRegistry::Append`：达到 `kMaxTypes` 后返回 `kAir` 而不是把 id 回绕到内置方块上（否则一个自定义方块会默默变成草）。

### 安全与健壮性（ASan + UBSan 对抗输入自检）

把 `Chunk` / `ChunkMesher` / `BlockRegistry` / `Noise` / `RaycastVoxel` / `Frustum` 这些 CPU 侧原语，加上图片解码，喂给一份用 `-fsanitize=address,undefined` 与 `-fno-sanitize-recover=undefined` 编译的 libgfx，输入包括 NaN、±inf、1e30、`INT_MIN` 坐标、未注册 id、越界访问、反向包围盒、畸形图片文件。UBSan 在“没崩”的情况下揪出三处真未定义行为，均已修：

- **`RaycastVoxel` 的 float→int 转换**：起点/方向只要有一个分量是 NaN、inf 或 1e30，`(int)std::floor(oA)`
  就越出 int32 表示范围（UB），紧随其后的 `lo - cell` 还可能有符号溢出。现在入口拒绝非有限的射线
  （契约已写进注释：这种射线答“未命中”而不是读出一个随机 cell），首 cell 改在 double 域算出、只在确认
  落在 `[lo, hi]` 内后才转 int，`tMax` 用等价的闭合形式一次算出（对合法输入逐位等价，且少一次舍入）。
- **`Noise::Perlin2/Perlin3` 的 `floorMask`**：同一个 `(int)std::floor(v)` 问题，`v` 来自 fbm 的倍频阶梯，
  坐标一大就 UB。Perlin 只用得到 `(int)floor(v) & 255` 和小数部分，两者对“减去 256 的整数倍”都不变量，
  所以先 `std::fmod(v, 256.0)` 折叠再取整：合法坐标逐位等价，极端坐标从 UB 变成确定的有限输出。
- **`ChunkMesher` 的跳过条件**：原来只测 `id == 0`，于是未注册的 id（旧存档的方块表、被卸载的 mod）
  虽然被 `BlockRegistry::Get` 当作 air 解析，却仍然生成贴图切片 0 的幽灵面。新增
  `BlockRegistry::IsAir(id)`（id 为 0 或超出表长），生成面与邻居遮挡判定都改用它。

- **demo 自己的命令行也在这条链上**：`strtod` 乐意把 `inf`、`nan`、`1e300` 当成合法数字，而
  `voxel_main.cpp` 里有四处 `static_cast<int>(flags.number(...))`（`--select` / `--rise` /
  `--auto-break` / `--auto-place`）——正是上面那两个探针抓到的同一类 UB（独立探针实测
  `(int)strtod("inf")` 在 UBSan 下 rc=134）。新增 `Flags::integer()`（越界饱和、NaN 落到下界）
  与 `Flags::real()`（非有限值回退默认，边界在 double 域内夹好之后再转 float），四个 int 调用点
  与 `--yaw` / `--pitch` / `--radius` 全部改用它们。`--auto-break` / `--auto-place` 另有 100000
  上限：下一行就要把两者相加，只饱和到 `INT_MAX` 会让那个加法回绕成负数。
  一点如实的区分：double→float 的越界转换在这套工具链上 UBSan 并不报（实测 `static_cast<float>(1e300)`
  rc=0，加 `-fstrict-float-cast-overflow` 也不报），所以 `real()` 是防御性收敛，不是被抓到的 UB。

- **`ModelLoader::Load` 的两处收尾**：把 8 份畸形模型喂给带 ASan + UBSan 的 libgfx（只有 `v` 没有 `f`、空文件、
  随机字节、截断的 glTF JSON、指向不存在 `.bin` 的 glTF、面索引越界、位置为 NaN/inf、材质指向一张只有文件头的
  PNG），判据不是"没崩"，而是"被接受的那些模型里，每个索引都指向该 mesh 自己发出的顶点"。两处改动：① 面索引现在由
  我们自己判界并整面丢弃（丢单个索引会打乱下面按三个一组算出的 `GeometryRange`）——OBJ importer 恰好会拒绝这种文件，
  但把这条不变式托付给"哪个 importer 碰巧跑了"不负责任；② 位置非有限时打一行 stderr，因为 NaN 顶点是合法上传、
  永不光栅化，用户的感受是"模型不见了"而不是"模型坏了"。两个 demo 都不经 `AssetManager::RequestModel`，画面无关。

### 新增

- **`src/demo_cli.h`**：两个 demo 共用的 header-only 命令行开关（不进 `module gfx`）。
  `--off a,b` / `--on a,b` / `--quit-after SEC` / `--help`，以及数值选项
  `--freeze-at` / `--yaw` / `--pitch` / `--radius` / `--rise` / `--select` / `--auto-break` / `--auto-place`。
  不传任何选项时与旧行为逐位等价（`flags.number(name, fallback)` 直接回退默认值）。
- **`voxel_demo` 退出统计行**：帧数、平均/最低 fps、生成与网格化的 chunk 数、破坏方块数、存活/累计粒子数——
  流式收敛与粒子压力这类“截图看不出来”的性质自此可自证。

### 验证结果

- **对抗输入**：35 项检查在 ASan + UBSan（不可恢复）下全通过，进程退出码 0 —— 任一 UB 都会当场 abort。
  除上述 CPU 原语外还包括图片解码：10 个畸形文件（只有头的 PNG、空文件、随机字节、只有一半的 JPEG、
  声称 6000×6000 却只装 8×8 数据的 PNG）× 6 个 `forceChannels`（含非法的 `7` 与 `-3`）组合，
  20 次被接受的解码每一次都满足 `pixels.size() == width*height*channels`——正是 `Upload` 先验再交给
  `glTexImage2D` 的那个不变式。探针本身是仓外的临时工具（`/tmp/adv/advcheck.cpp`）：它是 `import gfx;` 的
  模块消费者，因为定义在 module 实现单元里的实体带着模块附着（`__ZN3gfxW3gfx5NoiseC1Ej`），
  文本 include 同一份头文件链不上。
- **畸形模型文件与退化视口**：9 份模型文件（8 份畸形 + 1 份正常立方体，正常那份是"失败不等于加载器坏了"的对照）
  与下面的视口检查合起来 29 项，在 ASan + UBSan 下全通过，退出码 0（含 0×0 / 1×1 / 超过
  `GL_MAX_TEXTURE_SIZE` 的视口、`SpriteBatch` 与 `PickRay` 的退化尺寸）。退化尺寸下驱动只报
  `GL_INVALID_VALUE` 而链路自愈：`PostProcessChain::Resize` 拒绝 0/负数尺寸保住上一个好尺寸，不可能的尺寸让 FBO
  不完整（有诊断行），下一次合法 `Resize` 即恢复；`PickRay` 在 0 高度下给出非有限射线，而加固后的
  `RaycastVoxel` 与 `RaySphere` 都拒绝它。

- **对抗 argv**：两个 demo 用 UBSan 构建跑畸形命令行——`--select inf`、`--auto-break 1e300`、
  `--rise nan`、`--auto-place -inf`、`--pitch nan`、`--radius -1e300`、`--yaw 1e300`、`--off nosuch`、
  `--on bogus`、缺值的 `--auto-break`、`--freeze-at abc`、空值、`+` 与 `x`——退出码全 0，无一条 sanitizer
  报告；拼错的 feature 与非数字的值各自留下一行 stderr，而不是被静默吞掉。

- **性能**（60 s × 3 轮，`/usr/bin/time -l`）：PBR CPU 18.3 s / RSS 117 MB；体素空闲 20.7 s / 131.8 MB /
  平均 116 fps（最低 81.5）/ `chunks gen 243 == meshed 243`（流式收敛）；体素压力（`--auto-break 200`）
  25.3 s / RSS 131.5 MB（长跑不涨）/ 平均 117.3 fps / `spawned 748`、退出时 `live 0`（粒子池全部退休，无泄漏）。

12 个开关全部拿到“信号 ≫ 噪声底”的截图证据（每组噪声底 0.000%，即同参数两跑逐像素全等）：
PBR `shadow` 0.14% / `ibl` 15.4% / `bloom` 57.8% / `debug` 3.7% / `instances` 6.6% / `sky` 91.4% / `ortho` 18.7%；
体素 `particles` 3.59% / `fog` 63.5% / `sky` 36.5% / `ortho` 77.8% / `water` 4.5%（均为画面变化像素占比）。改完 CLI 的数字访问器后整轮复跑，12 项数值与改前逐项相同——默认命令行下的画面一字未动。

## v1.3.0

### 新增

- **体素子系统 `src/gfx/voxel/`**：`Chunk`（16³ uint16 方块存储 + 本地/越界查询 + dirty 与邻居 dirty 标记 + 包围球）、
  `BlockRegistry`（header-only，内置 air/grass/dirt/stone/sand/wood/leaves/water/snow，`solid/transparent/cutout/emissive` 属性）、
  `ChunkMesher`（纯 CPU、可在 worker 线程跑：相邻不透明面剔除、同种方块互剔皮肤、逐顶点 4 档 AO + 方向 tint、
  输出 opaque / transparent 两份网格）、`VoxelMeshGpu`（一条 chunk 的 GPU 记录，`Update` 原地换缓冲）。不做 greedy meshing。
- **`Texture2DArray`**：一方块一个纹理层，生成完整 mipmap 链，语义对齐 `Texture2D`；层间隔离彻底规避图集 bleed，
  顶点只需携带一个 layer float。
- **`Mesh::Update(MeshData&&)`**：glBufferData 整缓冲 orphan 重传、VAO 复用，chunk remesh 不再重建 GPU 资源。
- **`VoxelOpaquePass` / `VoxelTransparentPass`**：chunk 不进 `Scene` 节点图，pass 直接持有渲染记录列表，
  按相机距离降序（back-to-front）绘制透明流并关掉 depth-write，不透明流与 cutout（树叶）同批走 alpha cutoff；
  两者都做包围球视锥剔除。
- **`VoxelShaders`**（GLSL 410）：采样 `sampler2DArray`，顶点 light 通道调制 + `LightingBlock` 平行光近似 + 雾 + 水面 uv scroll。
- **`RaycastVoxel`（DDA）**：`src/gfx/camera/VoxelRay.h`，Amanatides & Woo 逐格步进，header-only 纯函数，
  对 solidity 谓词做模板参数（chunk / chunk 网格 / 扁平数组皆可），返回命中格 + 进入面法向 + 距离，放置方块不必二次查询。
- **`Camera` 飞行接口**：`MoveForward/MoveRight/MoveUp(dt)` 与 `SetYawPitch`，原有 orbit 行为不变。
- **`Noise`（`src/gfx/util/`）**：seeded 排列表的 Perlin 2D/3D + `Fbm2d(x, z, octaves)` + `ToUnit`；确定性、无全局状态、worker 线程安全。
- **`ParticleBatch`**：POINT sprite 批次，`Spawn/Update/Draw`，着色器内按 lifetime 缩尺寸与淡出（挖掘碎屑、放置反馈）。
- **线性距离雾**：`LightingBlock` 尾部追加 `fogColor` / `fogParams`，`LightSetup` 加 CPU 侧字段，PBR / Instanced / Voxel
  三条 fragment 程序统一 `mix`；默认关闭，未开启时既有场景视觉零变化（2D 精灵不雾）。
- **`voxel_demo` 可执行目标**：第二个 target（`src/voxel_main.cpp`），fBm 高度场地形 + 水位 + 树，
  worker 生成/网格化 + 渲染线程限量上传，飞行相机、DDA 破坏/放置、碎屑粒子、雾与 HUD；
  `scripts/run.sh [target]` 据此选择运行目标。世界层（chunk 网格、流式策略、地形生成、编辑规则）刻意留在 demo 侧。

### 边界（本轮不含，留作后续增量）

- 火把光 flood-fill 传播、greedy meshing、方块实体 / 生物 / 合成 / 存档 / 网络；`Chunk` 尺寸固定 16³。

## v1.2.0

### 新增

- **正交投影切换（Tab）**：`Camera` 增加 `SetOrthographic` / `ToggleProjection`，正交盒高度按当前
  fov + 轨道半径匹配透视取景；天空盒改用 `SkyboxViewProj`（恒为去平移的透视盒），在正交模式下仍正确。
- **`scripts/release.sh`**：一键把版本号同步到 `Platform.h`、`CMakeLists.txt`、`README.md`、`AGENTS.md`
  等全部锚点（可选 `--commit` / `--tag`），根治发布时源码版本与 tag 漂移；脚本刻意不 push、不建 Release。

### 修复

- **鼠标拾取选错物体**：`PickRay` 原以「远平面反投影点 − 眼位」构造射线且直接吃 GLFW 窗口坐标，
  在 Retina（内容缩放 2×）下整条射线被压向左上半、正交模式下更是方向全错。现改为反投影近/远两点
  构造与投影无关的射线，拾取坐标先按窗口尺寸归一化再乘 framebuffer 像素。
- **Bloom 对 PBR 材质无效果**：亮度过阈原为 1.0，而球体线性 HDR 仅 0.2–0.9 几乎全被键掉；且上一版
  为保小高光改的全分辨率让光晕只剩几像素不可见。现阈值降到 0.35、bloom 缓冲回半分辨率配 6 次宽模糊、
  强度 2.0，方向光高光 ×3 抬进 HDR，材质高光点终于长出柔光环。
- **GL 状态泄漏**：上一帧 HUD / sprite / debug 段关闭的 `GL_DEPTH_TEST` / `GL_CULL_FACE` 未复原，
  泄漏进本帧导致级联阴影图空、HUD 被剔除、正/背面剔除失效。`CascadedShadowMap` / `EnvironmentMap` /
  `SpriteBatch` / `DebugDraw` 各自显式设定所需状态，不再依赖上游段善后。
- **字体重影 / 糊成一团**：`Font` 里 `stbtt_packedchar` 的位图像素以整数除法算 UV 致截断为 0（全部采样
  图集原点），且用图集包围盒（过采样下是 bake 空间的 2×）当 quad 尺寸使字形拉伸重叠。改为浮点除法算 UV、
  用 `xoff2-xoff` 的真实 bake 空间跨度算 quad 尺寸。

## v1.1.0

### 新增

- **按读者角色重组的文档体系**：`doc/user/`（入门 / 基础 / 进阶 / 排错四篇，按难易递进）、
  `doc/developer/`（设计 + 线程安全，已全量中文化）、`doc/agents/`（面向 agent 的
  环境搭建与写程序运行手册），入口见 `doc/README.md`。
- **`AGENTS.md`**：仓库根的 AI agent 速查（关键事实 / 逐平台命令 / 文件地图 / 硬约束 / 报错→修复）。
- **`CMakePresets.json`**：macOS 上把编译器钉到 Homebrew LLVM，使命令行与 CLion 都能正确
  配置 C++20 named module（`condition: Darwin`，不影响 Linux/Windows 与 CI）。
- **`CHANGELOG.md`**：本文件。

### 修复 / 健壮性

- **`Font::LoadFromFile`**：校验 `tellg()` 得到的大小，`<=0` 或超过 64 MB 直接返回错误，
  杜绝畸形或超大字体文件驱动无界分配（潜在崩溃 / DoS）。
- **`ThreadPool::WorkerMain`**：为每个出队任务套 `try/catch(...)`，防止任务抛异常逃逸出
  工作线程顶层触发 `std::terminate`；异常任务被丢弃，关联 `std::future` 以 `broken_promise` 呈现。

### 重构

- **演示 `main.cpp`**：把 `1..5` 五段重复的按键切换边沿检测收敛为一个 `edgeToggle` lambda。

### 文档一致性

- `thread-safety.md` 补充 `ThreadPool` 顶层异常兜底说明；`src/assets/models/README.md` 等
  指向开发者文档的链接随目录重组更新。

## v1.0.0

首个正式版本：跨平台 OpenGL 4.1 Core / PBR 图形引擎 `gfx`（以 C++20 named module 交付的静态库）
与交互式演示应用。

- **渲染**：PBR 金属/粗糙工作流、级联阴影（CSM + PCF）、基于图像的照明（IBL：辐照度 / 预滤波 /
  BRDF LUT）、HDR + MSAA + Bloom + ACES 色调映射、天空盒。
- **架构**：分层子系统 + 单向依赖；线性渲染 pass 链（Shadow / Geometry / PostProcess / DebugHud）；
  场景层级 `Scene`/`SceneNode`/`Transform`；两阶段异步资源管线（后台解码、渲染线程上传）。
- **工具**：精灵批次 + 位图字体 HUD、GPU 实例化绘制、视锥剔除、调试线框、CPU 拾取、CPU+GPU 帧分析。
- **平台**：Windows / macOS / Linux；依赖 GLAD / GLFW / GLM / STB / Assimp；三平台 CI 发布。
- **线程安全**：GL 仅在单一渲染线程，辅以线程亲和断言、move-only RAII、`std::expected` 错误传播。
