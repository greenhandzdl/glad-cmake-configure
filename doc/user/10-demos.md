# 🟢 Demo 画廊：每个 demo 演示什么、怎么按键

> 从根 [README](../../README.md) 拆出：README 只留最小事实与快速开始，**逐 demo 的画面叙述、键位与索引表在这里**。想学“怎么做对”而不是“长什么样”的，去对应章节——透明与投影见 [进阶 §1.1–§1.3](8-advanced.md)，细分与绕序见 [② 几何与场景](3-geometry-scene.md)，命令行开关见 [①·补 §8.1](2a-window-input-cli.md)。

每个 demo 都只装配它演示的那一个子系统，其余留缺省（这本身就是“按需装配”的验收）：`./scripts/run.sh <demo>` 运行，`--quit-after N` 无头自检，`--help` 列出该可执行真正认识的开关与选项。

## 1. 最小实现基线：`GLFW_Template`

`src/main.cpp` 打开一个 800×600 窗口，只画一个三角形：一个自定义 `RenderPass` 在 `Execute` 里 `glClear` + 内联 GLSL 编译的着色器 + `Mesh::Draw()`，几何只以 CPU 侧 `MeshData`（3 个 `Vertex`，position+color）表达，VAO/VBO 与固定属性布局全部由 `gldx::Mesh` 代劳；逐帧只填 `fbWidth/fbHeight` 的空 `RenderFrame`。窗口与帧循环由 `gldxwin` 包办（`gldx::win::App::Get()` 拉起 glfw/GL 4.1 上下文，一个 `Window` 的 `OnFrame` 里驱动渲染），它不建 PBR 管线、不申请 LightBuffer/Scene/后处理——用来证明“只 `import` gldx 三件套、不申请任何可选子系统也能出图”，是理解引擎最小使用面的基线。想看 `Mesh` 背后手搭 `VertexArray`/`GLBuffer`/`AttachAttribute` 的底层版本，对照 demo 在 [`../../src/demo/hello_triangle/`](../../src/demo/hello_triangle/main.cpp)（`./scripts/run.sh hello_triangle`），两份源码 diff 即是 `Mesh::Upload` 帮你做掉的事。

## 2. 成品演示

五个把 HUD 当产物写的 demo（`voxel_terrain` / `glass_water` / `night_forest` / `anim-rigid`，加单功能 demo `text_hud`）用的是同一个引擎类 `gldx::Hud`（`src/gldx/text/Hud.h`）：行列表、面板按最宽行自 sizing、四角锚定、右对齐测量、行高推进、屏幕中心标记、以及"整块 overlay 只 begin/end 一次批"的记账全在引擎侧，demo 的 `Hud.{h,cpp}` 派生 `gldx::HudPass` 后**只写一个 `Build()` 负责拼字符串**（数字可以是 pass 的统计量、输入结构体的字段，或 `main.cpp` `snprintf` 好的一个 `std::string`）。面板颜色只有三个常量，全仓含义一致：`kFg` 是这个 demo 在主张的那件事、`kDim` 是上下文与键位、`kWarn`（琥珀）是"本帧缺了面板断言的东西"。写法与逐条口径见 [进阶 §6](8-advanced.md)。还在手排 `TextRenderer` 的是 `textured_sphere`（自己 `Measure` 量宽 + 画底板 + 手推 y 的左上角三行面板，正是 `gldx::Hud` 要替掉的那种写法——本轮只收敛上面四个 demo，它还没迁）与 `pbr_showcase`（用引擎自带的 `DebugHudPass`）。

### `pbr_showcase`：全链路交互场景

`./scripts/run.sh pbr_showcase` 是完整场景：带纹理的地面与球阵、金属立方体、一个旋转的子层级（carousel，演示场景层级变换传播），配合级联阴影、IBL 环境光照、HDR + Bloom + ACES 后期、天空盒，以及精灵批次文本 HUD。绘制不再是内联 `gl*` 调用，而是逐帧组装一个 `RenderFrame` 后一句 `renderer.Render(frame)`；`BuildPbrPipeline()` 依序跑 Shadow → Geometry → Skybox → PostProcess → DebugHud，这些都是**按需装配**的可选 pass。

**操作**：拖拽轨道旋转 / 滚轮缩放；`A`·`D` 太阳方位、`W`·`S` 太阳高度；右键拾取高亮；`1` 阴影、`2` IBL、`3` Bloom、`4` 线框、`5` 实例化、`6` 天空盒、`Tab` 透视/正交；`Esc` 退出。

### `voxel_terrain`：可玩体素世界

