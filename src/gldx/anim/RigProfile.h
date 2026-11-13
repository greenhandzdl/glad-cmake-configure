#ifndef GLDX_ANIM_RIGPROFILE_H
#define GLDX_ANIM_RIGPROFILE_H

/**
 * @file RigProfile.h
 * @brief 骨骼语义分类：哪个骨头是髋 / 膝 / 腕 / 肩，以及哪边是上 / 侧 / 前。
 *
 * 纯 CPU（std + glm + gldx::Skeleton）：可在工作线程构建，不含 GL、不含 Assimp。用法是在
 * Stage A 里 classify 一次，把结果当普通值类型传下去。注意槽位存的是**数组下标**，所以换了
 * 骨架（重新加载资产、换了 `--model`）就必须重新 classify：旧索引在新骨架上通常仍然合法，
 * 只是指向了别的骨头，没有任何一处会报错，症状是"腿在甩胳膊"。
 *
 * 为什么需要这一层：骨骼名字是导出器/建模软件的自由文本，不是标准。同一份 Mixamo 资产在
 * Blender 里重导一次就可能从 `LeftUpLeg` 变成 `UpperLeg.L`，3ds Max Biped 又是 `Bip01 L Thigh`。
 * 步态代码真正关心的只有语义（"髋绕侧轴摆"），所以名字→语义的映射收在这里，调用方只读槽位。
 *
 * 三个轴是**量出来的，不是写死的**：up = 髋→头，side = 左大腿→右大腿（反向，即指向角色的
 * 左侧），forward = cross(side, up)。写死"Y 是上"在 Z-up 导出的资产上会让整个步态绕错轴，
 * 而且不会有任何报错——只是走路变成原地扭。量出来之后，(side, up, forward) 必然是一个右手
 * 正交三系，步态里所有"绕侧轴摆腿"的表述才有意义。
 *
 * 最关键的一条（也是最容易踩的）：**槽位查不到时这里不抛异常、不打印、不失败，只留 -1**。
 * 后续每次 `RotateBone()` 碰到 -1 都会默默 no-op，于是"角色僵住"看起来像动画代码写错了。
 * 这个仓库历史上就吃过一次：`hand` / `clavicle` 两级槽位漏绑，所有腕、肩旋转被静默吞掉，
 * 几轮 review 都没看出来，症状只是"手看着很僵"。所以每个消费方都应在启动时读一次
 * Missing() / BindingReport() 并打进日志（demo 的 `--on report` 就干这个），把静默降级
 * 变成可断言。
 *
 * TODO(待用户确认)：混元（Hunyuan）系骨骼的命名规则暂无可靠样本，别名表里刻意没有添加。
 * 凭印象加名字等于把"猜错"编码成"绑上了"，比漏绑更难查——漏绑至少会被 Missing() 点名。
 */

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "gldx/anim/Skeleton.h"
// 槽位枚举与侧别常量拆在 RigSlot.h：RigNaming.h（"一个族怎么叫这些槽位"）也要用它们，
// 放在这里会形成 RigNaming ⇄ RigProfile 的循环包含。见 RigSlot.h 文件头。
#include "gldx/anim/RigSlot.h"

namespace gldx {

class RigNaming;   // 前向声明：RigProfile 只在 Classify 的签名里用到它

/**
 * 一具骨架的语义分类结果。纯数据，可拷贝、可跨线程传（不含 GL、不含指针）。
 */
struct RigProfile {
    int hips   = -1;
    int spine  = -1;
    int spine1 = -1;   // 辅助槽位：躯干中段，见 kRigRequiredSlotCount
    int chest  = -1;
    int head   = -1;

    // 每条链两节 + 末端，下标用 kRigLeft / kRigRight。
    int legUp[kRigSides]     = {-1, -1};
    int legLow[kRigSides]    = {-1, -1};
    int foot[kRigSides]      = {-1, -1};
    int toe[kRigSides]       = {-1, -1};
    int armUp[kRigSides]     = {-1, -1};
    int armLow[kRigSides]    = {-1, -1};
    int hand[kRigSides]      = {-1, -1};   // 腕：手部网格主要被权重到的那个骨头
    int clavicle[kRigSides]  = {-1, -1};   // 锁骨/肩胛：只转几度，但少了它就"像个铰接木偶"

    // 量出来的正交三系（右手系）：forward = cross(side, up)。
    // 默认值只在一个轴都量不出来时才会留下（例如骨架根本没有髋或头），那种情况下
    // 步态不会走路，但至少不会算出 NaN——见 .cpp 里对退化分支的说明。
    glm::vec3 side{1.0f, 0.0f, 0.0f};     // 指向角色左侧
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    glm::vec3 forward{0.0f, 0.0f, 1.0f};  // 角色行走的方向

