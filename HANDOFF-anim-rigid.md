# 交接：anim-rigid demo 的未解决问题

写给下一个 agent。仓库 `/Users/greenhandzdl/Documents/GitHub/glad-cmake-configure`，
先读 `AGENTS.md`（那里的约束优先于本文）。本文上一版列的三个问题**已经解决并复测**，
下面写清「怎么证的」与「剩下什么」。

## 0. 构建与验证

```bash
CMK=$(ls -d /opt/homebrew/Cellar/cmake/*/bin/cmake | head -1)
"$CMK" --build cmake-build-debug --target d_anim-rigid      # 二进制在 output/anim-rigid
```

冻结快照（本项目唯一的像素级验证手段）：

```bash
GLDX_SNAPSHOT=/tmp/a.bmp GLDX_SNAPSHOT_AT=9.0 ./output/anim-rigid \
  --off hud --freeze-at 1.0 --quit-after 12 --action idle \
  --distance 2.2 --target-y 0.85 --yaw 0.0 --pitch 0.12 --fov 40
# 出图 2560x1520，bmp；`--quit-after` 必须大于 `GLDX_SNAPSHOT_AT`，否则永远不拍（踩过）
```

## 1.（已完成）冻结快照不可复现

**上一版症状**：同一个二进制、同样的 argv，连跑两次得到不同像素（1697~14803 px，
`[settlehash]` 三次 5.108 / 4.884 / 4.913）。

**根因**（不是弹簧、不是呼吸时钟、不是探针复用）：结算的**门槛**判错了。

```cpp
if (view.freezeAt > 0.0f) { ... 复位弹簧 + 跑 600 步结算 ... }   // 旧
```

`view.freezeAt > 0` 是「命令行要求冻结」，不是「时钟已经停住」——**第一帧就满足它**。
而 `FrameInfo::time` 是在 `onFrame` **之前**采的（`App::Run` 里 `now = glfwGetTime()` 在回调前），
所以模型加载那次几秒的阻塞不在里面，第一帧拿到的 t 是「窗口/上下文/字体初始化花了多久」，
实测 0.19~0.26 s 且每次运行不同。于是：结算被喂进 t≈0.2 s 的姿态，快照却在 t=1.0 s 的姿态上画；
两次运行连喂进去的姿态都不一样。这也解释了为什么「复位弹簧」和「呼吸时钟跟着动画时钟走」
都治不好——它们改的是**解算器**，而错的是**输入**。

**改法**（`src/demo/anim-rigid/main.cpp`）：

1. 门改成 `view.clock >= freezeAt`（`view.clock` 就是 `min(info.time, freezeAt)`，只有越过之后才恒等于它）。
2. 结算循环每一步 `character.pose = settleBase` 再施加角度。原来 `Solve` 的写回是
   `local[bone] *= rot`，600 步连着叠会把世界矩阵转过 600 次——而循环里的探针、摆腿信号、
   `tuck` 的侧向符号全在读那个世界。
3. 结算结束后把 pose 还原成 `settleBase`：紧接着的常规二次运动还会用 dt=0 再施加一次角度
   （dt=0 只跳过积分、不跳过写回），不还原就等于同一份旋转乘两遍。

**复测**：九种动作（idle/walk/run/crouch/crouchwalk/attack/jump/sit/swim）+ walk 的
0.5/2.0/3.0/9.0 s 四个冻结时刻 + run/swim/sit/jump/attack 各一个，**每个跑两次，
`cmp -l` 差 0 字节**。核对命令：

```bash
for i in 1 2; do GLDX_SNAPSHOT=/tmp/d$i.bmp GLDX_SNAPSHOT_AT=9.0 ./output/anim-rigid \
  --off hud --quit-after 12 --freeze-at 1.0 --action walk \
  --distance 2.2 --target-y 0.85 --yaw 0.0 --pitch 0.12 --fov 40 >/dev/null 2>&1; done
cmp -l /tmp/d1.bmp /tmp/d2.bmp | wc -l     # 期望 0
```

## 2.（已完成）走路时裙摆那道撕开的三角翻片

上一版把这件事归给蒙皮权重（"下摆顶点的主导影响是大腿骨"）。**不是**：
`GLDX_NO_SECONDARY=1`（整条二次运动链关掉）时同一帧的裙摆是**完整锥面**，所以是
`gldx::ChainSolver` 的摆腿驱动把相邻两片推向了相反方向。

根因两处，都在 `src/gldx/anim/SecondaryMotion.cpp`：

