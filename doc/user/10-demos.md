# 🟢 Demo 画廊：每个 demo 演示什么、怎么按键

> 从根 [README](../../README.md) 拆出：README 只留最小事实与快速开始，**逐 demo 的画面叙述、键位与索引表在这里**。想学“怎么做对”而不是“长什么样”的，去对应章节——透明与投影见 [进阶 §1.1–§1.3](8-advanced.md)，细分与绕序见 [② 几何与场景](3-geometry-scene.md)，命令行开关见 [①·补 §8.1](2a-window-input-cli.md)。

每个 demo 都只装配它演示的那一个子系统，其余留缺省（这本身就是“按需装配”的验收）：`./scripts/run.sh <demo>` 运行，`--quit-after N` 无头自检，`--help` 列出该可执行真正认识的开关与选项。

## 1. 最小实现基线：`GLFW_Template`

`src/main.cpp` 打开一个 800×600 窗口，只画一个三角形：一个自定义 `RenderPass` 在 `Execute` 里 `glClear` + 内联 GLSL 编译的着色器 + `Mesh::Draw()`，几何只以 CPU 侧 `MeshData`（3 个 `Vertex`，position+color）表达，VAO/VBO 与固定属性布局全部由 `gldx::Mesh` 代劳；逐帧只填 `fbWidth/fbHeight` 的空 `RenderFrame`。窗口与帧循环由 `gldxwin` 包办（`gldx::win::App::Get()` 拉起 glfw/GL 4.1 上下文，一个 `Window` 的 `OnFrame` 里驱动渲染），它不建 PBR 管线、不申请 LightBuffer/Scene/后处理——用来证明“只 `import` gldx 三件套、不申请任何可选子系统也能出图”，是理解引擎最小使用面的基线。想看 `Mesh` 背后手搭 `VertexArray`/`GLBuffer`/`AttachAttribute` 的底层版本，对照 demo 在 [`../../src/demo/hello_triangle/`](../../src/demo/hello_triangle/main.cpp)（`./scripts/run.sh hello_triangle`），两份源码 diff 即是 `Mesh::Upload` 帮你做掉的事。

## 2. 成品演示

### `pbr_showcase`：全链路交互场景

`./scripts/run.sh pbr_showcase` 是完整场景：带纹理的地面与球阵、金属立方体、一个旋转的子层级（carousel，演示场景层级变换传播），配合级联阴影、IBL 环境光照、HDR + Bloom + ACES 后期、天空盒，以及精灵批次文本 HUD。绘制不再是内联 `gl*` 调用，而是逐帧组装一个 `RenderFrame` 后一句 `renderer.Render(frame)`；`BuildPbrPipeline()` 依序跑 Shadow → Geometry → Skybox → PostProcess → DebugHud，这些都是**按需装配**的可选 pass。

**操作**：拖拽轨道旋转 / 滚轮缩放；`A`·`D` 太阳方位、`W`·`S` 太阳高度；右键拾取高亮；`1` 阴影、`2` IBL、`3` Bloom、`4` 线框、`5` 实例化、`6` 天空盒、`Tab` 透视/正交；`Esc` 退出。

### `voxel_terrain`：可玩体素世界

`./scripts/run.sh voxel_terrain` 是体素原语的验收场：fBm 高度场地形 + 沙滩 + 湖泊 + 树冠，worker 线程生成与网格化、渲染线程限量上传，雾随距离收掉视距边缘。世界层（chunk 网格、流式策略、地形生成在 `World.{h,cpp}`，输入回调在 `Input.{h,cpp}`，HUD pass 在 `Hud.{h,cpp}`）全部写在 `src/demo/voxel_terrain/` 内（`main.cpp` 只留窗口/帧循环装配），引擎只提供原语、不含任何世界概念。

**操作**：鼠标转向（指针捕获）；`W`/`A`/`S`/`D` 飞行、`Space`/`Ctrl` 上下（`Shift` 减速）；左键破坏、右键放置；`1`–`8` 选方块；`F` 切换飞行/轨道相机；`[`/`]` 太阳方位、`-`/`=` 太阳高度；`P` 粒子；`Tab` 透视/正交；`X` 退出。

### `textured_sphere`：两条细分通路共用一份几何

