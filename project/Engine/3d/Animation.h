#pragma once
#include "GltfTypes.h"
#include "Structs.h"
#include <map>
#include <string>

// glTFのTRSアニメーションチャンネル(GltfModelData::nodeAnimations)を保持し、
// 任意の時刻を指定して補間済みのTRS値をサンプリングするクラス。
//
// 【重要】キーフレームが1個も無いチャンネルのフォールバック値について:
// glTFのアニメーションは「動かす軸のチャンネルだけ」を持つのが普通で、例えば回転だけをアニメーションさせ
// 静的なtranslation/scaleオフセットは持ったまま、というノードがよくある(テスト用glTFのJoint1がまさにこれで、
// translation=[0,1,0]の静的オフセットを持ちながらrotationチャンネルしか定義していない)。
// そのため、あるチャンネルにキーフレームが1個も無い場合は、汎用的な単位値(translate={0,0,0}等)ではなく、
// GltfModelData::nodes[]が持つそのノード本来のTRS値（レストポーズ）へフォールバックする。
// 汎用単位値を返すと、静的オフセットを持つノードの位置が毎フレーム原点にリセットされてしまい、
// スキン行列(逆バインドポーズ行列との積)と矛盾して描画が破綻することを実装時のトレースで確認したための対応。
class Animation {
public:
    static Animation Create(const GltfModelData& gltfData);

    float GetDuration() const { return duration; }

    // 指定ジョイント名・時刻での補間値を返す。
    // 対応するチャンネルにキーフレームが無ければ、そのノード本来のレストポーズ値を返す
    // （レストポーズ自体も無ければ translate={0,0,0} / scale={1,1,1} / rotate=単位クォータニオン）。
    // 時間が範囲外ならクランプ（端のキー値をそのまま返す）。
    Vector3 SampleTranslate(const std::string& jointName, float time) const;
    Quaternion SampleRotate(const std::string& jointName, float time) const;
    Vector3 SampleScale(const std::string& jointName, float time) const;

private:
    // アニメーションされないチャンネルのフォールバック用に保持する、ノード本来のTRS（レストポーズ）
    struct RestPose {
        Vector3 translate = { 0.0f, 0.0f, 0.0f };
        Quaternion rotate = { 0.0f, 0.0f, 0.0f, 1.0f };
        Vector3 scale = { 1.0f, 1.0f, 1.0f };
    };

    float duration = 0.0f;
    std::map<std::string, GltfNodeAnimation> nodeAnimations;
    std::map<std::string, RestPose> restPoses; // ノード名キー
};
