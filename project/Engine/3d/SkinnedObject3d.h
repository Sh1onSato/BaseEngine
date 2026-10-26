#pragma once
#include "Object3d.h"
#include "GltfTypes.h"
#include "Skeleton.h"
#include "Animation.h"
#include "SkinCluster.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

// glTF駆動のアニメーションを再生するObject3d派生クラス。
// GltfModelData（頂点・骨格・アニメーションの中間データ）を元に、毎フレーム
// 「アニメーションサンプリング → Skeleton::Update() → SkinCluster::Update()」を行い、
// 結果のスキン行列パレットをObject3d::SetBonePalette()経由でGPUへ渡す。
// 骨で動くモデル（人・動物）だけでなく、ノードが動くだけのモデル（トラックの車輪など）も動かせる。
// モーフターゲット（表情・口パク）は CPU で頂点を混ぜて、この物の頂点バッファを書き換える（モーフの後に GPU でスキニングされる）。
//
// 使い方:
//   auto fox = std::make_unique<SkinnedObject3d>();
//   fox->Initialize(object3dCommon);
//   fox->SetGltfData(dxCommon, ModelManager::GetInstance()->LoadGltfModel("Resources/models/fox", "Fox.glb"));
//   fox->PlayAnimation("Walk");            // 最初は 0 番目のアニメーションがループ再生されている
//   fox->Update(); fox->Draw();            // Object3d と同じ
class SkinnedObject3d : public Object3d {
public:
    void Initialize(Object3dCommon* common) override;

    // GltfModelDataから骨格(Skeleton)・スキンクラスタ(SkinCluster)・表示用の Model を作り、0 番目のアニメーションをループ再生する。
    // gltfDataはModelManager::LoadGltfModel()の戻り値。このSkinnedObject3dが持っている間は解放されない。
    void SetGltfData(DirectXCommon* dxCommon, std::shared_ptr<const GltfModelData> gltfData);

    void Update() override;

    // ===== アニメーションの再生 =====
    // 名前のアニメーションを最初から再生する（無ければログを出して何もしない）。loop が false なら最後で止まる
    void PlayAnimation(const std::string& name, bool loop = true);
    // 番号のアニメーションを最初から再生する（範囲外なら何もしない）
    void PlayAnimation(size_t index, bool loop = true);
    // 再生をやめてレストポーズ（モデルを作ったときの姿勢）に戻す
    void StopAnimation();
    // 再生の速さ（1 = 普通、0.5 = 半分、0 = 止める。マイナスは 0 にする）
    void SetAnimationSpeed(float speed);
    float GetAnimationSpeed() const { return animationSpeed; }
    void SetLooping(bool loop) { isLooping = loop; }
    bool IsLooping() const { return isLooping; }
    // 今の再生位置（秒）。変えると、その時刻の姿勢になる
    float GetAnimationTime() const { return animationTime; }
    void SetAnimationTime(float time);
    // ループしない再生が最後まで行ったか（ループ中・停止中は false）
    bool IsAnimationFinished() const { return isFinished; }

    // アニメーションの数・名前・長さ（秒）
    size_t GetAnimationCount() const;
    const std::string& GetAnimationName(size_t index) const;
    float GetAnimationDuration(size_t index) const;
    // 再生中のアニメーションの番号（停止中は -1）
    int32_t GetCurrentAnimationIndex() const { return currentAnimationIndex; }

    // ===== モーフターゲット（表情・口パクなど） =====
    // ターゲットの数（モーフが無ければ 0）
    size_t GetMorphTargetCount() const { return morphTargetCount; }
    // ターゲットの名前（Blender のシェイプキー名。glTF に書かれていなければ空）・名前からの番号（無ければ -1）
    const std::string& GetMorphTargetName(size_t targetIndex) const;
    int32_t FindMorphTarget(const std::string& name) const;
    // ターゲットの重みを自分で決める（0 = 元の形、1 = そのターゲットの形）。アニメーションの重みより優先する
    void SetMorphWeight(size_t targetIndex, float weight);
    // 自分で決めた重みをやめて、アニメーション（無ければ glTF の最初の重み）に戻す
    void ClearMorphWeight(size_t targetIndex);
    void ClearMorphWeights() { morphWeightOverrides.clear(); }
    // 今の重み（モーフするメッシュが複数あれば最初の物）
    float GetMorphWeight(size_t targetIndex) const;

    // ===== 骨の位置 =====
    // 名前のジョイント（骨・ノード）のワールド行列（剣を手に持たせるなど。Update の後で呼ぶ）。無ければ false
    bool GetJointWorldMatrix(const std::string& jointName, Matrix4x4& outMatrix) const;

private:
    // 再生位置を進める（ループ・最後で止める）
    void AdvanceAnimationTime();
    // モーフの重みを今の時刻・自分で決めた重みにして、変わったメッシュの頂点を作り直す
    void UpdateMorphWeights(const GltfAnimation* animation);

    std::shared_ptr<const GltfModelData> gltfData;
    // gltfData から作った表示用のモデル（骨の重みを残してある）
    std::shared_ptr<Model> ownModel;
    Skeleton skeleton;
    SkinCluster skinCluster;
    int32_t currentAnimationIndex = -1;
    float animationTime = 0.0f;
    float animationSpeed = 1.0f;
    bool isLooping = true;
    bool isFinished = false;

    // モーフするメッシュを置いているノードと、そのノードに今かかっている重み
    std::map<int32_t, std::vector<float>> appliedMorphWeights;
    // 自分で決めた重み（ターゲット番号キー）
    std::map<size_t, float> morphWeightOverrides;
    size_t morphTargetCount = 0;
};