`./scripts/run.sh voxel_terrain` 是体素原语的验收场：fBm 高度场地形 + 沙滩 + 湖泊 + 树冠，worker 线程生成与网格化、渲染线程限量上传，雾随距离收掉视距边缘。世界层（chunk 网格、流式策略、地形生成在 `World.{h,cpp}`，输入回调在 `Input.{h,cpp}`，HUD 行组装在 `Hud.{h,cpp}`：派生 `gldx::HudPass`，把 `main.cpp` 每帧 `snprintf` 好的一个 `std::string` 整块交给 `hud.AddBlock()`）全部写在 `src/demo/voxel_terrain/` 内（`main.cpp` 只留窗口/帧循环装配），引擎只提供原语、不含任何世界概念。

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

地面不必是一张平面。`--terrain` 有 `flat / random / hills / contour` 四档，全部由 `Terrain.{h,cpp}` 这一个类给出（它就是实验指导书那个 `CTerrain` 的现代等价：`SetSize / InitTerrain / LoadRawFile / GetHeight / GetAveHeight / BuildMeshData`）。`random` 就是指导书的双重循环逐格掷点，`hills` 是同一份噪声（`Noise.h`，与草地贴图共用一个真源）加 `--terrain-smooth` 的几次盒式平均，`contour` 从外部读高度——16 位小端无头 `.raw` 或任何 png/jpg 灰度图（`--terrain-map`），再用 `--terrain-levels N` 切成指导书那种梯田。关键的一点是**高度场不增加任何 draw**：它替换的仍是地面那一次提交，`5 world draws / 1406 instances` 在四档下一字不变；而 `heightAt()` 是全场唯一一处“这个点多高”的答案，树根、灯笼柱、箱子路径、漫游相机都问它，所以打开 `--terrain hills` 不会看见树浮在山头上。`--on walk` / `G` 让相机沿 seed 决定的路径表以 1.70 m 眼高贴地走，爬坡过堑是真发生而不是穿模。指导书原文有四处它自己没做的，这里逐个补掉：`MAP_W` 格网格要 `(MAP_W+1)²` 个顶点（`Vertex + MAP_W` 那套索引配它自己的 `MAP_W²` 数组会越界并把三角斜切）、固定管线不会自己生法线（跨 tile 沿用上一次 `glNormal3f` ⇒ 山脊涂成山谷）、它的高度恒正 ⇒ 整块地面浮在 y=0 之上（这里归一到 ±relief/2）、`.raw` 无头 ⇒ 靠 `m_nWidth` 假定尺寸就是越界读（这里校验字节数，对不上就报错回落平面）。梯田量化的 band 索引也要夹紧：不夹时 `v == maxH` 会除出第 n 档，台面中心落到 `maxH + step/2`，越出刚承诺的包络半个台阶。测试数据由 `make_heightmap.py` 现生成（闭式函数、无 RNG，重生成逐字节相同），产物只落 `outputs/.tmp/`。

取证口径与仓库约定一致：`--off hud`、`--freeze-at` 不晚于 `--snap-at`、指针未捕获时输入层不驱动仿真（`Input.cpp` 里鼠标与滚轮各有一道 `if (!in->captured) return;`）；同参数**单发**重跑 `cmp` 逐字节全等。反过来，把十几条取证命令**连跑成一个脚本**时会出现一种必须记住的抖动：每轮失败的张次都不一样，量级却是不满 100 个像素、最大 7–10/255，bbox 落在树与雾那一带——那是 GPU 末位级抖动被连跑放大，不是某一帧坏了，所以仓内取证一律单发逐条。贴脸机位 `--pos-x 2.5 --pos-y 1.15 --pos-z 4.6 --yaw 0.50 --pitch -0.10 --env-mix 0.5 --env-lod 0.7` 下三组对照的像素差 bbox **恰好落在箱子那一块**（(617,466)–(986,773)），也就是“只有箱子的像素被环境立方体改动过”这件事本身：`--off capture` ↔ 默认最大差 95/255（默认帧映出身后那盏灯笼的暖池与树线的冷紫，关掉后同一批像素只剩冷紫底 + 离散星点——镜像天空而不是镜像森林）；`--off refract` ↔ 默认最大差 39/255；`--env-mix 0.6` ↔ `--env-mix 0` 最大差 88/255。

