#pragma once
#include "Object3d.h"
#include "GltfTypes.h"
#include "Skeleton.h"
#include "Animation.h"
#include "SkinCluster.h"
#include <functional>
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
//
// 時刻の合図（足音・攻撃判定を出すタイミング）:
//   fox->AddAnimationEvent("Walk", 0.25f, "Step");                          // Walk の 0.25 秒の所を通ったら "Step"
//   fox->SetOnAnimationEvent([](const std::string& name) { if (name == "Step") { 足音を鳴らす; } });
// 上半身だけ別のアニメーション（レイヤー）:
//   size_t upper = fox->AddLayer("b_Spine01_02");                           // この骨とその先（背中・首・頭・前足）だけ
//   fox->PlayLayerAnimation(upper, "Attack", false, 0.1f);                  // 下半身は Run のまま、上半身だけ Attack
//   if (fox->IsLayerAnimationFinished(upper)) { fox->StopLayerAnimation(upper, 0.2f); }
// 8方向移動（2次元で混ぜる）:
//   fox->PlayBlend2D({ { "Idle", { 0, 0 } }, { "WalkF", { 0, 1 } }, { "WalkB", { 0, -1 } }, { "WalkL", { -1, 0 } }, { "WalkR", { 1, 0 } } }, input);
//   fox->SetBlend2DParameter(input); // 毎フレーム（スティックの向きなど）
class SkinnedObject3d : public Object3d {
public:
    void Initialize(Object3dCommon* common) override;

    // GltfModelDataから骨格(Skeleton)・スキンクラスタ(SkinCluster)・表示用の Model を作り、0 番目のアニメーションをループ再生する。
    // gltfDataはModelManager::LoadGltfModel()の戻り値。このSkinnedObject3dが持っている間は解放されない。
    // 呼び直すとレイヤーと時刻の合図は消える（どちらもこの後で足す）
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
    float GetBlendRate() const { return base.current.blendRate; }
    // 3本以上を、2次元のパラメーター（スティックの向き・移動の向きと速さなど）で混ぜてループ再生する（8方向移動など）。
    // parameter に近い点のアニメーションほど強く混ざる（点の上ではそのアニメーションだけ）。足の運びは PlayBlend と同じくそろう。
    // 見つからないアニメーションの点は無視する（全部無ければ何もしない）
    void PlayBlend2D(const std::vector<AnimationBlendPoint>& points, const Vector2& parameter, float blendSeconds = 0.0f);
    // PlayBlend2D のパラメーターを変える（毎フレーム変えてよい）
    void SetBlend2DParameter(const Vector2& parameter);
    const Vector2& GetBlend2DParameter() const { return base.current.blendParameter; }
    // PlayBlend / PlayBlend2D 中か
    bool IsPlayingBlend() const { return base.current.clips.size() >= 2; }
    // 混ぜているアニメーション（確認表示用）: 本数・i 本目の番号・今の割合（全部足すと 1）
    size_t GetBlendClipCount() const { return base.current.clips.size(); }
    int32_t GetBlendClipAnimation(size_t clip) const;
    float GetBlendClipWeight(size_t clip) const;
    // 再生をやめてレストポーズ（モデルを作ったときの姿勢）に戻す（blendSeconds かけて戻すこともできる）
    void StopAnimation(float blendSeconds = 0.0f);
    // 再生の速さ（1 = 普通、0.5 = 半分、0 = 止める。マイナスは 0 にする）。全部のアニメーション（レイヤーも）に効く
    void SetAnimationSpeed(float speed);
    float GetAnimationSpeed() const { return animationSpeed; }
    // 今再生しているアニメーションだけの速さ（SetAnimationSpeed と掛け合わせる）。次に Play〜 すると 1 に戻る。
    // 歩く速さに足の運びを合わせる、攻撃の振りだけ速くする など
    void SetCurrentAnimationSpeed(float speed);
    float GetCurrentAnimationSpeed() const { return base.current.speed; }
    void SetLooping(bool loop) { base.current.isLooping = loop; }
    bool IsLooping() const { return base.current.isLooping; }
    // 今の再生位置（秒。PlayBlend 中は 1周のうちの進み具合 0〜1）。変えると、その時刻の姿勢になる（時刻の合図は鳴らない）
    float GetAnimationTime() const { return IsPlayingBlend() ? base.current.phase : base.current.time; }
    void SetAnimationTime(float time);
    // ループしない再生が最後まで行ったか（ループ中・停止中は false）
    bool IsAnimationFinished() const { return base.current.isFinished; }
    // 前のアニメーションからつないでいる途中か
    bool IsBlending() const { return base.isBlending; }