* 每片裙片的"朝外"方向量的是 `bonePos - chain[0]`，而 `chain[0]` 只是**第 1 片裙片的根**，
  不是身体轴（实测各片根部相对它的侧向偏移 -0.071~+0.198 m）。对面那几片的指向会**穿过身体**，
  于是 `sideSign` 被分到错的一侧，相邻两片拿到方向相反的 `gaitSwingL/R`。
* `side`/`fwd` 是从 `(0,0,1)` 推的，隐含假设"身体正对世界 -Z"；转身时实测 `fwd` 只取到 ±0.85。
  而且左右是**硬切**（`sideSign >= 0 ? L : R`），中线两侧紧挨着的两片必然拿到反号。

改法：中线取各片**根关节的水平重心**，`side`/`fwd` 取**链根骨的世界轴列**，左右按侧向分量
线性混合（`wl = clamp(0.5 + 0.5*lat)`）。实测摆腿角从 `-0.093 / +0.180`（相邻两片差 0.27 rad）
变成 `-0.032 / +0.120`（差 0.15 rad），画面上翻片消失、裙摆回到锥面。`tuck` 的侧向符号也改用同
一个中线（原来同样分错边）。

**还留着、但属于资产而不是动画**：下摆花边在 4× 放大下能看到锯齿缺口。整条链已经走完：

1. `GLDX_NO_SECONDARY=1` 下缺口一模一样 ⇒ 不是解算。
2. 整体关剔除（`glDisable(GL_CULL_FACE)`，1938 字节变化）缺口消失——但把整圈裙子的**未受光内壁**
   露出来，读数比缺口更差，所以这不是解法。
3. 于是查"资产自己有没有声明双面"：加载器**一直在解析** `AI_MATKEY_TWOSIDED`
   （`SkinnedMaterial::twoSided`）却**从没人读过**。现在补了 `RiggedModel::isTwoSided()` 和
   demo 的 `--on twoside`（逐材质、默认关）。实测这份 PMX **一个材质都没标双面**——
   `--on twoside` 与默认**逐字节相同**（0 字节差），所以它对这具资产是个空开关。

结论：缺口是单面薄片的背面被剔除，这份资产没给可用信号。要修只能重刷权重（给它厚度）或
改几何。默认保持现状，因为强行不剔除是净亏。`--on twoside` 留着是因为它是唯一能表达
"这块面是单面薄片"的资产信号，换一份老实标了它的资产就直接可用。

## 3.（已完成）`crouch` 与 `crouchwalk` 不是同一帧

`--on report` 那一行是这条不变量的**测量**：

```
[anim-rigid] crouch == crouchwalk@phase 0.00: t=0 pose 2.359491e-01, drawn palette 3.689807e-01 (both must be 0)
```

上一版一直打印这个失败值（0.236），只是没人看。根因：`Setup.cpp` 里 `crouchSource` 用
`character.tune`（**站立**步态）构造，而 `crouchWalkSource` 用的是收窄过的 `crouchTune`
（`hipSwing*0.54`、`kneeSwingFlex*0.55` 等）。两个源用的根本不是同一条曲线。
改法：把 `crouchTune` 提出到块外，`ProceduralCrouch` 也用它构造（它内部只会再把 `crouch` 拉到 1）。
现在那一行是 `t=0 pose 0.000000e+00, drawn palette 0.000000e+00`（呼吸项在 t=0 恰好为 0），
`worst over a breath 3.000e-02` 是刻意保留的呼吸幅度。

顺带：`switch (view.requestAction)` 里漏了 `CrouchWalk`（demo 唯一的 `-Wswitch` 告警），补上并让它
和 `Crouch` 共用一组握持值——蹲下和蹲走是同一个动作的两个时刻，手指不该跳。

## 4. 还没做的（按风险排序）

1. **`window.OnFrame` 的 lambda（约 990 行，main.cpp:487 起）** 仍没抽出去。它捕获了 ~15 个
   局部量（`load` `character` `view` `renderer` `sprite` `font` `white` `camera` `hudRaw`
   `pressAt/pressKey/press2At/press2Key` `traceAction` `modelPath` `flags` `firstFrame`）。
   抽成函数需要一个打包这些的上下文结构体。**这是最大的一块，也是最容易改坏的**。
2. **约 27 个 `GLDX_*` 探针块（约 620 行）** 仍混在帧循环里。整块搬到 `Probes.h/.cpp` 需要先确认
   每个探针只用 `(Character, View)` 就能跑——多数已经自己算世界矩阵，所以可行，但要一个个核对。
3. 上面第 2 节那个"单面薄片花边"的锯齿（资产侧）。