**操作**：`W`/`A`/`S`/`D` 飞行、`Space`/`Ctrl` 上下、`Shift` 减速（反射只有一米半宽，6 m/s 一步就跨过箱子）、鼠标转向（指针捕获）；`1` FBO 抓取、`2` 折射、`3` 灯笼点光、`4` 雾、`R` 放行/停住箱子；`[`/`]` 环境混合（0–0.60）、`-`/`=` 飞行速度、滚轮 fov；`C` 剔除、`V` 线框、`B` Bloom、`H` HUD、`G` 切贴地漫游、`F` 释放/捕获指针、`Esc` 只释放指针、`X` 退出。CLI features `capture,refract,lights,fog,roam,hud,bloom,cull,wireframe,cursor,walk` + options `stars,star-size,seed,trees,env-size,env-mix,env-lod,emissive,ior,band,moon-x,moon-y,moon-z,moon-int,density,start-at,speed,fov,yaw,pitch,pos-x,pos-y,pos-z,freeze-at,snapshot,snap-at,terrain,terrain-cells,terrain-cell,terrain-relief,terrain-smooth,terrain-levels,terrain-map,terrain-tile,walk-radius`；HUD 打印的是回读到的 GL/路径状态（`planted 817 fir + 583 broadleaf`、`env FBO capture 256 px mix 0.18 lod 1.5`、`faces 0 = crate parked`、`wander moving t 6.0 s heading 214 deg 4.1 m away`、`gooch band 0.16 warm/cold/lift` 三组实际进 GPU 的数），改了 uniform 而这一行不变就说明改动根本没上 GPU；面板末行还会报字体后端与图集（`font freetype  48 px bake  95 glyphs  1024x1024 atlas`）——这是唯一一行“截图本身证明不了”的事实，因为 FreeType 与 stb_truetype 兜底画出来的字几乎一样。

### `anim-rigid`：带骨架与内嵌贴图的 FBX、GPU 蒙皮、和一段自己写的走路

`./scripts/run.sh anim-rigid` 的画面只有一个角色：站在一块无限延伸的淡蓝灰地面上，腿一前一后地走，地面网格朝镜头滚过来，脚下有一团跟着移动的接触阴影。它要演示的事是**骨架、蒙皮权重与材质贴图全部来自文件本身，动画则完全不是**。参考资产带 23 根骨骼、17 个 skinned mesh、36443 顶点 / 17531 三角形、四种 PBR 贴图**内嵌在 FBX 二进制里**（最大 4096²），却**一个动画片段都没有**（`AnimationStack` 数为 0），所以走路是 demo 按钟表算出来的：姿态是时钟的纯函数，不播放任何 clip，也不累积 dt（逐帧读 `min(time, freezeAt)`）。这也是为什么 `--freeze-at` 之后同一组参数能画出同一帧——取证时唯一要注意的是快照时刻必须**晚于** freeze 时刻（见下面的一行命令）。

**加载链路**（两阶段，Stage A 在工作线程）：`ModelLoader::LoadSkinned` 返回 `LoadedSkinnedModelData`（`meshes` + `skeleton` + `materials`/`meshMaterial` + `textures` + `missingTexturePaths`），它做的四件事各自都会静默出错，所以都写在注释里：① **把每个 mesh 节点的场景变换烘进顶点**——assimp 交出的是节点局部空间的顶点，而 cluster 的 bind 矩阵在 bind 场景空间（这份 FBX 是 Z-up，Y-up 转换停在 mesh 节点上），不烘就会让骨架相对自己的皮转 90 度；② **静止姿态由 inverse-bind 矩阵推导**而不是读文件的节点变换（这份资产上两者互相矛盾），于是“空姿态 == bind 姿态”是构造出来的，`W` 关掉走路回到 bind pose 就是这条性质的现场证据；③ **贴图字节自己解码**：这份 FBX 把图**内嵌**在文件里、材质指向一个并不存在的 `".fbm/"` 目录，只回路径的契约会把一个贴满图的身体报成裸模，所以 loader 用 `stbi_load_from_memory` 在工作线程解码，按用途定 `desc.srgb`（只有 albedo 是 sRGB），并把 **v 翻一次**（assimp 的 UV 以左下角为原点、解码后的 PNG 首行在上，不翻就是镜像采样）；④ **父子关系按名字从节点树取**：这个 FBX 里 `aiBone::mNode` 全是空的，只沿 mNode 爬会得到一具“全平”的骨架——而平骨架在 bind pose 下画得完美无缺，只有动起来才暴露成“脚不跟着腿”。`SkinnedMesh` 只是几何 owner（loc 0/1/2 = position/normal/uv、**5 = 骨骼索引**、6 = 权重），骨骼索引走 `VertexArray::AttachIntegerAttribute`（`glVertexAttribIPointer`：`uvec4` 经浮点归一化读出来可能指向隔壁骨骼）。每帧 `Skeleton::ComputeSkinning` 产出一份 `palette`（最多 `kMaxBonePalette = 64`，本资产用 23），顶点着色器做 4 影响线性混合。第一帧会**等** worker 返回再上传，所以相同 argv 的每一次运行后续帧完全一致。

