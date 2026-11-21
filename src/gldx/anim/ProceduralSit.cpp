module;

#include "gldx/gmf.hpp"

module gldx;

// 类型从模块拿；不文本 include 自己的头（会报 redefinition）。

namespace gldx {
namespace {

constexpr float kPi = 3.14159265358979323846f;

float Ease01(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

} // namespace

ProceduralSit::ProceduralSit(const Skeleton& skeleton, const RigProfile& rig,
                             const std::vector<glm::mat4>& restWorld, const SitTuning& tuning)
    : skeleton_(skeleton), rig_(rig), restWorld_(restWorld), tuning_(tuning) {}

void ProceduralSit::Sample(float t, std::vector<glm::mat4>& outPose) const {
    ResetToRest(skeleton_, outPose);

    auto rotate = [&](int bone, const glm::vec3& worldAxis, float radians) {
        if (radians == 0.0f) return;
        RotateBone(outPose, restWorld_, bone, worldAxis, radians);
    };

    const SitTuning& tune = tuning_;
    if (std::getenv("GLDX_SITDBG"))
        std::fprintf(stderr, "[sitdbg] Sample t=%.2f hip=%.3f knee=%.3f out=%.3f thigh=%.1fdeg\n",
                     (double)t, (double)tune.hip, (double)tune.knee, (double)tune.out,
                     (double)(tune.hip * 180.0f / 3.14159265f));

    // ---- 呼吸相位（循环） ----
    const float period = std::max(tune.breathSeconds, 1e-3f);
    float folded = std::fmod(t, period);
    if (folded < 0.0f) folded += period;
    const float breathe = std::sin((folded / period) * 2.0f * kPi);

    // ---- 骨盆高度：不在这里写 ----
    //
    // 坐姿的骨盆高度**交给地面解算**：调用方给 sit 换一个把**臀**当接触点的解算器
    // （`GroundContact::AddBodyContact`），于是"最低的实体触地"就把骨盆放到了地上。
    //
    // 这里曾经有一个 `seatDrop` 平移。它有两处错，都值得记下来：
    //   1. 单位：`outPose` 是**骨骼单位**（这具资产 x0.01），而 seatDrop 写的是米，
    //      于是 0.87 只剩 8.7 mm，读图时看起来"完全没生效"。
    //   2. 冗余：既然解算已经把臀钉到地板，再平移一次只会把两者打架。

    // ---- 躯干 ----
    // 坐地时上身只略前倾：手撑在身后，重心本来就稳，继续前倾会读成"要站起来"。
    rotate(rig_.spine, rig_.side, tune.spineLean);
    rotate(rig_.chest, rig_.side, tune.spineLean * 0.35f);
    rotate(rig_.head, rig_.side, -tune.headLift);
    // 极缓的侧倾与呼吸：幅度小到看不出是动画，但去掉它整个画面就死了。
    rotate(rig_.spine, rig_.forward, tune.spineTilt + tune.breathSway * breathe);
    rotate(rig_.chest, rig_.side, tune.breathSpine * breathe);

    // ---- 腿：鸭子坐（W-sitting），**不是一字开** ----
    //
    // 这一处曾经把 `legOut` 全加在绕 `up` 的旋转上（0.78 rad），结果整条腿被平摊成
    // 一条横线——用户看到的"开叉"。一字开和鸭子坐的差别不在外展角度大小，而在
    // **哪一段骨头被转开**：
    //
    //   一字开：大腿**水平**向两侧摊开，小腿继续沿同一方向。
    //   鸭子坐：大腿朝前偏下（几乎坐在上面），**小腿**从膝关节折回、贴着大腿的
    //           外侧向后。所以外展主要发生在膝关节，大腿只开一点点。
    //
    // ---- 参考图读法（W-sit / 割坐）----
    //
    // 两张参考图给出的是同一个姿势：臀部落地，**大腿向两侧摊开**（几乎水平贴地），
    // 小腿从膝盖**折回**、贴着大腿外侧朝后，大腿与小腿约成 90°；脚背朝上贴地、
    // 脚尖朝外。从正面看两腿围成一个 W，从侧面看膝盖在髋的前外侧、脚在身后。
    //
    // 实现上的关键：**外展要发生在大腿上**（绕 `forward` 轴，让腿从身体中线向侧面
    // 摊开），而不是绕 `up` 轴转——后者是绕腿自身长轴的"拧"，读不出摊开。大腿摊开
    // 之后，膝盖的屈曲方向自然变成"向后"，小腿就落到身体两侧。
    // ---- 腿：FK，每根骨头绕一个固定轴转一次 ----
    //
    // 子骨自动继承父骨的旋转（这是骨架层级的定义），所以下面三行就是完整的腿：
    //   1) 大腿绕 forward 外展 -> 膝盖向侧面打开（两膝并拢时这一步很小）
    //   2) 大腿绕 side 前屈   -> 从竖直倒到接近水平，人坐下
    //   3) 小腿绕 side 折回   -> 相对大腿成直角，落在臀侧
    //
    // 顺序有讲究：先外展再前屈，前屈才是"倒下去"的那一下；反过来的话外展是绕一根
    // 已经水平的轴转，会变成绕腿自身长轴的拧。
    for (int s = 0; s < kRigSides; ++s) {
        const float sideSign = (s == 0) ? 1.0f : -1.0f;
        const float asym = tune.legAsymmetry * sideSign;

        // ---- 大腿：先前屈到水平，再**绕竖直轴向外摊开** ----
        //
        // 顺序与轴的组合是这一处的全部难点，错一次就是另一种坐姿：
        //   绕 forward 外展（我之前写的）在腿还竖直时只是让它**向侧面倾斜**，
        //   俯视看仍是向前伸的两条腿——用户截图就是这个。
        //   先把腿前屈到水平（绕 side），再绕 **up** 转开，腿才会像扇子一样
        //   在**水平面**里摊到两侧，这才是鸭子坐的 W。
        rotate(rig_.legUp[s], rig_.side, -(tune.hip + asym));   // 前屈到水平
        rotate(rig_.legUp[s], rig_.forward, -tune.out * sideSign); // 水平面内向外摊开

        // 膝：腿已经水平摊开，绕 **up** 继续折回 -> 小腿在水平面里收回来，
        // 落在大腿内侧/尾侧。绕 side 折会让小腿垂到地下（试过）。
        rotate(rig_.legLow[s], rig_.forward, -tune.knee * sideSign);

        rotate(rig_.foot[s], rig_.side, tune.ankle);                   // 踝
    }

    // ---- 手臂：撑在身后 ----
    // T-pose 先降到 A-pose，再往后摆。绕 side 的**正**向是往后。
    rotate(rig_.armUp[kRigLeft], rig_.forward, -tune.armDown);
    rotate(rig_.armUp[kRigRight], rig_.forward, +tune.armDown);
    for (int s = 0; s < kRigSides; ++s) {
        rotate(rig_.armUp[s], rig_.side, tune.armBack);
        rotate(rig_.armLow[s], rig_.side, -tune.elbowBend);
        rotate(rig_.armLow[s], rig_.up, tune.armOut * (s == 0 ? 1.0f : -1.0f));
        rotate(rig_.hand[s], rig_.side, tune.wristBack);
    }
    // 头随呼吸轻轻抬落——眼睛追踪的是脸，不是胸口。
    rotate(rig_.head, rig_.side, -tune.breathHead * breathe * 0.5f);
}

} // namespace gldx
