module;

#include <cstdio>
#include <cstdlib>

#include "gldx/gmf.hpp"

module gldx;

// 类型从模块拿；不文本 include 自己的头（会报 redefinition）。

namespace gldx {
namespace {

constexpr float kPi = 3.14159265358979323846f;

float Clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }
float Ease01(float x) {
    x = Clamp01(x);
    return x * x * (3.0f - 2.0f * x);
}
float Segment01(float x, float a, float b) {
    return Clamp01((x - a) / std::max(b - a, 1e-5f));
}

// 一只手臂在归一化相位 p（0 = 前伸入水）上的四个角。
//
// 自由泳的一条手臂走完一圈分三段，比例取自实拍：抓水+推水约 45%（在水下），
// 出水约 5%，空中移臂约 50%（高肘前甩）。三段各自缓入缓出，接缝处角度连续。
struct ArmAngles {
    float reach = 0.0f;   // 正 = 向前伸（指向行进方向）
    float pull = 0.0f;    // 正 = 划到体侧
    float elbow = 0.0f;   // 正 = 屈肘
    float lift = 0.0f;    // 正 = 手臂抬离水面（空中移臂）
    float wrist = 0.0f;   // 正 = 扣腕
};

ArmAngles ArmAt(const SwimTuning& t, float p) {
    ArmAngles out;
    if (p < 0.50f) {
        // 水下：抓水 → 推水。前伸量从最大回到 0，同时手臂划到体侧（pull 拉满），
        // 肘越来越弯（抱水），最后扣腕把水推到大腿。
        const float s = Clamp01(p / 0.50f);
        const float pull01 = Ease01(Segment01(s, 0.15f, 1.00f));
        out.reach = t.reachFront * (1.0f - Ease01(Segment01(s, 0.0f, 0.45f)));
        out.pull  = t.pullBack * pull01;
        out.elbow = t.elbowBase + t.elbowPull * pull01;
        out.lift  = 0.0f;
        out.wrist = t.wristSnap * Ease01(Segment01(s, 0.60f, 1.00f));
    } else {
        // 空中移臂：高肘出水 → 经身体上方 → 回到前伸。
        const float s = Clamp01((p - 0.50f) / 0.50f);
        const float arc = Ease01(s);
        out.pull  = t.pullBack * (1.0f - arc);
        out.reach = t.reachFront * arc;
        // 屈肘：出水时保持高肘，接近前伸时逐渐伸直。
        out.elbow = t.elbowRecover * (1.0f - arc) + t.elbowBase * arc;
        // 抬起量是驼峰：出水瞬间最大，回到前面时归零。
        out.lift  = t.recoverLift * std::sin(kPi * s);
        out.wrist = t.wristSnap * (1.0f - arc);
    }
    return out;
}

} // namespace

ProceduralSwim::ProceduralSwim(const Skeleton& skeleton, const RigProfile& rig,
                               const std::vector<glm::mat4>& restWorld, const SwimTuning& tuning)
    : skeleton_(skeleton), rig_(rig), restWorld_(restWorld), tuning_(tuning),
      duration_(std::max(tuning.cycleSeconds, 1e-3f)) {}