    // アニメーションの数・名前・長さ（秒）
    size_t GetAnimationCount() const;
    const std::string& GetAnimationName(size_t index) const;
    float GetAnimationDuration(size_t index) const;
    // 名前のアニメーションの番号（無ければ -1）
    int32_t FindAnimation(const std::string& name) const;
    // 再生中のアニメーションの番号（停止中は -1。混ぜているときは1本目）・PlayBlend の2本目（混ぜていなければ -1）
    int32_t GetCurrentAnimationIndex() const { return base.current.index; }
    int32_t GetBlendSecondIndex() const { return GetBlendClipAnimation(1); }

    // ===== 時刻の合図（アニメーションイベント） =====
    // animation の time 秒の所を再生が通ったら eventName を知らせる（足音・攻撃判定の出し入れ・効果音など）。
    // ループするたびに知らせる。混ぜているときは一番強く混ざっているアニメーションの合図だけ。レイヤーの再生でも知らせる。
    // アニメーションが見つからなければログを出して何もしない
    void AddAnimationEvent(const std::string& animation, float time, const std::string& eventName);
    void ClearAnimationEvents() { animationEvents.clear(); }
    // 合図が来たときに呼ぶ関数（Update の終わりに、来た順に呼ぶ）
    void SetOnAnimationEvent(std::function<void(const std::string& eventName)> callback) { onAnimationEvent = std::move(callback); }
    // この Update で来た合図（関数を登録しなくても、こちらで調べられる）
    bool WasAnimationEventFired(const std::string& eventName) const;
    const std::vector<std::string>& GetFiredAnimationEvents() const { return firedEvents; }

    // ===== レイヤー（体の一部だけ別のアニメーション） =====
    // レイヤーを足して、その番号（0, 1, …）を返す。maskRootJoint の骨とその先の骨だけを、レイヤーのアニメーションで上書きする（"" なら全身）。
    // 後から足したレイヤーほど上に重なる。SetGltfData の後で呼ぶ
    size_t AddLayer(const std::string& maskRootJoint = "");
    // 上書きする骨を決め直す（rootJoint とその先。"" なら全身）・一部を外す（rootJoint とその先を上書きしない）
    void SetLayerMask(size_t layer, const std::string& rootJoint);
    void ExcludeFromLayerMask(size_t layer, const std::string& rootJoint);
    // レイヤーでアニメーションを再生する。止まっていたレイヤーは blendSeconds かけて効き始める（再生中なら、その秒数でつなぐ）
    void PlayLayerAnimation(size_t layer, const std::string& name, bool loop = true, float blendSeconds = 0.2f);
    // blendSeconds かけてレイヤーを効かなくして止める（下のアニメーションに戻る）
    void StopLayerAnimation(size_t layer, float blendSeconds = 0.2f);
    // レイヤーの効き具合（0 = 効かない、1 = 完全に上書き）。blendSeconds かけて変えることもできる
    void SetLayerWeight(size_t layer, float weight, float blendSeconds = 0.0f);
    float GetLayerWeight(size_t layer) const;
    bool IsLayerAnimationFinished(size_t layer) const;
    // レイヤーで再生中のアニメーションの番号（止まっていれば -1）
    int32_t GetLayerAnimationIndex(size_t layer) const;
    size_t GetLayerCount() const { return layers.size(); }

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
    // 混ぜるアニメーション1本分
    struct BlendClip {
        int32_t index = -1;
        float weight = 0.0f;
    };
    // 再生しているアニメーション1本分（混ぜるなら2本以上を混ぜた物）
    struct AnimationTrack {
        // gltfData->animations の番号（-1 ならレストポーズ。混ぜるときは1本目）
        int32_t index = -1;
        float time = 0.0f;
        bool isLooping = true;
        bool isFinished = false;
        // このアニメーションだけの速さ
        float speed = 1.0f;
        // 混ぜるアニメーション（2本以上なら混ぜる。全部 この割合の時刻を使うので、長さが違っても足の運びがそろう）
        std::vector<BlendClip> clips;
        float phase = 0.0f;
        // PlayBlend の割合・PlayBlend2D の点とパラメーター（割合 clips[].weight はここから決める）
        float blendRate = 0.0f;
        std::vector<Vector2> blendPositions;
        Vector2 blendParameter = { 0.0f, 0.0f };
        // まだ一度も進めていない（時刻 0 の合図を、最初の Update で知らせるため）
        bool hasAdvanced = false;
    };
    // ノード1つの姿勢
    struct NodePose {
        Vector3 translate = { 0.0f, 0.0f, 0.0f };
        Quaternion rotate = { 0.0f, 0.0f, 0.0f, 1.0f };
        Vector3 scale = { 1.0f, 1.0f, 1.0f };
    };
    // 1つの層の再生（つなぐ元を含む）。全身の再生（base）とレイヤーで使う
    struct TrackPlayer {
        AnimationTrack current;
        // つなぐ元。index が -1 なら blendFromPose（つなぎ始めた瞬間の見た目）を使う
        AnimationTrack blendFrom;
        std::vector<NodePose> blendFromPose;
        bool isBlending = false;
        float blendElapsed = 0.0f;
        float blendDuration = 0.0f;
        // 今の つなぐ元 と current の混ぜ具合（0 でつなぐ元、1 で current）
        float blendRate = 1.0f;
    };
    // レイヤー1つ分
    struct Layer {
        TrackPlayer player;
        std::vector<uint8_t> mask; // ノードごと。1 なら上書きする
        float weight = 0.0f;
        float weightFrom = 0.0f;
        float weightTo = 0.0f;
        float weightElapsed = 0.0f;
        float weightDuration = 0.0f;
        bool stopWhenFaded = false; // 効き具合が 0 になったら再生をやめる
    };
    // 時刻の合図1つ分
    struct AnimationEvent {
        float time = 0.0f;
        std::string name;
    };

