# 交接：anim-rigid demo 的未解决问题

写给下一个 agent。仓库 `/Users/greenhandzdl/Documents/GitHub/glad-cmake-configure`，
先读 `AGENTS.md`（那里的约束优先于本文）。本文只讲**还没解决**的事，以及踩过的坑。

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

## 1.（优先级最高）冻结快照不可复现

**症状**：同一个二进制、同样的 argv，连跑两次得到不同像素。

```bash
for i in 1 2; do GLDX_SNAPSHOT=/tmp/det$i.bmp GLDX_SNAPSHOT_AT=9.0 ./output/anim-rigid \
  --off hud --freeze-at 1.0 --quit-after 12 --action idle \
  --distance 2.2 --target-y 0.85 --yaw 0.0 --pitch 0.12 --fov 40 >/dev/null 2>&1; done
md5 -q /tmp/det1.bmp /tmp/det2.bmp      # 实测两个不同的 md5
```

**已定位到的边界**（这些是可靠结论，别重复做）：

- 抖动在**二次运动**里：`GLDX_NO_SECONDARY=1` 时连跑三次逐字节一致。
- 抖动幅度随动作而变：walk 实测两次之间 1697~14803 个像素不同，最大通道差 202
  （2.2 m 视距下 1 mm ≈ 1 px，所以 1e-3 量级的姿态差就够让上万条边缘像素不同）。
- 差异区在角色/裙摆那一带（例：x 942–1594、y 420–756），不是地面网格。

**已排除**（都做过 A/B，别再试）：

- 「结算前把弹簧状态 Reset 到静止」——加了 `GLDX_NO_RESET_SETTLE=1` 开关对比，两种都抖。
- 「呼吸微动时钟改成由动画时钟驱动」——`ChainSolver::SetClock()` 已加，仍是动画时间常数，
  照样抖。
- 「结算循环复用上一活动帧的探针/摆腿信号」——已改为按冻结姿态重算
  (`BuildClothProbes` / `ComputeGaitSwing` 两个调用点)，照样抖。

**还活着的诊断钩子**（可以直接用，也可以最后清掉）：

- `GLDX_SETTLEHASH=1` → 打印 `[settlehash] <double>`，是各链角度/角速度之和
  （`ChainSolver::StateFingerprint()`）。实测三次：19.4458 / 19.3584 / 19.3600，
  确认**结算输出本身**就不同。
- `GLDX_SKIRTTRACE=1` → 逐帧打印裙摆尖端相对骨盆的位置。

**下一步建议**：不要继续猜。直接给结算的**输入**打指纹（冻结姿态矩阵、探针坐标、
摆腿信号、`view.clock`、链数量），和 `[settlehash]` 的**输出**并排打印三次。
输入一致而输出不同 ⇒ 解算器自身有未初始化/顺序相关的量（检查 `ChainSolver` 里所有
成员是否都参与初始化，尤其是 `prevProbePos_`、`probeVel` 这类差分历史）。
输入不同 ⇒ 顺着那一项查帧数依赖。

## 2. 剩余可拆的模块（`main.cpp` 还有 1586 行）

已完成：`View.h` / `Scene.h` / `Character.h` / `ClothProbes.h` / `SkinPass.h` /
`HudPass.h` / `Setup.h+.cpp` / `Report.h+.cpp` / `AssetBorrow.h+.cpp`，
`main.cpp` 从 4097 降到 1586。

还剩下的两块，按风险排序：

1. **`window.OnFrame` 的 lambda（约 987 行，main.cpp:487–1583）**
   它捕获了 ~15 个局部量（`load` `character` `view` `renderer` `sprite` `font` `white`
   `camera` `hudRaw` `pressAt/pressKey/press2At/press2Key` `traceAction` `modelPath`
   `flags` `firstFrame`）。抽成函数需要一个把这些打包的上下文结构体，或者把若干
   `char&/const char*` 参数摊平。**这是最大的一块，也是最容易改坏的**。
2. **约 27 个 `GLDX_*` 探针块（约 620 行）**
   它们混在帧循环里，和功能代码交织（手指弯曲、捩骨、接地解算都不是探针）。整块搬到
   `Probes.h/.cpp` 需要先确定每个探针只用 `(Character, View)` 就能跑——多数已经自己
   算世界矩阵，所以可行，但要一个个核对。

## 3. 重构时踩过的坑（会再踩一次）

- **头文件里不许 include 引擎头**：`gldx` 是 C++ 模块，include 它的头会报
  `declaration ... follows declaration in module gldx`。照 `glass_water/Scan.h` 的做法：
  头文件只写注释 “NOTE: names gldx types, so include AFTER `import gldx;`”。
- **头文件里的自由函数必须 `inline`**，否则每个包含它的 TU 都生成定义，
  链接期 `duplicate symbol`。
- **搬代码块时 `namespace {` / `}` 会跟着搬**，导致命名空间提前闭合、后面的类型
  跑到全局作用域，报 "did you mean `anim::X`" 这种看起来毫无道理的错。
  搬完立刻用括号配平扫一遍。
- **`--on report` 是 `ReportGait`**，它**会写** `character.runGroundOffset/runGroundReady`
  （跑一次取整周期最低点），所以签名是 `Character&` 不是 `const Character&`。

## 4. 已知但可能不用管的

- `GLDX_PEN` 的穿透数值是保守下界（骨骼中心到探针、固定半径 0.11 m），
  walk 最差 0.047、run 0.091；但多角度多时刻目视扫描（7 个动作约 60 帧）没有实际切口。
  要更硬的判据就得改成布面**表面**到探针的距离，那需要把网格求出来。
- 相机拖拽只验证到 “`Drag → Apply → 渲染` 是通的”（合成拖拽改变 625996 像素），
  真实鼠标事件那一层在这台机器上验不了（无 Quartz / cliclick 类工具）。
  已加兜底：每帧用 `MouseIsDown` 回查物理按键，按下/松开事件丢了也能转/停。

## 5. 环境变量速查（都是在 `src/demo/anim-rigid/*` 里 `getenv` 的）

```
GLDX_SNAPSHOT / GLDX_SNAPSHOT_AT / GLDX_PEN / GLDX_SKIRTTRACE / GLDX_SETTLEHASH
GLDX_NO_SECONDARY / GLDX_NO_RESET_SETTLE / GLDX_NOBREATHE / GLDX_NO_MODEL_CACHE
GLDX_GAITDBG / GLDX_INPUT / GLDX_SIM_DRAG / GLDX_PRESS_AT+KEY / GLDX_PRESS_AT2+KEY2
GLDX_SKIRTLIFT / GLDX_SKIRTFLARE / GLDX_SKIRTSYM / GLDX_GAITLIFT / GLDX_GAITLOW
GLDX_CURL / GLDX_TCURL / GLDX_CURLAXIS / GLDX_ARM / GLDX_ARMPHASE / GLDX_ELBOWDIR
GLDX_BONES / GLDX_CHAINS / GLDX_PROPS / GLDX_AXES / GLDX_MAT / GLDX_DUMP_JOINTS ...
```