**走路**是绕**世界轴**写的（大腿绕横向轴摆），而每根骨骼自己的轴来自绑定，所以 `PoseRotation()` 用该骨骼的 rest world 矩阵把世界轴折进它的局部系；哪个方向是上/横向也不是常量，而是加载时从骨骼位置**量**出来的（上 = hips→head，横向 = 左大腿 − 右大腿，前向 = 叉乘）。步态本身按“支撑相 60% + 摆动相 40%”的两段曲线写：髋角在支撑相里线性后扫（脚是钉住的，骨盆匀速越过它，缓动反而会让角色一冲一冲）、摆动相用余弦缓动；膝屈只在摆动相前段达到峰值（峰值放在摆动中段就是正步走）；踝在脚跟着地时背屈、蹬地时跖屈，脚趾关节只在支撑相末尾、脚跟已经离地时弯；手臂与同侧腿反向摆。

**手臂有三个各自的坑，一处都不能少**。这套绑定**手部一根骨骼都没有**（23 根骨骼全在躯干与四肢，没有手指），但手部网格**同时受 Hips/Spine/Head 和腕部链影响**——它自带一个会动的扇形（四根手指随腕部旋转张开、收拢），所以“手看起来是活的”只需要腕部转动就够了；换成一个 100% 绑在自己骨骼上的手，同一段代码就会显得僵硬。三个坑：

- **T-pose 必须降到 A-pose**。绑定姿态是 T-pose，在 T-pose 上绕横向轴“摆”是让手臂绕自己长度自转，画面上等于没动，所以手臂先绕前向轴降下来（`armDown`）。**但不是降得越多越好**：降到 1.45 rad（83°，几乎垂直向下）时手腕正好落进裙子的锥体里——肩 x≈0.17、腕 y≈0.85 那圈的裙半径≈0.21，而腕在≈0.20，于是**手和裙子穿模**。现在的值是 **1.05 rad（60°）**。
- **摆幅要跟着裙子收敛**：裙子是蒙在大腿上的（skinned），大步幅会把裙摆拖着走，所以 `hipSwing` 是 0.33 rad 而不是 0.36，`kneeSwingFlex` 0.72、`armSwing` 0.38。
- **间隙靠外展拿，不靠更往前屈肘**。裙摆在髋高处有 0.2 m 宽，垂下来的腕会被它吞掉，所以除去屈肘（`elbowBase` 0.55，两个摆动相都保持）之外还要 0.05 rad 的前臂**外展**（`armOut`）；继续把肘往前屈只会让前臂更靠近裙面，方向正好相反。
- 于是手不再是“两连杆钟摆的末端、朝向永远不变”那个死样子，而有三处远端旋转：±0.045 rad 的**锁骨前伸**（`clavicleRoll`，肩膀跟着手臂走一点）、**腕屈**（`wristBase` 0.14 起步、`wristSwing` 0.16 让手掌滞后于前臂）、**腕偏**（`wristDeviate` ±0.07，掌心随摆动内外转）。腕一转，手网格那个扇形跟着张开收拢——手指不是骨骼驱动的，但它们也不是刚性的。

**这三处一度全是死代码——查句柄比调参数更值钱**：`RigProfile::Classify()`（当初是这个 demo 里的 `ClassifyRig()`）只填了 `armUp`/`legUp`/`foot`/`toe` 这些链条，`rig.hand[2]` 与 `rig.clavicle[2]` 停在初始化值 `-1`；而所有腕/肩的调用都写成 `rotate(rig.hand[s], …)` / `rotate(rig.clavicle[s], …)`，`rotate()` 见 `bone < 0` 直接返回。于是**此前每一个渲染帧、每一张截图里腕屈、腕偏、锁骨前伸都是空操作**，而那套参数一直看起来“已经调好了”——手仍然是僵的，原因不在角度而在句柄。修法是把 `LeftHand`/`RightHand`/`LeftShoulder`/`RightShoulder` 和手臂链一起按名字查出来（这套资产里 `hand = {22, 14}`、`clavicle = {16, 11}`），而不是再加一个旋转：**先让已有的旋转真的落在骨头上，手的动作是捡回来的，不是新写的**。这也是本页处处坚持“量出来再写进正文”的原因——参数表本身不会告诉你它有没有接上。**这个 bug 现在是可复现的一行断言**：`--on report` 除了步态表还打一行 `[anim-rigid] rig: 20/20 slots bound`，任何一个槽位没解析出来都会被点名（坏构建上是 `; unbound: hand.L hand.R clavicle.L clavicle.R`）。把它写进文档的理由不是“多了个功能”，而是这条教训：**静默降级比崩溃更难查，所以给降级路径补一条出声的断言**——`rotate(-1)` 那种“参数越界就当没发生”的写法，会让一个真实缺陷以“手感差”的形态活过好几轮；换一套骨骼命名不同的资产，今后是当场报出未绑定的槽位，而不是再画一只僵硬的手。