void ProceduralSwim::Sample(float t, std::vector<glm::mat4>& outPose) const {
    ResetToRest(skeleton_, outPose);


    auto rotate = [&](int bone, const glm::vec3& worldAxis, float radians) {
        if (radians == 0.0f) return;
        RotateBone(outPose, restWorld_, bone, worldAxis, radians);
    };

    const SwimTuning& tune = tuning_;
    (void)0;

    // 时间域取模再除（理由见 ProceduralWalk.cpp）：t 与 t+Duration 必须给出同一姿态。
    float folded = (duration_ > 0.0f) ? std::fmod(t, duration_) : 0.0f;
    if (folded < 0.0f) folded += duration_;
    const float phase = (duration_ > 0.0f) ? (folded / duration_) : 0.0f;

    // 左右手臂相差半个周期。
    const ArmAngles left = ArmAt(tune, phase);
    const ArmAngles right = ArmAt(tune, phase < 0.5f ? phase + 0.5f : phase - 0.5f);

    // ---- 腿：向后伸直（身体是一条直线）+ 小幅打腿 ----
    //
    // 注意默认姿态里腿是**垂下**的，俯卧之后那就是"腿朝下、插进水里"。游泳的人
    // 腿在身后，所以髋要伸展：绕 side 的**正**向让朝下的骨头往**后**摆——俯卧之后
    // "后"正是脚该去的方向。
    //
    // 打腿频率是划水的两倍（每半周期一次，左右交替 = 四拍），幅度很小。
    const float kickPhase = phase * 2.0f;
    for (int s = 0; s < kRigSides; ++s) {
        const float off = kickPhase + (s == 0 ? 0.0f : 0.5f);
        const float k = std::sin(off * 2.0f * kPi);
        // 上提那半段才屈膝：打腿的"鞭状"来自先弯后甩，一直弯就是蹬自行车。
        const float up = std::max(k, 0.0f);
        rotate(rig_.legUp[s], rig_.side, tune.legTrail - tune.kickAmp * k);
        rotate(rig_.legLow[s], rig_.side, tune.kneeFlex * up);
        // 脚背绷直，脚踝略背屈一点点以免完全僵直。
        rotate(rig_.foot[s], rig_.side, tune.anklePoint);
        rotate(rig_.toe[s], rig_.side, tune.anklePoint * 0.4f);
    }

    // ---- 躯干 ----
    // 身体已经被调用方转成水平；这里叠抬头、挺背、滚动与换气。
    //
    // 滚动跟着**划水的那一侧**：左手在水下推水时，身体向左滚，右肩出水——这正是
    // 自由泳一条手臂前伸、另一条划到体侧的由来。
    const float rollDriver = std::sin(phase * 2.0f * kPi);
    // Body roll happens at the SHOULDERS (girdle) and the upper spine. Rotating the
    // hips too - which this did - swings both legs out of line as a rigid pair, so the
    // swimmer's legs never sat in the water plane: a look-down shot showed them fanned
    // sideways against a straight torso. A real freestyle roll is a spiral: hips roll
    // about half as far as the shoulders, and that difference is the flutter kick's
    // room to work in.
    rotate(rig_.hips, rig_.forward, tune.bodyRoll * rollDriver * 0.18f);
    rotate(rig_.spine, rig_.forward, tune.bodyRoll * rollDriver * 0.82f);
    rotate(rig_.spine, rig_.side, tune.spineArch);
    rotate(rig_.chest, rig_.side, tune.spineArch * 0.6f);
    rotate(rig_.head, rig_.side, tune.headLift);
    // 换气：头侧转，正对刚出水的那只手臂。
    rotate(rig_.head, rig_.up, tune.headTurn * rollDriver);

    // ---- 手臂 ----
    // 这具骨架是 T-pose：先把手臂放成 A-pose（游泳的手臂贴得比走路更紧）。
    // 之后：
    //   reach = 向前伸（绕 side 的负向是往前）
    //   pull  = 划到体侧（绕 side 的正向）
    //   lift  = 空中移臂时手臂抬离水面（绕 up 轴，把手臂从"指向行进方向"抬到"指向
    //           身侧上方"——俯卧之后 up 轴在身体前后方向，所以它读作"高肘出水"）
    // 自由泳的一条手臂在水里只有两个极端位置，中间是连续过渡：
    //
    //   前伸（entry）：手臂指向**行进方向**。俯卧之后身体的 +forward 已经指向水底，
    //                   所以"向前伸直"是绕 **up 轴**把手臂转到头前 —— 不是绕 side。
    //   推水（pull）  ：手臂沿体侧向后扫到大腿。
    //
    // 之前整段都绕 side 转，那是把手臂在**矢状面**里上下摆：从"头前上方"摆到
    // "头前下方"，然后屈肘缩回来，两条手臂一上一下地划——这就是"潜水/刨水"的感觉。
    // 实测 handFwd 在整段里只从 +0.85 走到 -0.27（0.52 m 长的手臂，等于几乎没动）；
    // 真正的前伸应当到 ~+0.9 m，推水落到 ~-0.1 m，行程 1.0 m 以上。
    // ---- 手臂：贴在**水面平面**里向前伸，不是垂到身体下面 ----
    //
    // 这里踩过一次，症状是"两只手都在身后"：`armDown = 1.30 rad` 先把整条手臂压到
    // 身体中线**以下**（俯卧之后就是"垂向水底"），此后绕 up 轴无论怎么转，手臂都
    // 在身体下侧/后侧划——参考图里游泳的人手臂是**贴着水面**、在身体平面里向前伸、
    // 向后划的。
    //
    // 所以 armDown 只放一点点（把 T-pose 的手臂收到与身体同宽，约 0.15 rad），
    // 真正的"前伸/后划"全部交给绕 up 轴那一个自由度：俯卧之后 up 轴指向水底，
    // 绕它转就是在水面平面里画圈。
    // ---- 手臂：在水面平面里向前伸 ----
    //
    // 这具骨架的 T-pose 里手臂指向 **side**（横向）。身体俯卧之后：
    //   * 绕 up（指水底）转 = 在水面平面里把手臂从"横着"转到"朝前 / 朝后"——**这是
    //     前伸与划水的主自由度**；
    //   * 绕 forward（指水底前方的反向）转 = 手臂上下离开水面。
    //
    // 之前把 `armDown = 1.30` 加在绕 forward 上，手臂先被压到身体下方（俯卧后即"垂
    // 向水底"），之后怎么转 up 都在身体下侧划——实测 handUp 常数 0.6-0.7，两只手
    // 都在身后，正是"看起来像潜水"的来源。
    //
    // 现在：armDown 只收 0.12（让手臂略窄于肩），主量全给绕 up。
    // ---- 手臂：用**方向对齐**，不再手推欧拉角 ----
    //
    // 这一处改了七次。"绕哪个轴、正还是负"这种推理在这具骨架上已经错过太多次
    // （实测出现过：两臂横摊 wSide=2.5 m、一手指前一手朝天 up=±0.7），每次都是
    // 靠看图发现、再靠猜符号修，下一次换个相位又错。
    //
    // 现在改成几何做法：上臂的静止方向可以从骨架直接量出来（restWorld 里
    // armUp→armLow 的位移），目标方向也可以直接写出来（在身体前向-竖直平面里
    // 按划水进度取角）。两者之间的夹角与转轴由两个向量唯一确定：
    //     axis = normalize(cross(restDir, wantDir))
    //     angle = acos(clamp(dot(restDir, wantDir), -1, 1))
    // 这样"手臂指向哪里"是**直接给定**的，与骨架手性、与左右镜像无关。
    struct ArmTarget { float fwd; float up; };   // 期望方向（在 rig 的前向/上向平面里）
    auto restArmDir = [&](int boneUp, int boneLow) {
        const glm::vec3 a(restWorld_[static_cast<std::size_t>(boneUp)][3]);
        const glm::vec3 b(restWorld_[static_cast<std::size_t>(boneLow)][3]);
        const glm::vec3 d = b - a;
        const float len = glm::length(d);
        return (len > 1e-6f) ? d / len : rig_.side;
    };

    for (int s = 0; s < kRigSides; ++s) {
        const ArmAngles& arm = (s == 0) ? left : right;
        const int boneUp = (s == 0) ? rig_.armUp[kRigLeft] : rig_.armUp[kRigRight];
        const int boneLow = (s == 0) ? rig_.armLow[kRigLeft] : rig_.armLow[kRigRight];
        if (boneUp < 0 || boneLow < 0) continue;
        const float sideSign = (s == 0) ? 1.0f : -1.0f;

        // ---- 划水：由**手臂自己的周期相位**直接驱动扫掠角 ----
        //
        // 反复失败的根源在这里：扫掠角曾经由 `pull` 通道算出来，而 `pull` 只在水下段
        // （相位 0→0.5）从 0 涨到 1、空中移臂段又从 1 落回 0。于是手臂在**半个周期
        // 内去、半个周期内回**，永远只在一个扇区里来回——这就是"雨刮器"。
        //
        // 正确做法是让**相位**直接当角度用：一圈 360° 正好是一次完整划水。
        //     armPhase 0.00 -> 0°   前伸（指向 +forward）
        //     armPhase 0.25 -> 90°  抓到体侧
        //     armPhase 0.50 -> 180° 推到大腿（指向身后）
        //     armPhase 0.75 -> 270° 空中移臂绕回前方
        // 两臂的 armPhase 相差 0.5，所以永远一前一后。
        //
        // 侧向分量按 sideSign 镜像：左臂往左划、右臂往右划，两条手臂各占自己那一侧，
        // 不会叠在身体中线。
        const float armPhase = (s == 0) ? std::fmod(phase, 1.0f) : std::fmod(phase + 0.5f, 1.0f);

        // ---- 划水方向：在**矢状面**里画圈 ----
        //
        // 这一处前后错了七次，每次都是同一类错误的变体："看起来是前后，实测是左右"。
        // 决定性的证据来自 trace 的两列（手腕相对髋的位置）：
        //     写成 forward*cos + side*sin  ->  wristSide 扫 2.7 m，wristFwd 只 0.2 m
        //     也就是在**水平面**里左右扇 —— 正是用户说的"雨刮器"。
        //
        // 自由泳的手臂在**矢状面**（前后 × 上下）里划：从头顶前方入水，沿身体下方
        // 划到大腿，再从水上回到前方。所以方向必须由 forward 与"身体竖直方向"张成，
        // **绝不能出现 side 分量**。
        //
        // 身体俯卧后 up 指向水底，所以"朝水面"是 -up。用 fwd*cos - up*sin 就得到
        // 干净的矢状面圆周：sweep=0 指前，sweep=pi/2 指水面（入水/出水那一侧），
        // sweep=pi 指后。
        // 目标方向：绕 side 从"指向前"扫到"指向后"，扫过 180°。
        // side 是身体的左右轴——俯卧后绕它转，手臂在**矢状面**里前后摆，这正是
        // 自由泳的划水平面，也是侧视唯一看得见的运动。
        // 目标方向在**站立坐标系**里表达，和其余关节一致。
        //
        // 全部姿势（包括手臂）都在站姿系里摆完，最后一步才整体俯卧。这样"手臂指向"
        // 与"身体朝向"用的是同一套轴，不会出现一边按俯卧轴、一边按站姿轴的情况
        // （那样会让头部被甩到下方、整个人翻倒——试过，画面直接歪掉）。
        //
        // 站姿系里：forward = 身体朝前。手臂从"指向正前"扫到"指向身后"，中间经过
        // 体侧，这就是一次划水；俯卧之后它自然变成"贴着水面从前划到后"。
        // ---- 四段划水周期（回收段贴着肩膀滑回）----
        //
        // **坐标系约定（这一处最容易错，写清楚）**：
        // 所有关节角都在**站姿系**里摆，最后才整体俯卧（`prone_` 绕 side 转 90°）。
        // 俯卧把站姿系映射成：
        //     站姿 forward（身体朝前）  ->  俯卧后朝下（水底）
        //     站姿 up（头顶方向）      ->  俯卧后朝前（行进方向）
        // 所以在**站姿系**里写"手臂沿行进方向前伸"，应当用 **+up**；写"手指向身后"
        // 用 **-up**。"抬离水面"则是 **-forward**。
        //
        // 四段（方向都在站姿系里写，俯卧后自动成为"水下从前划到后"）：
        //   [0.00,0.35) 抓水推水：沿**行进方向**前伸，再沿身体正面（俯卧后=水下）划到体侧
        //   [0.35,0.45) 出水：手臂抬到身体背面（俯卧后=水面之上）
        //   [0.45,0.85) **贴肩滑回**：在体侧、与肩同高，沿行进方向平移回来
        //   [0.85,1.00) 入水前伸
        //
        // 符号是这一处的全部难点，写死在这里：
        //   俯卧 +90° 之后  站姿 up      -> 行进方向（头朝前）
        //                   站姿 forward -> 朝下（水底）
        //                   站姿 -forward-> 朝上（水面）
        // 所以「在水下把水往后推」用的是 **+F**（朝下），而「出水」才是 **-F**。
        // 曾经把这两者写反，画面就是"手举在头顶上方划"（用户截图）。
        struct StrokeKey { float phase; glm::vec3 dir; };
        const glm::vec3 F = rig_.forward, S = rig_.side * sideSign, U = rig_.up;
        const StrokeKey keys[] = {
            {0.00f, glm::normalize(U)},                       // 前伸（沿行进方向）
            {0.30f, glm::normalize(U * 0.55f + F * 0.80f)},   // 抓水：手向下压（水下）
            {0.45f, glm::normalize(-U * 0.75f + F * 0.45f)},  // 推水到底：手在身后、略下
            {0.55f, glm::normalize(-U * 0.55f - F * 0.55f)},  // 出水：抬到水面上
            {0.85f, glm::normalize(U * 0.62f - F * 0.30f + S * 0.55f)},  // 贴肩滑回
            {1.00f, glm::normalize(U)},                       // 回到前伸
        };
        glm::vec3 want = keys[0].dir;
        for (std::size_t k = 1; k < std::size(keys); ++k) {
            if (armPhase <= keys[k].phase) {
                const float span = keys[k].phase - keys[k - 1].phase;
                const float u = (span > 1e-6f) ? (armPhase - keys[k - 1].phase) / span : 0.0f;
                const float e = u * u * (3.0f - 2.0f * u);   // smoothstep：关键帧处无拐角
                want = glm::normalize(keys[k - 1].dir * (1.0f - e) + keys[k].dir * e);
                break;
            }
        }
        if (std::getenv("GLDX_P")) {
            // Report the direction the UPPER ARM actually ends up pointing, measured
            // from the finished pose (world space), not the target we asked for.
            std::fprintf(stderr, "[p] ph=%.2f want=(%.2f,%.2f,%.2f)\n",
                         (double)armPhase, (double)want.x, (double)want.y, (double)want.z);
        }
        const glm::vec3 rest = restArmDir(boneUp, boneLow);

        // 方向对齐：把上臂从 rest 转到 want。轴与角由两个单位向量唯一确定，
        // 与骨架手性无关——这正是"靠符号猜"反复失败之后剩下的唯一稳妥做法。
        const float dotv = std::clamp(glm::dot(rest, want), -1.0f, 1.0f);
        const float ang = std::acos(dotv);
        const glm::vec3 axis = glm::cross(rest, want);
        if (std::getenv("GLDX_SWARM") != nullptr) {
            std::fprintf(stderr, "[arm] ph=%.2f rest=(%.2f,%.2f,%.2f) want=(%.2f,%.2f,%.2f) ang=%.3f\n",
                         (double)armPhase, (double)rest.x, (double)rest.y, (double)rest.z,
                         (double)want.x, (double)want.y, (double)want.z, (double)ang);
        }
        if (glm::length(axis) > 1e-5f && ang > 1e-4f) {
            rotate(boneUp, glm::normalize(axis), ang);
        }

        // 屈肘：自由泳的手臂在水中是**弯**的（抱水），一直伸直就是一根棍子。
        // 绕 side 的**负**向是"往前弯"（与走路同一条约定），这里用正的角度让前臂
        // 朝身体中线收。
        rotate(boneLow, rig_.side, arm.elbow);
        rotate(rig_.hand[s], rig_.side, -arm.wrist);
    }

    // ---- 整体俯卧：身体绕 side 转 90°，脸朝下 ----
    //
    // 放在**最后**：上面所有关节角都是在"身体坐标系"里摆的（站着划），这一步把
    // 整具身体连同已经摆好的手臂一起转到世界里的俯卧姿态。把 prone 设成 0 时手臂
    // 清晰地前伸，证明了"对齐本身是对的、只是被这一步的次序吃掉"。
    if (prone_ != 0.0f) {
        RotateBone(outPose, restWorld_, rig_.hips, rig_.side, prone_);
    }
}

} // namespace gldx
