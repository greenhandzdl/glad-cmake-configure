// Cloth probes and the gait signal that drives the skirt.
//
// These sit next to Character because they are pure functions of its rig: given world
// matrices they say where the cloth has to be pushed and which leg is swinging. Both
// the per-frame path and the frozen settle loop call them, which is the reason they are
// a module instead of a block inside the frame loop - the settle loop used to reuse the
// previous frame's probes, and that made the same snapshot irreproducible.
#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "Character.h"

namespace anim {

struct GaitSwing {
    float left = 0.0f;
    float right = 0.0f;
};

inline GaitSwing ComputeGaitSwing(const Character& character, const std::vector<glm::mat4>& world) {
    GaitSwing out;
    const glm::vec3 fwdRef(0.0f, 0.0f, -1.0f);
    for (int sd = 0; sd < gldx::kRigSides; ++sd) {
        const int bu = character.rig.legUp[sd];
        const int bl = character.rig.legLow[sd];
        if (bu < 0 || bl < 0) continue;
        const glm::vec3 hip(world[(std::size_t)bu][3]);
        const glm::vec3 knee(world[(std::size_t)bl][3]);
        const float sw = glm::dot(knee - hip, fwdRef);
        if (sd == 0) out.left = sw;
        else         out.right = sw;
    }
    return out;
}

/**
 * 布面探针：布料要和**哪些刚体**相撞，以及每个探针的半径。
 *
 * 抽成函数是因为它有两个调用点：每帧的主路径，和冻结时的结算循环。后者原先复用
 * 上一活动帧留下的探针集合——而那一帧的探针取决于"冻结前跑了多少帧"，于是同一个
 * `--freeze-at` 快照的结算结果每次都不一样（指纹实测 19.44 / 19.36 / 19.36）。
 * 结算必须自己按冻结姿态重建，才谈得上可复现。
 */
struct ClothProbes {
    std::vector<glm::vec3> legs;      // 髋/大腿中点/膝/踝，每侧四个
    std::vector<float>     legRadii;
    std::vector<glm::vec3> hands;     // 小臂末端 + 手
    std::vector<glm::vec3> armMids;   // 肩到肘的中点
    float                  legRadius = 0.0f;   // SetLegProbes 的统一半径
};

inline ClothProbes BuildClothProbes(const Character& character,
                             const std::vector<glm::mat4>& world) {
    ClothProbes out;
    const float rad = 0.125f * character.model.unitScale() * 10.0f;   // thigh radius
    out.legRadius = rad;
    for (int sd = 0; sd < gldx::kRigSides; ++sd) {
        const int bu = character.rig.legUp[sd];
        const int bl = character.rig.legLow[sd];
        const int bf = character.rig.foot[sd];
        if (bu >= 0) { out.legs.emplace_back(world[(std::size_t)bu][3]); out.legRadii.push_back(rad); }
        // 大腿中点也要一个探针：腿往前摆时膝盖跑到髋**前面**，小腿仍竖直，于是
        // "髋到膝"的中段正好扫过下摆前片。加点比加半径有效——半径调大会把整圈裙
        // 撑开，中点只补上缺的那一段。
        if (bu >= 0 && bl >= 0) {
            out.legs.emplace_back(glm::mix(glm::vec3(world[(std::size_t)bu][3]),
                                           glm::vec3(world[(std::size_t)bl][3]), 0.55f));
            out.legRadii.push_back(rad * 0.95f);
        }
        if (bl >= 0) { out.legs.emplace_back(world[(std::size_t)bl][3]); out.legRadii.push_back(rad * 0.90f); }
        if (bf >= 0) { out.legs.emplace_back(world[(std::size_t)bf][3]); out.legRadii.push_back(rad * 0.85f); }
        // 手与小臂：下摆在每个站立姿态里都和手同高，不推开就会一直相交。
        for (int b : {character.rig.armLow[sd], character.rig.hand[sd]}) {
            if (b >= 0) out.hands.emplace_back(world[(std::size_t)b][3]);
        }
        const int au2 = character.rig.armUp[sd];
        const int al2 = character.rig.armLow[sd];
        if (au2 >= 0 && al2 >= 0) {
            out.armMids.emplace_back(glm::mix(glm::vec3(world[(std::size_t)au2][3]),
                                              glm::vec3(world[(std::size_t)al2][3]), 0.6f));
        }
    }
    return out;
}

}  // namespace anim