`./scripts/run.sh textured_sphere` 把「细分」这件事变成看得见、量得出、而且**能比较**的东西。它有两套细分通路跑在**同一份 `MeshData`、同一个 VBO/EBO** 上：CPU 侧是 `GeometryFactory::Sphere(radius, segments, rings)` 的两个整数（经线条数与纬线圈数），键盘按下去就换一个 `MeshData`，`Mesh::Update` 原地换缓冲而保留同一个 VAO，所以改档便宜到可以逐次按键实时生效，HUD 同时打印换算出的 `verts`/`idx`，让「更平滑」的代价是一个数而不是一种感觉（48×24→1225/6912，10×5→66/300）；硬件侧是细分控制/求值阶段（TCS/TES）——同一份索引流以 `Mesh::DrawAs(GL_PATCHES)` 交出，每个三角形就是一个 3 控制点的 patch，**不需要第二份几何**。`G` 在两条通路之间切换是做对照的唯一诚实方式：把笼压到 12×6（91 顶点）关掉细分，轮廓是**多边形**；`G` 打开、level 设 3，还是那 91 个顶点，轮廓变成**真正的圆**（HUD 报 `~1296 tris from 144 patches`）——粗笼变圆是 TES 把重心插值得到的弦上点 `normalize(p) * radius` **重投影回球面**的结果，不是 CPU 又生成了顶点。`A` 进一步按边的**屏幕像素长度**逐边定 level（朝镜一侧密、轮廓侧疏；共享边的 level 必须由两端点对称地算出，否则相邻 patch 之间会裂开，这也是这里选 `equal_spacing` 的原因）。

三张贴图全部在代码里生成、不依赖磁盘文件：UV 网格图（u 由红渐变到绿、v 加蓝、每 10% 一条十分度线，是「球面参数化」最诚实的可视化——经线必然对齐 segment 列，而每一根极点行无论网格多密都塌成同两个三角形）、带原点象限标记的彩色 checker、以及用 `gldx::Noise` 沿圆周采样（因此经度方向无缝）的噪声 planet。着色器是手写的 GLSL 410（location 0/1/2 = position/normal/uv、单个 half-lambert 项保证贴图读得清、一个绑到 unit 0 的 `sampler2D`），刻意不走 `GeometryPass`/PBR；也正因为它绕开了引擎管线，它必须自己处理两件 API **从不汇报**的事：一是 **sRGB 往返**——贴图按 sRGB 上传 ⇒ 采样得到线性辐射，光照在线性域做，写回默认帧缓冲（glfw 不请求 `GLFW_SRGB_CAPABLE` 就不是 sRGB 的）前还要 `pow(c, vec3(1.0/2.2))` 编码一次；二是**绕序**——`Renderer::Init()` 设 `glFrontFace(GL_CCW) + glCullFace(GL_BACK)`，所以一个从外侧看呈顺时针的网格，它整个可见表面都会被判成背面而剔掉，或者让你看见闭合形体远侧的内表面，而顶点法向看起来完全正常。**画面发闷发暗往往是这两个原因叠加的结果**（本 demo 就是先只修了 sRGB、发现球阵仍然不对，才用关掉剔除的方式定位到绕序）；`C` 键切掉背面剔除、让着色器按 `gl_FrontFacing` 压暗远侧，就是**找出反向网格的方法本身**——近侧亮远侧暗说明绕序对，反过来就是反了。

**操作**：拖拽轨道旋转 / 滚轮缩放；`[`·`]`（或 `←`·`→`）改 segments，`-`·`+`（或 `↑`·`↓`）改 rings；`G` 切 GPU 细分、`1`–`9` 设细分 level、`A` 切自适应、`C` 切背面剔除；`T` 循环贴图、`W` 切 wireframe（细分笼 + 线框正是看清生成格阵与其接缝的组合）、`H` 切 HUD、`Esc` 退出。初值亦可由 `--segments/--rings/--texture uv|checker|planet/--tess-level/--sphere-radius/--yaw/--pitch/--orbit` 与 `--on tess --on adaptive --off cull --on wireframe` 给定，`--snapshot PATH [--snap-at SEC]` 可无头出图做细分扫描的像素对照；若驱动链不出可用的 patch 程序，demo 会打一条 `stderr` 并把 `tess` 关掉降级到 CPU 网格，而不是开一个空窗。

### `glass_water`：四道闸门 + 推导出来的透明度 + 投影切换

