#pragma once
#include "GltfTypes.h"
#include "Structs.h"
#include <string>
#include <vector>

// 2次元で混ぜるアニメーション1本分（SkinnedObject3d::PlayBlend2D / AnimationStateMachine の State::blendPoints）。
// position はそのアニメーションが一番合うパラメーターの値（8方向移動なら 前 = {0, 1}、右 = {1, 0}、止まる = {0, 0} など）
struct AnimationBlendPoint {
    std::string animation;
    Vector2 position = { 0.0f, 0.0f };
};

// glTFのアニメーション1本(GltfAnimation)から、任意の時刻のノードの姿勢（translate / rotate / scale）を求める関数群。
// データは GltfModelData が持っているので、ここでは何もコピーしない。
//
// 【重要】キーフレームが1個も無いチャンネルのフォールバック値について:
// glTFのアニメーションは「動かす軸のチャンネルだけ」を持つのが普通で、例えば回転だけをアニメーションさせ
// 静的なtranslation/scaleオフセットは持ったまま、というノードがよくある(テスト用glTFのJoint1がまさにこれで、
// translation=[0,1,0]の静的オフセットを持ちながらrotationチャンネルしか定義していない)。
// そのため、あるチャンネルにキーフレームが1個も無い場合は、汎用的な単位値(translate={0,0,0}等)ではなく、
// そのノード本来のTRS値（レストポーズ、GltfModelData::nodes[]）へフォールバックする。
// 汎用単位値を返すと、静的オフセットを持つノードの位置が毎フレーム原点にリセットされてしまい、
// スキン行列(逆バインドポーズ行列との積)と矛盾して描画が破綻することを実装時のトレースで確認したための対応。
class Animation {
public:
    Animation() = delete;

    // animation の time 秒でのノード nodeIndex の姿勢を求める。animation が nullptr か、動かないチャンネルは restNode の値。
    // 時間が範囲外ならクランプ（端のキー値をそのまま返す）。
    static void SampleNode(const GltfAnimation* animation, int32_t nodeIndex, const GltfNode& restNode, float time,
                           Vector3& translate, Quaternion& rotate, Vector3& scale);

    // animation の time 秒でのノード nodeIndex のモーフの重みを weights に入れる。
    // このアニメーションに重みのチャンネルが無ければ false（weights はそのまま）
    static bool SampleWeights(const GltfAnimation* animation, int32_t nodeIndex, float time, std::vector<float>& weights);
};