    // 解析时实际选中的命名族（指向静态字面量，不拥有）。见 namingFamily()。
    const char* namingFamily_ = "";

    // hips + 两条完整的大腿链都绑上才为 true。步态没有这个前提就不是"走得像不像"的问题，
    // 而是单腿原地摆的问题，所以这个标志必须由消费方检查，而不是"反正摆不出错就不管"。
    bool walkable = false;

    /**
     * 名字表 → 槽位，并量出三个轴。不打印、不失败，缺失槽位留在 -1（见文件头）。
     *
     * `naming` 决定"这份资产用哪一族骨骼名"。传 nullptr 时按 `DetectBestNaming()` 自动挑
     * 命中最多的那一族——这是绝大多数调用方该用的方式；显式传一个族只在"我知道它是什么、
     * 别猜了"的时候（例如一个已知是混元导出的资产，但它的名字恰好也命中了别的族的少数槽位）。
     *
     * 为什么是 static 成员而不是自由函数 `ClassifyRig`：`RigProfile::Classify(skeleton)` 在
     * 编辑器自动补全里能被"Rig"找到，而 `ClassifyRig` 只能被人记着；仓库里同类一次性工厂
     * （GeometryFactory::Cube 之流）也都是挂在类型上的。
     */
    [[nodiscard]] static RigProfile Classify(const Skeleton& skeleton, const RigNaming* naming = nullptr);

    /**
     * 自动挑族：遍历引擎内置的每一族（Mixamo/Blender、3ds Max Biped、UE…），取 `Detect()`
     * 最高的那个。返回的引用永远有效（指向无状态单例），不会返回 nullptr。
     *
     * 平手时的顺序是固定的（按内置列表顺序），不是为了"更聪明"，而是为了同一份资产在两次
     * 运行里挑到同一族——否则槽位解析结果会随编译期无关改动漂移。
     */
    [[nodiscard]] static const RigNaming& DetectBestNaming(const Skeleton& skeleton);

    // 实际选中那一族的名字（"mixamo/Blender"…），写进诊断，让人知道这份资产被当成谁。
    // 混元族接进来之后，这一行是"它到底有没有被认出来"的唯一证据。
    [[nodiscard]] const char* namingFamily() const noexcept { return namingFamily_; }

    // 槽位的诊断名，形如 "legUp.L"，直接可打进日志。返回静态字符串字面量，不分配。
    [[nodiscard]] static const char* SlotName(RigSlot slot) noexcept;

    // 槽位对应的骨骼下标，未绑定为 -1。
    [[nodiscard]] int Index(RigSlot slot) const noexcept;

    // 20 个必需槽位里绑上了几个（spine1 不在分母里）。
    [[nodiscard]] int BoundCount() const noexcept;

    /**
     * 未绑定的**必需**槽位名字，全绑上时为空。
     *
     * 返回 `std::vector<const char*>` 而不是 `std::vector<std::string>`：名字全都是静态
     * 字符串字面量，`const char*` 拷的是指针，不需要为每个名字建一条 std::string（也就不会
     * 在堆上留下一串小对象）；剩下的唯一一次分配是 vector 本身，而这是个启动时调用一次、
     * 只在诊断路径上的函数，不值得为它引入固定容量的自造容器。
     * 用 `std::string` 存名字会为每个槽位各建一条堆对象，而这些都是启动期一次性诊断；
     * 换成 `std::string_view` 则把生命周期约束推给每个调用方（存进容器或日志后可能悬垂）。
     * 指针指向字符串字面量，两头的麻烦都没有。
     *
     * 只报必需槽位；spine1 的缺失由 BindingReport() 单独提一句。
     */
    [[nodiscard]] std::vector<const char*> Missing() const;

    /**
     * 给 `--on report` 这类启动期自检用的一行话：
     *   "20/20 slots bound"
     *   "18/20 slots bound; unbound: hand.L hand.R"
     * 并在 spine1 缺失时追加 "; auxiliary spine1 unbound"。
     *
     * 为什么放在引擎层而不是 demo 里：这段话的措辞不重要，重要的是它存在——一旦某个槽位
     * 悄悄漏绑，报出来的一句话比"手看起来僵"这种主观描述更早让人找到根因。demo 各写一份
     * 的时候，这个自检就只存在于"写那段代码的人记得的那个 demo"里。
     */
    [[nodiscard]] std::string BindingReport() const;
};

} // namespace gldx

#endif // GLDX_ANIM_RIGPROFILE_H