**骨盆的上下不是手写的 bob**：每只脚取脚跟与脚掌两个接触点（从 bind pose 一次取好，之后由 foot/toe 骨骼带着走），摆完姿势后找到最低的那个鞋底点，再把整个 `palette` 平移同样的量。于是“一个周期沉两次、双支撑最深”是解出来的而不是调出来的。这条求解在 HUD 上可以当场读出来：默认 `ground: pelvis  +9.4 mm | lowest sole  +0.0 mm`，而**关掉求解**（`--off ground`）同一帧变成 `lowest sole -13.0 mm`——脚陷进地板 13 mm，因为刚性骨骼链在 `p=0.000`（脚跟着地那一刻）根本无法让两只脚同时贴地（报告里 `soleL +0.0307 / soleR -0.0278`），求解器只能把前脚脚跟抬起来约 6 cm。这是没有 foot IK 的**诚实残差**，不是 bug：真实答案要么是脚 IK，要么是烘焙好的步态。

**第二处残差在裙子上，没有修，也不假称修了**：走路时裙摆有一道撕开的三角翻片——下摆有一块面被拖到锥体之外，像被掀起来的一角；它在多个相位（`--freeze-at 0.9` 与 `3.4`）都在，只是形状随相位变，而 bind pose（`--off walk`）里完全没有。原因是资产权重：那圈下摆顶点的**主导影响是大腿骨**，于是它们按全权重绕髋旋转，而不是在髋与腿之间过渡（`hipSwing` 只是把它们转出去的那只手）；所以翻片**保持形状**、像一块刚体三角被拖到锥体外，而不是被拉长——这是“权重没有过渡”，不是姿态代码的问题。它与上面“刚性两连杆无法同时踩平两只脚”是同一类诚实残差，解法同样不在角度里：要么给裙子补一条骨骼链（本资产没有），要么重新刷权重；调 `hipSwing` 只会让裂口换个位置。

**原地走（treadmill）与 `--speed`**：地面网格按 `speed*time` 往后滚，屏幕取景因此固定，变的只有走路本身。一个周期的行程由腿长与髋摆幅推出来（`4 * legLength * sin(hipSwing)`；参考资产腿长 0.90 m、周期 1.17 m），地面行程与步频共用这一个数——所以 `--speed` 改的是**步频与地面滚动**，不是步幅；两者一旦脱钩，脚就在地上打滑。

**这个故事里哪部分现在归引擎**：骨架解析、姿态数学、GPU 持有与地面反解都已经搬进 `src/gldx/anim/`，这个 demo 只剩"走路"这一件属于它自己的事。四件东西各有明确归属——`RigSlot`/`RigNaming`/`RigProfile`（语义槽位 + 一张**可扩展的命名族接口**：Mixamo/Biped/UE 各是一个 `RigNaming` 派生类，加混元只需再写一个派生类；解析完再从髋→头和左右大腿**量出**三轴，所以换一套骨骼命名的资产是"报出未绑定的槽位"而不是"手变僵了"）、`Pose`（`RotateBone` 把世界轴角度折进骨骼局部系）、`RiggedModel`（Stage-B 持有者，含 1×1 哑元贴图的兜底）、`GroundContact`（四个鞋底接触点反解骨盆离地量）。**刻意没搬的是步态本身**：`GaitTuning`、髋/膝/踝曲线、treadmill 都是这个 demo 的主张，不是通用能力——把它塞进引擎只会让下一个使用者被迫接受一套走路风格。这条界线的判据是"换一个 demo 会不会原样再用一次"：`RigProfile` 会，步态不会。用法见 [🟡 ③ 资产加载 §5](4-assets-loading.md)。

**渲染**是两个 GLSL 410 程序，都直渲默认帧缓冲（不走 `PostProcessChain`，所以着色器自己 ACES + `pow(1/2.2)` 收尾）：一个单太阳的蒙皮 PBR（GGX + 高度相关 Smith + Schlick，albedo/metallic/roughness 贴图与材质标量相乘，法线贴图用 cotangent frame，半球天光补光），一个地面（程序化网格线 + 椭圆接触阴影 + 向天空的远处淡出）。程序里的四个 sampler **每次 draw 都绑一张完整贴图**：文件缺哪一种图就绑 1×1 的哑元（白 albedo、平法线 `(128,128,255)`、白色标量），因为 Apple 驱动上 active sampler 指向不完整贴图会直接丢掉整个 draw，看起来像“模型不见了”。这条兜底现在由 `RiggedModel::AlbedoOrFallback()` 家族负责，demo 侧没有 null 分支可写。

**draw 数只数在 HUD 那一行上，而这一行本身就是那台天平**：默认 `palette 23/64 bones | bind check 4.8e-07 | 18 draws`，也就是 **17 次 skinned mesh 提交 + 1 次地面网格**；`--off ground` 变成 **17 draws**——正好少掉地面那一次，说明地面求解只占一个 draw、没有别的东西寄生在它上面；而 `--off walk` / `--off maps` / `--off normalmap` 都还是 **18 draws**。两句合起来才是这个 demo 的诚实说法：**走路是免费的**（同一个 18，关掉走路一次 draw 都不省），动画不是播放出来的 clip，而是几十行三角函数；能省下 draw 的只有地面。`--on report` 只往 stderr 打步态表，不打画面里的数字。