`./scripts/run.sh glass_water` 把「深度测试会不会吃掉透明物体后面的像素」拆成**四道各自独立的闸门**，每一道都能单独关掉，从而看出它本来在守什么。场景是一个 36×24 的水池：不透明侧有甲板条石、四根立柱、三个破水而出的桥墩和两颗水底礁石，cutout 侧是远岸栅栏与两面格栅屏（`discard` 掉孔洞——**有洞不是「透明」，是「不存在」**，所以它们留在不透明侧、照常写深度、不参与排序），透明侧则是一张切成 6×6 的水面、五块只有 `baseAlpha` 不同的玻璃板（0.10/0.20/0.30/0.50/0.80，同一物件的透明度梯子，其后必然还看得见池底与立柱）、一只沉在水下的彩色箱子、一个 `baseAlpha` 只有 0.04 的肥皂泡、一块近乎不透明的磨砂板，最后是三盏 additive 的灯。**但透明不再是调出来的**：`Surface` 只剩 `ior`/`roughness`/`absorption`/`pathLen` 四个物理量（旧版的 `fresnelPower`/`reflect`/`specular`/`shininess` 四个旋钮已删——它们可以互相矛盾：同一块玻璃可以“高光很尖”同时“菲涅耳很平”），一份 GLSL 服务五层的做法没变，变的是 fragment 现在是一个电介质 Cook-Torrance（GGX NDF + Smith 相关可见度 + 矢量 Schlick），F0 由 IOR 推导（水 1.333→2.0%、玻璃 1.52→4.0%、肥皂膜 1.4→2.9%），环境是与穹顶贴图共用同一组常量的解析天空。渲染侧不用引擎的 `GeometryPass`，`BlendPass` 自己排一个五层状态机：**Sky**（深度测试与写入全关 + `glCullFace(GL_FRONT)`，否则一个 90 单位的穹壳自己就把深度缓冲写满）→ **Opaque**（写深度、关混合）→ **Cutout** → **Blend**（开混合 + `glDepthMask(GL_FALSE)` + 远→近）→ **Glow**（additive，顺序无关，画在最后）。四道闸门分别对应 `1` 排序、`2` 深度写、`3` 闭合壳双趟、`4` 混合方程，键和 `--off sort` / `--on depthwrite` / `--off shell` / `--blend X` 一一对应（每道守什么、单独关掉的症状表见 [进阶 §1.1](8-advanced.md)）。

这里多一层与“四道闸门”无关、但同样会错的东西：**alpha 是从守恒推出来的，不是有人选的值**。入射光里 F 被反射、`(1-F)·absorb` 被吸收、只剩 `(1-F)·T` 能穿透，所以 `coverage = mirror·fresnelToAlpha + (1-mirror)·absorb`，`fresnelToAlpha=1` 时严格等于 `1-(1-F)T`，两项之和恒为 1——这就是为什么五块板 + 一个湖 + 一个泡泡叠起来仍然读作玻璃而不是一堆灰色：模型里没有东西可以在合成时凭空发明辐射。水的 `baseAlpha` 因此从 0.34 降到 0.05（那 0.34 里绝大部分本来在冒充反射与吸收），而 `fresnelToAlpha<1` 获得了一个明确语义：**这块表面在谎报自己的反射率**。`absorb` 来自 Beer-Lambert 斜程（`pathLen/max(NoV,0.12)`，系数按真实水的比例：红先死、蓝最后），所以“正看读得出池底格子 / 斜看是一堵绿墙 / 远岸总比近处暗”三个现象是一行代码的结果，没有贴图也没有 mask。两个踩过的坑写进了注释：（1）**premultiplied 与 coverage 是配对关系**——`(ONE, 1-SRC_ALPHA)` 不缩放源色，所以 shader 必须自己 `rgb *= alpha`，否则每层把全额辐射叠到刚被它放行的 `(1-a)` 背景上，五块“10% 玻璃”就刷白一帧（`uPremultiply` 由 `BlendPass` 按层给，只有那一层为 1）；标量 alpha 下 straight 与 premultiplied 逐字节相同，因为那是**同一处算术写在两个地方**。（2）**PBR 水那片“白沫”是亚像素高光火点**——GGX 在 roughness 0.06 时峰值 ~2.4e4，而波面法线只在顶点上采样，MSAA 会把火点摊成一池白雾；修法是 Karis 的解析高光抗锯齿（`fwidth(n)` 展宽 lobe + 按太阳圆盘立体角裁剪峰值），而不是把 roughness 调大蒙混。推导过程与代码形态见 [进阶 §1.2](8-advanced.md)。

