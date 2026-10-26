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
//   fox->PlayAnimation("Walk", true, 0.2f); // 0.2 秒かけて今の姿勢から Walk へつなぐ（最初は 0 番目がループ再生されている）
//   fox->Update(); fox->Draw();             // Object3d と同じ
class SkinnedObject3d : public Object3d {
public:
    void Initialize(Object3dCommon* common) override;

    // GltfModelDataから骨格(Skeleton)・スキンクラスタ(SkinCluster)・表示用の Model を作り、0 番目のアニメーションをループ再生する。
    // gltfDataはModelManager::LoadGltfModel()の戻り値。このSkinnedObject3dが持っている間は解放されない。
    void SetGltfData(DirectXCommon* dxCommon, std::shared_ptr<const GltfModelData> gltfData);

    void Update() override;

    // ===== アニメーションの再生 =====
    // 名前のアニメーションを最初から再生する（無ければログを出して何もしない）。loop が false なら最後で止まる。
    // blendSeconds > 0 なら、その秒数かけて今の姿勢からなめらかにつなぐ（前のアニメーションもつなぐ間は動き続ける）
    void PlayAnimation(const std::string& name, bool loop = true, float blendSeconds = 0.0f);
    // 番号のアニメーションを最初から再生する（範囲外なら何もしない）
    void PlayAnimation(size_t index, bool loop = true, float blendSeconds = 0.0f);
    // 2本のアニメーションを rate（0 = first だけ、1 = second だけ）で混ぜてループ再生する（歩き↔走りを移動の速さで混ぜるなど）。
    // 長さが違っても、1周のうちの同じ割合の所を使うので足の運びがそろう。どちらかが見つからなければ何もしない
    void PlayBlend(const std::string& first, const std::string& second, float rate, float blendSeconds = 0.0f);
    // PlayBlend で混ぜる割合を変える（0〜1。毎フレーム変えてよい）
    void SetBlendRate(float rate);
    float GetBlendRate() const { return current.secondRate; }
    // PlayBlend 中か
    bool IsPlayingBlend() const { return current.secondIndex >= 0; }
    // 再生をやめてレストポーズ（モデルを作ったときの姿勢）に戻す（blendSeconds かけて戻すこともできる）
    void StopAnimation(float blendSeconds = 0.0f);
    // 再生の速さ（1 = 普通、0.5 = 半分、0 = 止める。マイナスは 0 にする）。つないでいる途中の進み方にも効く
    void SetAnimationSpeed(float speed);
    float GetAnimationSpeed() const { return animationSpeed; }
    void SetLooping(bool loop) { current.isLooping = loop; }
    bool IsLooping() const { return current.isLooping; }
    // 今の再生位置（秒。PlayBlend 中は 1周のうちの進み具合 0〜1）。変えると、その時刻の姿勢になる
    float GetAnimationTime() const { return IsPlayingBlend() ? current.phase : current.time; }
    void SetAnimationTime(float time);
    // ループしない再生が最後まで行ったか（ループ中・停止中は false）
    bool IsAnimationFinished() const { return current.isFinished; }
    // 前のアニメーションからつないでいる途中か
    bool IsBlending() const { return isBlending; }

    // アニメーションの数・名前・長さ（秒）
    size_t GetAnimationCount() const;
    const std::string& GetAnimationName(size_t index) const;
    float GetAnimationDuration(size_t index) const;
    // 名前のアニメーションの番号（無ければ -1）
    int32_t FindAnimation(const std::string& name) const;
    // 再生中のアニメーションの番号（停止中は -1。PlayBlend 中は1本目）・PlayBlend の2本目（混ぜていなければ -1）
    int32_t GetCurrentAnimationIndex() const { return current.index; }
    int32_t GetBlendSecondIndex() const { return current.secondIndex; }

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
    // 再生しているアニメーション1本分（PlayBlend なら2本を混ぜた物）
    struct AnimationTrack {
        // gltfData->animations の番号（-1 ならレストポーズ）
        int32_t index = -1;
        float time = 0.0f;
        bool isLooping = true;
        bool isFinished = false;
        // 混ぜるもう1本（-1 なら混ぜない）と、その割合（0 = index だけ、1 = secondIndex だけ）
        int32_t secondIndex = -1;
        float secondRate = 0.0f;
        // 混ぜるときの進み具合（0〜1。2本とも この割合の時刻を使うので、長さが違っても足の運びがそろう）
        float phase = 0.0f;
    };
    // ノード1つの姿勢
    struct NodePose {
        Vector3 translate = { 0.0f, 0.0f, 0.0f };
        Quaternion rotate = { 0.0f, 0.0f, 0.0f, 1.0f };
        Vector3 scale = { 1.0f, 1.0f, 1.0f };
    };

    // track の再生位置を deltaTime 秒進める（ループ・最後で止める）
    void AdvanceTrack(AnimationTrack& track, float deltaTime) const;
    const GltfAnimation* GetAnimation(int32_t index) const;
    // track のノードの姿勢・モーフの重み（混ぜるなら2本を混ぜた物）
    void SampleTrackPose(const AnimationTrack& track, int32_t nodeIndex, NodePose& pose) const;
    void SampleTrackWeights(const AnimationTrack& track, int32_t nodeIndex, std::vector<float>& weights) const;
    // つなぎ始める（今の見た目か、再生中のアニメーションを「つなぐ元」にする）
    void BeginBlend(float blendSeconds);
    // 全ノードの姿勢を決める（つないでいる間は つなぐ元 と 今のアニメーション を混ぜる）
    void UpdatePose(float blendRate);
    // モーフの重みを今の時刻・自分で決めた重みにして、変わったメッシュの頂点を作り直す
    void UpdateMorphWeights(float blendRate);

    std::shared_ptr<const GltfModelData> gltfData;
    // gltfData から作った表示用のモデル（骨の重みを残してある）
    std::shared_ptr<Model> ownModel;
    Skeleton skeleton;
    SkinCluster skinCluster;
    float animationSpeed = 1.0f;

    // 今再生しているアニメーション
    AnimationTrack current;
    // つなぐ元。index が -1 なら blendFromPose（つなぎ始めた瞬間の見た目）を使う
    AnimationTrack blendFrom;
    std::vector<NodePose> blendFromPose;
    std::map<int32_t, std::vector<float>> blendFromMorphWeights;
    bool isBlending = false;
    float blendElapsed = 0.0f;
    float blendDuration = 0.0f;

    // モーフするメッシュを置いているノードと、そのノードに今かかっている重み
    std::map<int32_t, std::vector<float>> appliedMorphWeights;
    // 自分で決めた重み（ターゲット番号キー）
    std::map<size_t, float> morphWeightOverrides;
    size_t morphTargetCount = 0;
};