还有一条与画面无关但值得写下来的约定：这个 pass 在 `Execute` 开头**自己设 GL 状态**（深度测试/深度写/背面剔除/关混合）。`SpriteBatch::End()` 只恢复 blending，所以一个从上一帧 HUD pass 继承状态的 3D pass 会在深度测试与剔除都关着的情况下画——症状是角色自己的背面与内层衣物透出来，看起来像蒙皮坏了。

**自备一个模型**：仓内**不放** FBX（`*.fbx` 是整树 gitignore：仓库从不提交模型），默认路径 `src/demo/anim-rigid/demo_should_be_ignored.fbx` 只是一个投放位置——把自己的“带骨架 + 带蒙皮权重”的 FBX 丢到那里，或者用 `--model PATH` 指向别处；没有骨骼的文件 `LoadSkinned` 返回 `std::unexpected`（HUD 打红字），而不是画一具静态模型假装成功。加载结果与贴图情况会打在 stderr 上，不用凭猜。

**操作**：`W` 走路开/关（关掉即 bind pose，可作为蒙皮的 A/B 对照组）、`G` 地面、`F` 线框、`H` HUD；拖拽轨道旋转、滚轮缩放；`Esc` 退出。CLI features `hud,walk,ground,wireframe,maps,normalmap,report` + options `model,speed,fov,yaw,pitch,distance,freeze-at,target-y`（`--target-y` 抬轨道中心，默认 0.92 m 在胸口附近）。这些开关**只认 `--on`/`--off`**（`--off ground`、`--on wireframe`），写 `--walk`/`--ground` 会被 `Flags` 报 unknown argument。

**`--on report` 的步态表**（16 行采样，这里摘 7 行；相位是周期比例，关节角是 rad，鞋底是**求解前**离地高度，单位 m。同一路径还会打一行绑定断言 `[anim-rigid] rig: 20/20 slots bound`，见下面「手臂」一节）：

```
0.000 -0.330 +0.140 -0.180 | +0.0307 -0.0278     # 脚跟着地：刚性链两只脚无法同时贴地
0.188 -0.124 +0.031 +0.000 | -0.0054 +0.1681
0.375 +0.083 +0.000 +0.000 | -0.0182 +0.0087
0.562 +0.289 +0.000 +0.315 | -0.0214 +0.0156     # 支撑相末尾，踝跖屈 + 趾关节
0.688 +0.255 +0.720 +0.042 | +0.1766 -0.0038     # 摆动腿抬到最高 0.177 m，而且在身体后侧
0.750 +0.126 +0.634 +0.000 | +0.0877 -0.0093     # 移到身体下方时已经降到 0.088 m
0.938 -0.291 +0.000 -0.162 | +0.0373 -0.0235
```

峰值 0.177 m 出现在 `p=0.688`（还在身体后方）、到 `p=0.750`（移到身体下方）已经掉到 0.088 m——这个**不对称**正是“不像正步走”的来源：正步走会把抬腿的峰值放在脚下最需要间隙的那一瞬。

**无头取证**（`--off hud` 让两次运行之间只剩姿态一个变量；**快照时刻必须晚于 `--freeze-at`**，写同一个值会拍到时钟还没被夹住的那一帧）：

```bash
# 基线帧。A/A：同参数连跑三次逐字节全等（cmp 全等）
GLDX_SNAPSHOT=/tmp/a.bmp GLDX_SNAPSHOT_AT=3.0 ./output/anim-rigid --off hud --freeze-at 2 --quit-after 6

# 对照组一：关掉走路，画的是 bind pose
GLDX_SNAPSHOT=/tmp/b.bmp GLDX_SNAPSHOT_AT=3.0 ./output/anim-rigid --off hud --off walk --freeze-at 2 --quit-after 6

# 对照组二：关掉贴图，退回材质标量（证明那四张图确实来自 FBX 内嵌字节）
# 注意要连 normalmap 一起关：--off maps 只停 albedo/metallic/roughness，法线图仍生效，
# 单独关它反而差 46.36%（测到的是「有法线无底色」，不是「无贴图」）
GLDX_SNAPSHOT=/tmp/c.bmp GLDX_SNAPSHOT_AT=3.0 ./output/anim-rigid --off hud --off maps,normalmap --freeze-at 2 --quit-after 6

# 对照组三：线框，看 17531 个三角形真的都在
GLDX_SNAPSHOT=/tmp/d.bmp GLDX_SNAPSHOT_AT=3.0 ./output/anim-rigid --off hud --on wireframe --freeze-at 2 --quit-after 6
```