相机还能在**透视与正交之间切换**（`Tab` / `--on ortho`），换的是顶点的 `uViewProj`（`glm::perspective` ↔ `glm::ortho`）而不是一个标签，顶点阶段没有任何分支。正交唯一真正需要 shader 知道的是**视线方向**：正交 frustum 里每条光线平行，“眼位到像素的向量”不是粗糙近似而是**未定义**，硬套透视式会让 NoV→菲涅耳→守恒 alpha 随屏幕位置漂移（边缘最亮），纯属投影伪影；所以 `viewDir` 改取 `-normalize(uCamForward)`，掠射镜带在正交下变均匀——那不是 bug，那就是“去掉透视”的含义本身。`Tab` 翻转那一帧会按 `orthoHeight = 2·d·tan(fov/2)` 把取景交给正交盒（反向时 fov 保持原值），因为一个会顺手缩放场景的切换看起来就是 bug；滚轮驱动当前生效的那个量（fov 或盒高）而不是飞行速度。细节见 [进阶 §1.3](8-advanced.md) 与 [⑤ 相机与拾取 §2](6-camera-picking.md)。

这里最容易被误解的一点是：**受害者不可能是任何不透明物体**。不透明层先画且写深度，透明物在它之后通过深度测试，所以「玻璃把石柱吃掉」在正确的层序下压根不会发生；真正会被吃掉的是 **blend 层内部的兄弟图元**——盖住水下箱子的那片水面 tile 只要在箱子之前提交并且写了深度，整只箱子就没了（注意是两个开关各一半：正确排序下箱子先于盖它的 tile 提交，先画的东西不会被后来的深度值拒掉，所以 `--on depthwrite` 单独打开时本取证帧里先消失的是最后画、而深度测试一直开着的那两盏 additive 灯）。因此取证视角必须钉死，让「一个透明物体在正确帧里清清楚楚、在错误帧里整块消失」真的落在画面里：箱子被放在玻璃板排左侧的开阔水面下（放到五块玻璃后面就会被逐层衰减到看不见，而看不见的伪证什么都证明不了），旁边几米外那只不透明桥墩是对照组。排序粒度同样是要害：水面不是「一个物体」，而是 36 个索引连续的 `GeometryRange` tile，**和物体共用同一张排序表**；关掉排序后 `Mesh::Draw()` 按声明序遍历 ranges，得到的正是同一批三角形的错误顺序，不需要第二份几何。混合则必须发生在线性 HDR target 里（8 位整数会把每个中间值舍入、并把 additive 的灯截顶），编码交给 `PostProcessPass` 的 exposure + ACES，`glBlendFuncSeparate` 的 alpha 通道用 `GL_ZERO, GL_ONE` 钉住目标 alpha 不被污染。HUD 打印的是**回读出来的 GL 状态**（`depthTest yes depthWrite no blend yes cull yes`、`src/dst ONE / 1-SRC_ALPHA`）、**从物品表回读的水面材质**（`water ior 1.333 -> F0 2.04% rough 0.06 path 3.2 m`：改了材质而这一行没变，说明改动根本没上 GPU）与真实的提交序列（`drawn far->near (44 submissions): 41.9 m water sheet tile 0 … 39.0 m frosted slab`），而不是它「以为」自己设了什么——测错对象是这类面板最常见的失效方式。

**操作**：`W`/`A`/`S`/`D` 飞行、`Space`/`Ctrl` 上下、鼠标转向（指针捕获）、`Shift` 减速（错误最好贴着一米读）；`1` 排序、`2` 深度写、`3` 壳双趟、`4` 循环混合方程；`Tab` 透视/正交、滚轮缩放当前生效的那个量（fov 或正交盒高）；`[`/`]` 海况、`-`/`=` 速度；`C` 背面剔除、`V` 线框、`B` Bloom、`H` HUD、`F` 释放/捕获指针、`X` 退出。

取证口径与仓库约定一致但多**三条**前提：`--freeze-at SEC` 必须**不晚于** `--snap-at`（波时钟停在 freeze 时刻，截图早于它就会拍到还在动的海面），比较时必须 `--off hud`（HUD 里有实时 fps 数字），而且**指针未捕获时整个输入层都不驱动仿真**（`--snapshot` 运行不捕获指针，而窗口仍是 frontmost——一杂散按键或一下滚轮就能把相机开走，“只在没人碰键盘时可复现”不叫复现性）。三条都满足时同参数两次运行逐像素全等，于是各配置相对正确帧的像素差是测量而不是巧合（取证帧 `--pos-x 0 --pos-y 3.4 --pos-z 12 --yaw 0.30 --pitch -0.06`，判据为“灰度差 > 0.6% 满量程”的像素数，1600×1200）：`--off sort` 2.99%、`--on depthwrite` 1.29%、两者同时（「被吃掉」帧）1.54%、`--off shell` 17.12%、`--off cull` 20.28%、`--blend additive` 32.17%、`--on ortho` 96.91%，而 `--blend straight` 与默认帧**逐字节相同**（标量 alpha 下两者只是把同一个乘法写在两个地方，按 `4` 再 `cmp` 就是这条论述的检查）；末帧 stderr 固定报 `47 blend submissions, 79 total draws`（44 = 36 tile + 8 个 blend 物体，+3 盏灯；79 = 1 天空 + 14 不透明 + 9 cutout + 44 + 8 次壳第二趟 + 3 灯）。