    // track の再生位置を deltaTime 秒進める（ループ・最後で止める）。collectEvents なら通った時刻の合図を firedEvents に積む
    void AdvanceTrack(AnimationTrack& track, float deltaTime, bool collectEvents);
    // animationIndex の、previous〜now（wrapped ならループで 0 に戻った）の間にある合図を積む
    void CollectEvents(int32_t animationIndex, float previous, float now, bool wrapped);
    // player を進め、つなぎの混ぜ具合を決める
    void AdvancePlayer(TrackPlayer& player, float deltaTime, bool collectEvents);
    const GltfAnimation* GetAnimation(int32_t index) const;
    // track のノードの姿勢・モーフの重み（混ぜるなら全部を混ぜた物）
    void SampleTrackPose(const AnimationTrack& track, int32_t nodeIndex, NodePose& pose) const;
    void SampleTrackWeights(const AnimationTrack& track, int32_t nodeIndex, std::vector<float>& weights) const;
    // player のノードの姿勢（つないでいる間は つなぐ元 と混ぜる）
    void SamplePlayerPose(const TrackPlayer& player, int32_t nodeIndex, NodePose& pose) const;
    // 混ぜるアニメーションの割合を、PlayBlend の割合・PlayBlend2D のパラメーターから決め直す
    static void UpdateBlendWeights(AnimationTrack& track);
    // つなぎ始める（今の見た目か、再生中のアニメーションを「つなぐ元」にする）。isBase ならモーフの重みも覚える
    void BeginBlend(TrackPlayer& player, float blendSeconds, bool isBase);
    // 全ノードの姿勢を決める（全身の再生の上にレイヤーを重ねる）
    void UpdatePose();
    // モーフの重みを今の時刻・自分で決めた重みにして、変わったメッシュの頂点を作り直す（全身の再生だけを使う）
    void UpdateMorphWeights();
    // レイヤーの効き具合を進める
    void UpdateLayerWeight(Layer& layer, float deltaTime);
    // jointIndex とその先のノードの mask を value にする
    void MarkJointTree(std::vector<uint8_t>& mask, int32_t jointIndex, uint8_t value) const;
    Layer* FindLayer(size_t layer);
    const Layer* FindLayer(size_t layer) const;

    std::shared_ptr<const GltfModelData> gltfData;
    // gltfData から作った表示用のモデル（骨の重みを残してある）
    std::shared_ptr<Model> ownModel;
    Skeleton skeleton;
    SkinCluster skinCluster;
    float animationSpeed = 1.0f;

    // 全身の再生
    TrackPlayer base;
    std::map<int32_t, std::vector<float>> blendFromMorphWeights;
    std::vector<Layer> layers;

    // 時刻の合図（アニメーションの番号キー）と、この Update で来た合図
    std::map<int32_t, std::vector<AnimationEvent>> animationEvents;
    std::vector<std::string> firedEvents;
    std::function<void(const std::string&)> onAnimationEvent;

    // モーフするメッシュを置いているノードと、そのノードに今かかっている重み
    std::map<int32_t, std::vector<float>> appliedMorphWeights;
    // 自分で決めた重み（ターゲット番号キー）
    std::map<size_t, float> morphWeightOverrides;
    size_t morphTargetCount = 0;
};