参考资产在 2560×1520 上、对最终二进制 `e171a9020a734afc` 实测：`--off walk` 差 **3.20%** 的像素（阈值 >8/255，最大通道差 **228**）、`--off maps,normalmap` 差 **3.44%**（最大 **231**）、`--on wireframe` 差 **62.72%**（最大 **237**）。`walk` 这一项在腕/锁骨修复**之前**是 3.21%，修复只搬动了阈值边缘约 250 个像素（手在画面里很小，这本身是个有用的尺度感）；三个数字在修复后重测、且最终二进制的基线帧与修复构建逐字节相同（`md5 bbf2e477d46e5695f107164135c618d8`），所以复现时应当看到 3.20%。参考资产同一帧的 stderr 与 HUD 是同一组数：`23 bones, 17 meshes, 36443 verts (17531 tris), height 1.64 m, palette 23/64, bind check 4.8e-07`、`textures: albedo yes, normal yes, metallic yes, roughness yes (up to 4096x4096 px); 0 unreferenced map path(s)`、`hierarchy: 1 root bone(s), depth 8, foot chain 4 -> 5 -> 6`；HUD 另报 `leg 0.90 m | cycle 1.17 m` 与 `18 draws`。

**还有一处诚实的粗糙**：`--on wireframe` 会把角色与地面一起画成线框，所以 62.72% 这个数字是“整幅画面的三角形都被改画了”，不是“角色轮廓变了”的度量；要单独量角色就用 `--off ground`（此时 17 draws、27.4% 的画面仍是天空）。

## 3. Demo 索引

| demo（目录 = 可执行名） | 演示的子系统 | 运行 |
|------|------|------|
| `pbr_showcase` | 全链路成品场景（PBR+CSM+IBL+Bloom+天空盒+实例化+HUD+拾取） | `./scripts/run.sh pbr_showcase` |
| `voxel_terrain` | 体素原语验收场（chunk 流式 + 编辑 + 碰撞 + 粒子） | `./scripts/run.sh voxel_terrain` |
| `glass_water` | 五层透明状态机（Sky/Opaque/Cutout/Blend/Glow）与四道独立闸门：远→近排序（水面按 tile 与物体同表竞争）、blend 层不写深度、闭合壳双趟、三种混合方程；透明度是电介质 Cook-Torrance（F0 由 IOR 推导、alpha 由反射+吸收守恒得出、Beer-Lambert 斜程吸收）；`Tab`/`--on ortho` 切透视与正交投影 | `./scripts/run.sh glass_water` |
| `night_forest` | 实例化夜景（两份 `InstancedMesh` 的 1400 棵树带 offset + 统一缩放，5 draw / 1406 实例）：逐顶点色（location 9）与它兼任的材质掩码、CPU 生成的无缝 HDR 星空 cube（splat 而非 gather）与可选全景重投影、Gooch 暖/冷非真实光照、木箱由 seed 路径表驱动的随机漫游、六面 RGB16F FBO 抓取驱动的反射+折射（Fresnel 混合、默认 `mix` 0.18）、`--terrain` 四档高度场（双重循环随机 / fBm 山峦 / 读 png 与 16 位 `.raw` / 梯田量化）与 `G` 贴地漫游 | `./scripts/run.sh night_forest` |
| `pbr_lighting` | LightBuffer UBO + PbrMaterial + 球阵（最小 lit scene） | `./scripts/run.sh pbr_lighting` |
| `shadow_csm` | CascadedShadowMap + ShadowPass 定向光投影 | `./scripts/run.sh shadow_csm` |
| `ibl_environment` | EnvironmentMap::Generate + IBL 采样 | `./scripts/run.sh ibl_environment` |
| `postprocess_bloom` | PostProcessChain（HDR/MSAA/Bloom/ACES） | `./scripts/run.sh postprocess_bloom` |
| `skybox` | SkyboxRenderer + 独立 SkyboxPass（只留背景） | `./scripts/run.sh skybox` |
| `instancing` | InstancedMesh GPU 场 + 实例化 pass | `./scripts/run.sh instancing` |
| `particles` | ParticleBatch（`GL_PROGRAM_POINT_SIZE` point-sprite） | `./scripts/run.sh particles` |
| `text_hud` | `gldx::Hud` + Font + SpriteBatch + TextRenderer 叠 HUD：一个面板里 26/20/16 三种字号（`AddLineAt`）、带换行的 `AddBlock`、右上右对齐的循环色词；`--snapshot` / `--snap-at` / `--freeze-at` 取证（面板自己就是被测对象，没有 `--off hud`，所以这张图是版面证据与字体后端 A/B，不是逐字节基线） | `./scripts/run.sh text_hud` |
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
| `anim-rigid` | 骨架 + GPU 蒙皮 + 内嵌贴图：`ModelLoader::LoadSkinned`（烘 mesh 节点变换、rest pose 由 inverse-bind 矩阵推、`aiBone::mNode` 为空时按名字从节点树取父级、内嵌 PNG 在工作线程解码并翻一次 v）→ `gldx::Skeleton`（FK + 每帧 palette）→ `SkinnedMesh`（`uvec4` 骨骼索引走 `AttachIntegerAttribute`），fragment 是单太阳 PBR 且 albedo/normal/metallic/roughness 四张图都来自 FBX 本体；资产没有动画片段，走路是按钟表算的程序化步态（世界轴经 rest world 折进骨骼局部系、对侧摆臂、膝屈峰值放在摆动前段、接触点解出骨盆高度而不手写 bob），treadmill 地面按 `speed*time` 滚，周期 1.27 m 由腿长与髋摆幅推出 | `./scripts/run.sh anim-rigid` |

