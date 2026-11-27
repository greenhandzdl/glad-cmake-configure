// Diagnostics for the anim-rigid frame loop.
//
// Split out of main.cpp because those blocks were interleaved with the functional
// code they sit next to - finger curl, the MMD twist bones, the seated lift and the
// ground solve are NOT probes, and the ~250 lines of measurement that lived between
// them made the frame loop hard to read for no functional reason. Each function here
// is a pure reader of `(Character, View)`: it recomputes world matrices itself and
// only writes to stderr, so it cannot change the animation it measures.
//
// Every function keeps the same `GLDX_*` gate it had in main.cpp (the env check moved
// inside), which means the switch table in AGENTS.md section 7 is unchanged and no
// call site has to know which probes exist.
//
// NOTE: names gldx types, so include AFTER `import gldx;`.
#pragma once

#include "Character.h"

namespace anim {

// 穿模量化：每个腿部/手部探针到最近裙摆骨骼的距离，穿透 = 半径 - 距离；
// 另有手到同侧大腿轴线的距离（坐姿下"手插进大腿"就是这么量的）。
void ProbePenetration(const Character& character, const View& view);

// 裙摆尖端相对**骨盆**的逐帧位置。判据：扣掉身体自身运动之后振幅还剩多少——
// 若 ≈0，裙摆就是被刚性焊在髋上，画面只会"跟着身体走"。
void ProbeSkirtTrace(const Character& character, const View& view);

// 裙摆左右对称性：相对 chain[0] 根节点的 x 分布，mean 和 asym 两列。
void ProbeSkirtSymmetry(const Character& character);

// 头发末端微动（每 20 帧一次），看"这具身体还活着"的那条链有没有在动。
void ProbeHairTip(const Character& character);

// 捩骨（cuff-carrying twist bone）的世界坐标，让左右两片可以在同一帧里比较。
// 它们加权到不同的骨上（`左手捩` 挂在下腕、`右腕捱1` 挂在上臂），这正是两侧漂移的来源。
void ProbeTwistCheck(const Character& character);

// 腿骨与上臂/前臂/腕骨的世界坐标，以及大腿/小腿长度。`palette` 里存的是 SKINNING 矩阵
// （world * inverseBind），从里面读 [3] 会差出一个绑定矩阵——这个坑就记在这里。
void ProbeLimbsAndHands(const Character& character, const View& view);

}  // namespace anim