### `night_forest`：1400 棵树的实例化夜景、星空 cube、和一只会照出森林的木箱

`./scripts/run.sh night_forest` 的主角是**实例化**：一片夜里的小空地，中央一只 1.25 m 的木箱，四周 9–95 m 的针叶/阔叶混交林（默认 1400 棵；`seed 20261104` 实测 817 冷杉 + 583 阔叶、4850 个候选点因 3.1 m 最小间距被拒），四盏灯笼，头顶是程序生成的 HDR 星空 cube。整个世界的绘制是 **5 次 draw / 1406 个实例**（地面 1 + 冷杉 817 + 阔叶 583 + 灯笼 4 + 木箱 1），末帧固定打印这一行；`V` 线框直接看见实例化在付什么费，`C` 关剔除则能看见“faceted 冠层少一半”是绕序而不是实例化的问题。

树不是几百个 `SceneNode`，而是两份 `InstancedMesh`：实例矩阵带 offset（面积均匀的环形散布——半径**平方**上均匀，否则内缘挤成树篱、外缘秃成草坪）加一个 0.65–2.30 的**统一**缩放。统一是硬约定而不是省事：顶点阶段用 `normalize(mat3(aInstance) * aNormal)` 当法向矩阵，非均匀压扁必须烘进网格（阔叶那个“面包”就是 CPU 侧压的），否则法向扭离曲面、Gooch 的明暗边就长在错的地方。**一棵树同时是树皮棕和针叶绿**靠的是逐顶点色（为此给 `InstancedMesh` 的几何 VBO 补上 location 9，与 instance 色**相乘**而不是替换，`GeometryFactory` 留白 ⇒ 老调用方逐字节不变；对照实验：`pbr_showcase` 有/无这处改动，除 HUD 那行实时 fps 文字外整幅 1600×1200 全等）。同一个 alpha 通道顺手当材质掩码：灯笼只有玻璃几片为 1（自发光），木箱整面为 1（允许花环境光），树皮树冠全为 0——“哪个像素有资格反射”是逐顶点的**许可**，“反射多强”是逐次 draw 的 **uniform**，两者分开才不至于为了关掉树干的反射而重做一份网格。

天空盒是 cube，内容是星空，在 CPU 上涂成 6×`size`² 的 half-float（默认 512）；`--stars PATH` 换成自备的 2:1 全景图走引擎那一张面基表（`gldx::CubeFaceAxis`）重投影，于是两条路把月亮放在同一个位置。星点是 **splat 而不是 gather**，这是本 demo 最贵的一课：gather 的搜索半径只有一个格点，星点的高斯必须窄于半个 texel 才不被截断，而 512 面的一 texel 在这块 retina 窗上是约 5 px——“窄于半 texel”无论怎么写都是硬边方块，截断还在格点边界铺出整片网格纹。splat 反过来：每颗星按**真实夹角**写回周边 texel，宽度不再受搜索半径约束，六个面的接缝由“每个面各自问自己的 texel 看到什么、半份相加”自然成立。代价算过而不是猜的：朴素写法（每 texel 从表里重建三个向量 + `normalize` + `acos`）实测 5.2 s，用面基向量正交性折成闭式标量（`|w|² = 1+s²+t²`、`w·sd = a(1+s·bs+t·bt)`、用 `chord² = 2(1-cos)` 顶替角度平方省掉 `acos`）后 0.12 s；整片星空 1.7 s、进程启动 2.4 s。银河的两层 fBm 只在带内算（阈值 0.02，八度 3/2 而不是 4/4），因为带外的值已经低于 half 能存的那点尾数。

光照是 Gooch 而不是 PBR：一轮 0.03 量级的月亮加四盏灯笼，物理响应的正确结果是剪影。Gooch 的明暗交界是一条**边**而不是余弦坡，暗部是一个**颜色**而不是零，所以这里受光侧偏暖、不受光侧压成冷蓝紫，另加一个只在 terminator 暗侧生效的 `uShadowLift`——灯笼光池读作“暖”而不是“比较不黑”，而全画面没有一处纯黑（`3` 关掉点光源、`4` 关掉雾，各自少了什么一眼可见）。