> 最小实现基线不在表内：`./scripts/run.sh`（默认目标 `GLFW_Template`）即 `src/main.cpp` 的 hello-triangle（高层 `Mesh` 路径）；其手搭底层对照版在表内的 `hello_triangle` 行。

## 4. 无头取证：命令行开关之外

退出时体素 demo 打印一行统计（帧数、平均/最低 fps、生成与网格化的 chunk 数、破坏方块数、存活/累计粒子数），流式收敛与粒子压力这类“截图看不出来”的性质靠它自证。

CPU 侧原语（体素 DDA、Chunk、ChunkMesher、BlockRegistry、Noise、Frustum）另跑过一轮 AddressSanitizer + UndefinedBehaviorSanitizer 的对抗输入自检（NaN / inf / 1e30 / `INT_MIN` 坐标、未注册 id、越界访问、反向包围盒），做法记在 [AGENTS.md](../../AGENTS.md) 的“CPU 侧原语的对抗输入自检”一节。画廊里这些开关同样按这个形状喂过一遍畸形 argv（`--select inf`、`--rise nan`、`--auto-break 1e300`、拼错的功能名），各 demo 都正常退出且不会把 `inf` 转成整数。

共享开关（`--off/--on/--quit-after/--freeze-at/--snapshot/--snap-at/--yaw/--pitch/...`）的完整总表与逐项语义在 [①·补 窗口·输入·命令行 §8.1](2a-window-input-cli.md)。

## 5. cube 面约定：一个只能靠回读 GPU 才能发现的 bug

任何把内容写进 `samplerCube` 再自己采回来的代码（`EnvironmentMap` 生成天空、`night_forest` 抓箱子那六面、自备全景重投影）都在用一张“每面 `{right, up, facing}`”的面基表，而这张表**不是口味问题**：OpenGL §8.13 先按主轴选面，再把剩下两个带符号分量作 (sc, tc)，即 `s = (1 + sc/|ma|)/2`、`t = (1 + tc/|ma|)/2`。全仓只有一份：`src/gldx/texture/TextureCubeMap.h` 的 `gldx::kCubeFaceAxis` / `CubeFaceAxis(face, which)` / `CubeFaceDirection(face, s, t)` / `CubeFaceTexelDirection(face, i, j, n)` / `kCubeFaceTargets`（面序恒为 GL 枚举序 +X −X +Y −Y +Z −Z），引擎 pass 与 demo 都消费它，**自己再抄一份就是已知的病因**。

为什么要专门提一句：这类错**不会报错，也不会裂接缝**。写侧和读侧用同一张错表时，平滑内容（天空、渐变色）在六个面之间依然自洽，看起来一切正常；坑的是**落点**——太阳圆盘被写进的方向和 sampler 真正读回来的方向不一致，于是每个反射面、每颗高光都在看向“被请求方向之外”的天。取证不能靠看，得把 GPU 上的像素拉回来算：`EnvironmentMap::Generate()` 后 `glGetTexImage` 回读六面 → 找全 cube 最亮纹素簇的流量加权质心 → 用 §8.13 闭式反投影成一个方向 → 与请求方向求夹角。引擎旧表用这个方法量出来是：太阳正对一个面中心时差 **8.0–8.1°**，而 ±Z 两面因 `facing` 符号写反差 **171.9°**（“往后看”的画面被涂进了 +Z 面）；改成消费唯一一份表后 **0.0–0.1°**（残差就是小面分辨率下的纹素量化）。手性也能当自检：合法表必须六面 `right × up` 与 `facing` 同号（GL 约定下恒为 `-facing`），旧表 ±X 两面是 `+facing`（镜像）——镜像不是“转错了个角度”，而是根本就是另一张表。完整叙述见 [CHANGELOG](../../CHANGELOG.md) 的“cube 面基表不再是‘一种口味’”条目。