## 5. 重构/调试时踩过的坑（会再踩一次）

* **头文件里不许 include 引擎头**：`gldx` 是 C++ 模块，include 它的头会报
  `declaration ... follows declaration in module gldx`。照 `glass_water/Scan.h` 的做法：
  头文件只写注释 "NOTE: names gldx types, so include AFTER `import gldx;`"。
* **头文件里的自由函数必须 `inline`**，否则每个包含它的 TU 都生成定义，链接期 `duplicate symbol`。
* **搬代码块时 `namespace {` / `}` 会跟着搬**，导致命名空间提前闭合、后面的类型跑到全局作用域，
  报 "did you mean `anim::X`" 这种看起来毫无道理的错。搬完立刻用括号配平扫一遍。
* **`--on report` 是 `ReportGait`**，它**会写** `character.runGroundOffset/runGroundReady`
  （跑一次取整周期最低点），所以签名是 `Character&` 不是 `const Character&`。
* **判定"链解算有问题"最快的开关是 `GLDX_NO_SECONDARY=1`**：它把整条二次运动关掉，
  剩下的就是姿态/蒙皮本身。上一版把一个解算 bug 误判成权重 bug，就是没先跑这个对照。

## 6. 已知但不用管的

* `GLDX_PEN` 的穿透数值是保守下界（骨骼中心到探针、固定半径 0.11 m）。九种动作各扫 5 个相位的复测：
  idle/crouch/swim 0.000、walk 0.047、crouchwalk 0.024、attack 0.048、jump 0.056、run 0.093、**sit 0.081**（
  与上一版同量级，说明这次改动没有把腿推进裙子里）。目视（正面 + yaw 0.6/1.05/1.4/2.4）没有找到实际切口。
  **注意 `GLDX_PEN` 读的是 `character.pose`，看不到座姿的 skirtFlare**——那个 hack 改的是 `palette`，
  所以扫 skirtFlare 时 pen 恒为 0.0713、必须看图。
  要更硬的判据就得改成布面**表面**到探针的距离，那需要把网格求出来。
* **座姿的裙摆是一块平板，这是有意的取舍，别再调**。`skirtFlare` 把裙片沿半径推出去，现在取 0.20 m
  （而裙锥半径本身只有约 0.25 m），所以坐下去裙摆像一个圆盘。扫过 0.20 / 0.12 / 0.06 / 0.00：
  0.12 以下就开始在大腿处露白（前片被膝盖顶穿），0.00 时腿直接穿出裙面。也就是说现在的值
  是"最不容易穿模"的那个点，压小只是把"裙子像硬圈"换成"膝盖穿出来"。
  默认值已由 `GLDX_SKIRTFLARE` 之外没有旋钮，改之前先按上面的四个值各拍一张。
* 相机拖拽只验证到 "`Drag → Apply → 渲染` 是通的"（合成拖拽改变 625996 像素），
  真实鼠标事件那一层在这台机器上验不了（无 Quartz / cliclick 类工具）。已加兜底：
  每帧用 `MouseIsDown` 回查物理按键。

## 7. 环境变量速查（都是在 `src/demo/anim-rigid/*` 或 `SecondaryMotion.cpp` 里 `getenv` 的）

```
GLDX_SNAPSHOT / GLDX_SNAPSHOT_AT / GLDX_PEN / GLDX_SKIRTTRACE / GLDX_SETTLEHASH
GLDX_NO_SECONDARY / GLDX_NO_RESET_SETTLE / GLDX_NOBREATHE / GLDX_NO_MODEL_CACHE
GLDX_GAITDBG / GLDX_INPUT / GLDX_SIM_DRAG / GLDX_PRESS_AT+KEY / GLDX_PRESS_AT2+KEY2
GLDX_SKIRTLIFT / GLDX_SKIRTFLARE / GLDX_SKIRTSYM / GLDX_GAITLIFT / GLDX_GAITLOW
GLDX_CURL / GLDX_TCURL / GLDX_CURLAXIS / GLDX_ARM / GLDX_ARMPHASE / GLDX_ELBOWDIR
GLDX_BONES / GLDX_CHAINS / GLDX_PROPS / GLDX_AXES / GLDX_MAT / GLDX_DUMP_JOINTS ...
GLDX_SKIRTDRIVE   # 每片裙片的 lat/fwd/sw/gx/gz 与弹簧角，查"哪一片在乱动"
// 另：`--on twoside` 逐材质按资产的双面标记关剔除（默认关，对这份资产与默认逐字节相同）
```