木箱同时是“随机漫游”和“FBO 环境”两件道具。漫游：`t = 3 s` 起沿一条**由 seed 决定的路径表**走（0.1 s 固定步长、预计算 600 s、走出 16 m 半径就转向回家而不是反弹），朝向把箱子的那一面正脸对着行进方向——“在某一时刻随机向一个方向移动”这句话整个住在表里，渲染循环不挑方向，所以 `--freeze-at` 之后箱子位置是时钟的纯函数。环境：六面 RGB16F cube（默认 256 px；near 0.35 让箱子脚下那面不被裁进反射，far 260 让雾先于远平面把距离收掉），**箱子动了才重抓**（位移 > 1e-4 m），停着时 HUD 打 `faces 0`、反射静止不动——同一件事的两面。抓那六次 draw 里箱子自己不画，且 unit 11 暂时指回星空 cube：往一张图里写同时又从它采样，在 GL 里是未定义而不是“运气不好”。反射与折射取同一个 cube，逐像素由 Fresnel 曲线混合（掠射角偏镜、正对偏透），`mix` 默认只有 **0.18**（按“混合低一点”的要求；到 0.5 木头就不再是木头了），`[`/`]` 或 `--env-mix` 抬上去做取证对照。

取证口径与仓库约定一致：`--off hud`、`--freeze-at` 不晚于 `--snap-at`、指针未捕获时输入层不驱动仿真；同参数两次运行 `cmp` 逐字节全等。贴脸机位 `--pos-x 2.5 --pos-y 1.15 --pos-z 4.6 --yaw 0.50 --pitch -0.10 --env-mix 0.5 --env-lod 0.7` 下三组对照的像素差 bbox **恰好落在箱子那一块**（(617,466)–(986,773)），也就是“只有箱子的像素被环境立方体改动过”这件事本身：`--off capture` ↔ 默认最大差 95/255（默认帧映出身后那盏灯笼的暖池与树线的冷紫，关掉后同一批像素只剩冷紫底 + 离散星点——镜像天空而不是镜像森林）；`--off refract` ↔ 默认最大差 39/255；`--env-mix 0.6` ↔ `--env-mix 0` 最大差 88/255。

**操作**：`W`/`A`/`S`/`D` 飞行、`Space`/`Ctrl` 上下、`Shift` 减速（反射只有一米半宽，6 m/s 一步就跨过箱子）、鼠标转向（指针捕获）；`1` FBO 抓取、`2` 折射、`3` 灯笼点光、`4` 雾、`R` 放行/停住箱子；`[`/`]` 环境混合（0–0.60）、`-`/`=` 飞行速度、滚轮 fov；`C` 剔除、`V` 线框、`B` Bloom、`H` HUD、`F` 释放/捕获指针、`Esc` 只释放指针、`X` 退出。CLI features `capture,refract,lights,fog,roam,hud,bloom,cull,wireframe,cursor` + options `stars,star-size,seed,trees,env-size,env-mix,env-lod,emissive,ior,band,moon-x,moon-y,moon-z,moon-int,density,start-at,speed,fov,yaw,pitch,pos-x,pos-y,pos-z,freeze-at,snapshot,snap-at`；HUD 打印的是回读到的 GL/路径状态（`planted 817 fir + 583 broadleaf`、`env FBO capture 256 px mix 0.18 lod 1.5`、`faces 0 = crate parked`、`wander moving t 6.0 s heading 214 deg 4.1 m away`、`gooch band 0.16 warm/cold/lift` 三组实际进 GPU 的数），改了 uniform 而这一行不变就说明改动根本没上 GPU。

## 3. Demo 索引

