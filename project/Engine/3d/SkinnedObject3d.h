#pragma once
#include "Object3d.h"
#include "GltfTypes.h"
#include "Skeleton.h"
#include "Animation.h"
#include "SkinCluster.h"

// glTF駆動のスケルタルアニメーションを再生するObject3d派生クラス。
// GltfModelData（頂点・骨格・アニメーションの中間データ）を元に、毎フレーム
// 「アニメーションサンプリング → Skeleton::Update() → SkinCluster::Update()」を行い、
// 結果のスキン行列パレットをObject3d::SetBonePalette()経由でGPUへ渡す。
class SkinnedObject3d : public Object3d {
public:
    void Initialize(Object3dCommon* common) override;

    // GltfModelDataから骨格(Skeleton)・アニメーション(Animation)・スキンクラスタ(SkinCluster)を構築する。
    // gltfDataはModelManagerのキャッシュ（呼び出し側）が所有権を持つ非所有ポインタ。
    // このSkinnedObject3dが生きている間、呼び出し側はgltfDataを解放しないこと。
    void SetGltfData(DirectXCommon* dxCommon, const GltfModelData* gltfData);

    void Update() override;

    void SetLooping(bool loop) { isLooping = loop; }

private:
    const GltfModelData* gltfData = nullptr; // 非所有
    Skeleton skeleton;
    Animation animation;
    SkinCluster skinCluster;
    float animationTime = 0.0f;
    bool isLooping = true;
};
