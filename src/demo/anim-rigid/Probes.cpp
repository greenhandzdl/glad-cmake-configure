// Module first, then the headers that name its types (same order as Report.cpp).
import gldx;

#include "Probes.h"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>

namespace anim {

namespace {

// World matrices for a pose, computed the way every consumer of these probes does.
// Factored out because all six of them need it and none of them should be the reason
// a second copy of the call exists.
std::vector<glm::mat4> WorldsOf(const Character& character) {
    std::vector<glm::mat4> w;
    character.model.skeleton().ComputeWorld(character.pose, character.model.Mat4(), w);
    return w;
}

}  // namespace

void ProbePenetration(const Character& character, const View& view) {
    // 穿模量化：对每个腿部/手部探针，找最近的裙摆骨骼，若距离小于探针半径
    // 就说明布面被穿过去了，穿透深度 = 半径 - 距离。扫相位取最大值。
    if (std::getenv("GLDX_PEN") == nullptr) return;
    (void)view;
    const std::vector<glm::mat4> pw = WorldsOf(character);
    const auto& skc2 = character.rig.chains[static_cast<int>(gldx::RigChainKind::Skirt)];
    float worst = 0.0f;
    if (skc2.count > 0) {
        std::vector<glm::vec3> pts;
        for (int sd = 0; sd < gldx::kRigSides; ++sd) {
            const int bu = character.rig.legUp[sd], bl = character.rig.legLow[sd];
            const int bf = character.rig.foot[sd];
            const int au = character.rig.armUp[sd], al = character.rig.armLow[sd];
            const int bh = character.rig.hand[sd];
            if (bu >= 0) pts.emplace_back(pw[(std::size_t)bu][3]);
            if (bu >= 0 && bl >= 0)
                pts.emplace_back(glm::mix(glm::vec3(pw[(std::size_t)bu][3]),
                                          glm::vec3(pw[(std::size_t)bl][3]), 0.55f));
            if (bl >= 0) pts.emplace_back(pw[(std::size_t)bl][3]);
            if (bf >= 0) pts.emplace_back(pw[(std::size_t)bf][3]);
            if (au >= 0 && al >= 0)
                pts.emplace_back(glm::mix(glm::vec3(pw[(std::size_t)au][3]),
                                          glm::vec3(pw[(std::size_t)al][3]), 0.6f));
            if (bh >= 0) pts.emplace_back(pw[(std::size_t)bh][3]);
        }
        for (const glm::vec3& p3 : pts) {
            float best = 1e9f;
            for (int i = 0; i < skc2.count; ++i) {
                const int b = skc2.bones[i];
                if (b < 0 || static_cast<std::size_t>(b) >= pw.size()) continue;
                best = std::min(best, glm::distance(p3, glm::vec3(pw[(std::size_t)b][3])));
            }
            const float radius = 0.11f;
            worst = std::max(worst, radius - best);
        }
    }
    // 手到同侧大腿轴线的距离：坐姿下"手插进大腿"就是这么量的。
    {
        float hd = 1e9f;
        for (int sd = 0; sd < gldx::kRigSides; ++sd) {
            const int bh = character.rig.hand[sd];
            const int bu = character.rig.legUp[sd];
            const int bl = character.rig.legLow[sd];
            if (bh < 0 || bu < 0 || bl < 0) continue;
            const glm::vec3 h(pw[(std::size_t)bh][3]);
            const glm::vec3 a(pw[(std::size_t)bu][3]);
            const glm::vec3 b(pw[(std::size_t)bl][3]);
            const glm::vec3 ab = b - a;
            const float t = glm::clamp(glm::dot(h - a, ab) / std::max(glm::dot(ab, ab), 1e-6f),
                                       0.0f, 1.0f);
            hd = std::min(hd, glm::distance(h, a + ab * t));
        }
        std::fprintf(stderr, "[pen] handToThighAxis=%.4f\n", (double)hd);
    }
    std::fprintf(stderr, "[pen] worst=%.4f\n", (double)worst);
}

void ProbeSkirtTrace(const Character& character, const View& view) {
    if (std::getenv("GLDX_SKIRTTRACE") == nullptr) return;
    // 逐帧记录裙摆尖端相对**骨盆**的位置。判据：减去身体自身的运动之后，振幅还剩下多少。
    // 若≈0，裙摆就是被刚性焊在髋上的——不管解算代码写得多热闹，画面都只会"跟着身体走"，
    // 看起来就是"从来没动过"。
    const std::vector<glm::mat4> tw = WorldsOf(character);
    const auto& sc2 = character.rig.chains[static_cast<int>(gldx::RigChainKind::Skirt)];
    const int hipB = character.rig.hips;
    if (sc2.count > 0 && hipB >= 0) {
        const glm::vec3 hipW(tw[(std::size_t)hipB][3]);
        std::fprintf(stderr, "[trace] t=%.3f", (double)view.clock);
        // 每片 3 节，取第 3 节（尖端）。
        for (int i = 2; i < sc2.count; i += 3) {
            const int b = sc2.bones[i];
            if (b < 0 || static_cast<std::size_t>(b) >= tw.size()) continue;
            const glm::vec3 p2(tw[(std::size_t)b][3]);
            std::fprintf(stderr, " p%d=(%+.3f,%+.3f,%+.3f)", i / 3,
                         (double)(p2.x - hipW.x), (double)(p2.y - hipW.y),
                         (double)(p2.z - hipW.z));
        }
        std::fprintf(stderr, "\n");
    }
}

void ProbeSkirtSymmetry(const Character& character) {
    if (std::getenv("GLDX_SKIRTSYM") == nullptr) return;
    const std::vector<glm::mat4> bw = WorldsOf(character);
    const auto& skc = character.rig.chains[static_cast<int>(gldx::RigChainKind::Skirt)];
    if (skc.count > 0) {
        const glm::vec3 root(bw[(std::size_t)skc.bones[0]][3]);
        float lo = 1e9f, hi = -1e9f, sum = 0.0f;
        int n = 0;
        for (int i = 0; i < skc.count; ++i) {
            const int b = skc.bones[i];
            if (b < 0 || static_cast<std::size_t>(b) >= bw.size()) continue;
            const float dx = glm::vec3(bw[(std::size_t)b][3]).x - root.x;
            lo = std::min(lo, dx);
            hi = std::max(hi, dx);
            sum += dx;
            ++n;
        }
        std::fprintf(stderr,
                     "[skirt] n=%d min=%+.3f max=%+.3f mean=%+.3f (asym %.3f)\n", n,
                     (double)lo, (double)hi, (double)(sum / std::max(n, 1)), (double)(hi + lo));
        for (int i = 0; i < skc.count; ++i) {
            const int b = skc.bones[i];
            if (b < 0 || static_cast<std::size_t>(b) >= bw.size()) continue;
            const glm::vec3 pd(bw[(std::size_t)b][3]);
            std::fprintf(stderr, "[skirtbone] %2d dx=%+.3f dy=%+.3f dz=%+.3f\n", i,
                         (double)(pd.x - root.x), (double)(pd.y - root.y),
                         (double)(pd.z - root.z));
        }
    }
}

void ProbeHairTip(const Character& character) {
    if (std::getenv("GLDX_BREATHE") == nullptr) return;
    static int bf = 0;
    if (bf++ % 20 == 0) {
        const std::vector<glm::mat4> bw = WorldsOf(character);
        const auto& hc = character.rig.chains[static_cast<int>(gldx::RigChainKind::Hair)];
        if (hc.count > 0) {
            const int b = hc.bones[hc.count - 1];
            if (b >= 0 && static_cast<std::size_t>(b) < bw.size()) {
                const glm::vec3 p3(bw[(std::size_t)b][3]);
                std::fprintf(stderr, "[breathe] frame=%d hairTip=(%.4f,%.4f,%.4f)\n", bf,
                             (double)p3.x, (double)p3.y, (double)p3.z);
            }
        }
    }
}

void ProbeTwistCheck(const Character& character) {
    if (std::getenv("GLDX_TWISTCHK") == nullptr) return;
    // `GLDX_TWISTCHK`：每片捩骨实际落点。左右加权到的骨不同，所以只有量同一个坐标系里的
    // 落点，才看得出两侧为什么漂移。
    static int shown = 0;
    if (shown++ < 2) {
        const std::vector<glm::mat4> tw = WorldsOf(character);
        for (int b : {96, 118, 104, 124}) {
            if (b < 0 || static_cast<std::size_t>(b) >= tw.size()) continue;
            const glm::vec3 w = glm::vec3(tw[(std::size_t)b][3]);
            std::fprintf(stderr, "[twistchk] %3d '%s' x=%+.3f y=%+.3f z=%+.3f\n", b,
                         character.model.skeleton().bones()[(std::size_t)b].name.c_str(),
                         (double)w.x, (double)w.y, (double)w.z);
        }
    }
}

void ProbeLimbsAndHands(const Character& character, const View& view) {
    if (std::getenv("GLDX_HANDDBG") == nullptr) return;
    // `character.palette` holds SKINNING matrices (world * inverseBind), not
    // bone world matrices - reading [3] out of it gave shoulder/elbow points
    // 2.6 m apart, which is how this mistake showed up. World matrices have
    // to come from ComputeWorld on the same pose.
    const std::vector<glm::mat4> dbgWorld = WorldsOf(character);
    auto P = [&](int b) {
        return b < 0 ? glm::vec3(0) : glm::vec3(dbgWorld[(std::size_t)b][3]);
    };
    const glm::vec3 hip = P(character.rig.hips);
    {
        const glm::vec3 kn = P(character.rig.legLow[0]);
        const glm::vec3 an = P(character.rig.foot[0]);
        const glm::vec3 hipJ = P(character.rig.legUp[0]);
        const float thigh = glm::length(kn - hipJ);
        const float shin = glm::length(an - kn);
        const float drop = hipJ.y - an.y;
        std::fprintf(stderr,
                     "[legdbg] hipJ=(%.3f,%.3f) knee=(%.3f,%.3f) ankle=(%.3f,%.3f) "
                     "thigh=%.3f shin=%.3f hipAboveAnkle=%.3f\n",
                     (double)hipJ.x, (double)hipJ.y, (double)kn.x, (double)kn.y,
                     (double)an.x, (double)an.y, (double)thigh, (double)shin, (double)drop);
    }
    for (int sd = 0; sd < gldx::kRigSides; ++sd) {
        const glm::vec3 ua = P(character.rig.armUp[sd]);
        const glm::vec3 la = P(character.rig.armLow[sd]);
        const glm::vec3 hd = P(character.rig.hand[sd]);
        std::fprintf(stderr,
                     "[handdbg] %s shoulder=(%.3f,%.3f,%.3f) elbow=(%.3f,%.3f,%.3f) "
                     "wrist=(%.3f,%.3f,%.3f) | from hip: dx=%+.3f dy=%+.3f dz=%+.3f\n",
                     sd == 0 ? "L" : "R", (double)ua.x, (double)ua.y, (double)ua.z,
                     (double)la.x, (double)la.y, (double)la.z, (double)hd.x, (double)hd.y,
                     (double)hd.z, (double)(hd.x - hip.x), (double)(hd.y - hip.y),
                     (double)(hd.z - hip.z));
    }
    (void)view;
}

}  // namespace anim