| demo（目录 = 可执行名） | 演示的子系统 | 运行 |
|------|------|------|
| `pbr_showcase` | 全链路成品场景（PBR+CSM+IBL+Bloom+天空盒+实例化+HUD+拾取） | `./scripts/run.sh pbr_showcase` |
| `voxel_terrain` | 体素原语验收场（chunk 流式 + 编辑 + 碰撞 + 粒子） | `./scripts/run.sh voxel_terrain` |
| `glass_water` | 五层透明状态机（Sky/Opaque/Cutout/Blend/Glow）与四道独立闸门：远→近排序（水面按 tile 与物体同表竞争）、blend 层不写深度、闭合壳双趟、三种混合方程；透明度是电介质 Cook-Torrance（F0 由 IOR 推导、alpha 由反射+吸收守恒得出、Beer-Lambert 斜程吸收）；`Tab`/`--on ortho` 切透视与正交投影 | `./scripts/run.sh glass_water` |
| `night_forest` | 实例化夜景（两份 `InstancedMesh` 的 1400 棵树带 offset + 统一缩放，5 draw / 1406 实例）：逐顶点色（location 9）与它兼任的材质掩码、CPU 生成的无缝 HDR 星空 cube（splat 而非 gather）与可选全景重投影、Gooch 暖/冷非真实光照、木箱由 seed 路径表驱动的随机漫游、六面 RGB16F FBO 抓取驱动的反射+折射（Fresnel 混合、默认 `mix` 0.18） | `./scripts/run.sh night_forest` |
| `pbr_lighting` | LightBuffer UBO + PbrMaterial + 球阵（最小 lit scene） | `./scripts/run.sh pbr_lighting` |
| `shadow_csm` | CascadedShadowMap + ShadowPass 定向光投影 | `./scripts/run.sh shadow_csm` |
| `ibl_environment` | EnvironmentMap::Generate + IBL 采样 | `./scripts/run.sh ibl_environment` |
| `postprocess_bloom` | PostProcessChain（HDR/MSAA/Bloom/ACES） | `./scripts/run.sh postprocess_bloom` |
| `skybox` | SkyboxRenderer + 独立 SkyboxPass（只留背景） | `./scripts/run.sh skybox` |
| `instancing` | InstancedMesh GPU 场 + 实例化 pass | `./scripts/run.sh instancing` |
| `particles` | ParticleBatch（`GL_PROGRAM_POINT_SIZE` point-sprite） | `./scripts/run.sh particles` |
| `text_hud` | Font + SpriteBatch + TextRenderer 叠 HUD | `./scripts/run.sh text_hud` |
| `camera_picking` | Camera + Frustum 剔除 + PickRay/PickNearest | `./scripts/run.sh camera_picking` |
| `debug_draw` | DebugDraw 线框 + Profiler CPU/GPU 帧计时 | `./scripts/run.sh debug_draw` |
| `geometry_upload` | GeometryFactory → MeshData → Mesh::Upload（无光照纯色） | `./scripts/run.sh geometry_upload` |
| `hello_triangle` | 基线对照：手搭 VertexArray/GLBuffer/AttachAttribute 画 `src/main.cpp` 同一个三角形 | `./scripts/run.sh hello_triangle` |
| `texture_samplers` | Texture2D + Sampler（NEAREST vs LINEAR 对照） | `./scripts/run.sh texture_samplers` |
| `textured_sphere` | 两种细分共用一条索引流：CPU 侧重网格（`segments`/`rings` 即经线纬线数，`Mesh::Update` 原地重网格）↔ 硬件 TCS/TES（同一 MeshData 以 `Mesh::DrawAs(GL_PATCHES)` 画，粗笼由 TES 重投影回球面）+ 三张程序化 equirectangular 贴图（UV 网格/checker/噪声 planet）+ `C` 关剔除读绕序 | `./scripts/run.sh textured_sphere` |
| `model_loading` | AssetManager 异步 + GLDX_ENABLE_ASSIMP=OFF 优雅降级 | `./scripts/run.sh model_loading` |
| `multi_viewport` | 多窗口同步视图：每窗独立 context 各自上传、单线程串行重渲、原子共享时钟/轨道（拖任一窗全窗同步） | `./scripts/run.sh multi_viewport` |
| `render_passes` | 多窗口 + for 循环装配不同 `RenderPass` 子类（Clear/Triangle/Quad/Line/Point），各窗独立 context 共设同一原子时钟 | `./scripts/run.sh render_passes` |
| `multi_window_levels` | 多窗口的四个正交维度：同步↔异步时钟、per-window 视锥剔除（同一场不同 `visible/total`）、LOD/视距分档、以及一个与其余窗完全无关的独立 context（island） | `./scripts/run.sh multi_window_levels` |
| `shader_file` | `CreateFromFiles` 从磁盘加载 GLSL（坏路径 exit 1，无路径退回内嵌源） | `./scripts/run.sh shader_file` |
| `geometry_shader` | `CreateFromSources({Vertex,Geometry,Fragment})` 选择性装配几何阶段（点→方块阵） | `./scripts/run.sh geometry_shader` |
| `geometry_shader_file` | `CreateFromFiles({{Vertex},{Geometry},{Fragment}})` 多阶段从磁盘加载（含从文件装配的几何阶段；坏路径 exit 1，无路径退回内嵌源） | `./scripts/run.sh geometry_shader_file` |
| `shader_stages` | `CreateFromSources` 一次装配全 5 个图形阶段（V+TC+TE+G+F），画 GL_PATCHES 细分线框 | `./scripts/run.sh shader_stages` |
| `menger_sponge` | 顶点/几何阶段**生成**几何：`uniform int uLevel` 即细分层级（无 mesh 重建）；另一臂用 `gldx::TransformFeedback` 让 GPU 自行细分，绘制数直接取 `PRIMITIVES_GENERATED` | `./scripts/run.sh menger_sponge` |

> 最小实现基线不在表内：`./scripts/run.sh`（默认目标 `GLFW_Template`）即 `src/main.cpp` 的 hello-triangle（高层 `Mesh` 路径）；其手搭底层对照版在表内的 `hello_triangle` 行。

## 4. 无头取证：命令行开关之外

退出时体素 demo 打印一行统计（帧数、平均/最低 fps、生成与网格化的 chunk 数、破坏方块数、存活/累计粒子数），流式收敛与粒子压力这类“截图看不出来”的性质靠它自证。

CPU 侧原语（体素 DDA、Chunk、ChunkMesher、BlockRegistry、Noise、Frustum）另跑过一轮 AddressSanitizer + UndefinedBehaviorSanitizer 的对抗输入自检（NaN / inf / 1e30 / `INT_MIN` 坐标、未注册 id、越界访问、反向包围盒），做法记在 [AGENTS.md](../../AGENTS.md) 的“CPU 侧原语的对抗输入自检”一节。画廊里这些开关同样按这个形状喂过一遍畸形 argv（`--select inf`、`--rise nan`、`--auto-break 1e300`、拼错的功能名），各 demo 都正常退出且不会把 `inf` 转成整数。

共享开关（`--off/--on/--quit-after/--freeze-at/--snapshot/--snap-at/--yaw/--pitch/...`）的完整总表与逐项语义在 [①·补 窗口·输入·命令行 §8.1](2a-window-input-cli.md)。

## 5. cube 面约定：一个只能靠回读 GPU 才能发现的 bug

任何把内容写进 `samplerCube` 再自己采回来的代码（`EnvironmentMap` 生成天空、`night_forest` 抓箱子那六面、自备全景重投影）都在用一张“每面 `{right, up, facing}`”的面基表，而这张表**不是口味问题**：OpenGL §8.13 先按主轴选面，再把剩下两个带符号分量作 (sc, tc)，即 `s = (1 + sc/|ma|)/2`、`t = (1 + tc/|ma|)/2`。全仓只有一份：`src/gldx/texture/TextureCubeMap.h` 的 `gldx::kCubeFaceAxis` / `CubeFaceAxis(face, which)` / `CubeFaceDirection(face, s, t)` / `CubeFaceTexelDirection(face, i, j, n)` / `kCubeFaceTargets`（面序恒为 GL 枚举序 +X −X +Y −Y +Z −Z），引擎 pass 与 demo 都消费它，**自己再抄一份就是已知的病因**。

为什么要专门提一句：这类错**不会报错，也不会裂接缝**。写侧和读侧用同一张错表时，平滑内容（天空、渐变色）在六个面之间依然自洽，看起来一切正常；坑的是**落点**——太阳圆盘被写进的方向和 sampler 真正读回来的方向不一致，于是每个反射面、每颗高光都在看向“被请求方向之外”的天。取证不能靠看，得把 GPU 上的像素拉回来算：`EnvironmentMap::Generate()` 后 `glGetTexImage` 回读六面 → 找全 cube 最亮纹素簇的流量加权质心 → 用 §8.13 闭式反投影成一个方向 → 与请求方向求夹角。引擎旧表用这个方法量出来是：太阳正对一个面中心时差 **8.0–8.1°**，而 ±Z 两面因 `facing` 符号写反差 **171.9°**（“往后看”的画面被涂进了 +Z 面）；改成消费唯一一份表后 **0.0–0.1°**（残差就是小面分辨率下的纹素量化）。手性也能当自检：合法表必须六面 `right × up` 与 `facing` 同号（GL 约定下恒为 `-facing`），旧表 ±X 两面是 `+facing`（镜像）——镜像不是“转错了个角度”，而是根本就是另一张表。完整叙述见 [CHANGELOG](../../CHANGELOG.md) 的“cube 面基表不再是‘一种口味’”条目。
