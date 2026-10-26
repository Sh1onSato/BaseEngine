#include "SkinnedObject3d.h"
#include "Calculation.h"
#include "GameTime.h"
#include "Logger.h"
#include <algorithm>
#include <cmath>

namespace {
    // これより短い2点の間は、2次元で混ぜる割合の計算に使わない（同じ位置の点）
    constexpr float kMinBlendPointDistance = 1.0e-6f;

    Vector3 Lerp(const Vector3& a, const Vector3& b, float t) {
        return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
    }

    // なめらかに始まってなめらかに終わる 0〜1
    float SmoothStep(float t) {
        return t * t * (3.0f - 2.0f * t);
    }
}

void SkinnedObject3d::Initialize(Object3dCommon* common) {
    Object3d::Initialize(common);
}

void SkinnedObject3d::SetGltfData(DirectXCommon* dxCommon, std::shared_ptr<const GltfModelData> gltfData) {
    this->gltfData = std::move(gltfData);
    skeleton = Skeleton::Create(*this->gltfData);
    skinCluster.Initialize(dxCommon, *this->gltfData);

    // 骨の重みを残した表示用のモデル（同じ glTF でも SkinnedObject3d ごとに作る）
    ownModel = std::make_shared<Model>();
    ownModel->InitializeFromGltf(dxCommon, *this->gltfData, true);
    SetModel(ownModel);

    // モーフするメッシュを置いているノード（Model は最初の重みで作ってある）
    appliedMorphWeights.clear();
    morphWeightOverrides.clear();
    morphTargetCount = 0;
    for (const GltfPrimitive& primitive : this->gltfData->primitives) {
        if (primitive.morphTargets.empty() || primitive.nodeIndex < 0) continue;
        appliedMorphWeights[primitive.nodeIndex] = this->gltfData->nodes[static_cast<size_t>(primitive.nodeIndex)].morphWeights;
        morphTargetCount = (std::max)(morphTargetCount, primitive.morphTargets.size());
    }

    // アニメーションの番号・骨の番号が変わるので、レイヤーと時刻の合図は作り直してもらう
    const bool wasLooping = base.current.isLooping;
    base = TrackPlayer{};
    base.blendFromPose.assign(this->gltfData->nodes.size(), NodePose{});
    blendFromMorphWeights.clear();
    layers.clear();
    animationEvents.clear();
    firedEvents.clear();
    if (GetAnimationCount() > 0) {
        PlayAnimation(size_t{ 0 }, wasLooping);
    } else {
        StopAnimation();
    }
}

void SkinnedObject3d::PlayAnimation(const std::string& name, bool loop, float blendSeconds) {
    const int32_t index = FindAnimation(name);
    if (index < 0) {
        Logger::Log("SkinnedObject3d: アニメーションが見つかりません: " + name + "\n");
        return;
    }
    PlayAnimation(static_cast<size_t>(index), loop, blendSeconds);
}

void SkinnedObject3d::PlayAnimation(size_t index, bool loop, float blendSeconds) {
    if (index >= GetAnimationCount()) {
        return;
    }
    BeginBlend(base, blendSeconds, true);
    base.current = AnimationTrack{};
    base.current.index = static_cast<int32_t>(index);
    base.current.isLooping = loop;
}

void SkinnedObject3d::PlayBlend(const std::string& first, const std::string& second, float rate, float blendSeconds) {
    const int32_t firstIndex = FindAnimation(first);
    const int32_t secondIndex = FindAnimation(second);
    if (firstIndex < 0 || secondIndex < 0) {
        Logger::Log("SkinnedObject3d: 混ぜるアニメーションが見つかりません: " + first + " / " + second + "\n");
        return;
    }
    BeginBlend(base, blendSeconds, true);
    base.current = AnimationTrack{};
    base.current.index = firstIndex;
    base.current.clips = { { firstIndex, 0.0f }, { secondIndex, 0.0f } };
    base.current.blendRate = std::clamp(rate, 0.0f, 1.0f);
    base.current.isLooping = true;
    UpdateBlendWeights(base.current);
}

void SkinnedObject3d::SetBlendRate(float rate) {
    base.current.blendRate = std::clamp(rate, 0.0f, 1.0f);
    UpdateBlendWeights(base.current);
}

void SkinnedObject3d::PlayBlend2D(const std::vector<AnimationBlendPoint>& points, const Vector2& parameter, float blendSeconds) {
    AnimationTrack track;
    for (const AnimationBlendPoint& point : points) {
        const int32_t index = FindAnimation(point.animation);
        if (index < 0) {
            Logger::Log("SkinnedObject3d: 2次元で混ぜるアニメーションが見つかりません: " + point.animation + "\n");
            continue;
        }
        track.clips.push_back({ index, 0.0f });
        track.blendPositions.push_back(point.position);
    }
    if (track.clips.empty()) {
        return;
    }
    if (track.clips.size() == 1) {
        PlayAnimation(static_cast<size_t>(track.clips[0].index), true, blendSeconds);
        return;
    }
    BeginBlend(base, blendSeconds, true);
    track.index = track.clips[0].index;
    track.isLooping = true;
    track.blendParameter = parameter;
    base.current = std::move(track);
    UpdateBlendWeights(base.current);
}

void SkinnedObject3d::SetBlend2DParameter(const Vector2& parameter) {
    base.current.blendParameter = parameter;
    UpdateBlendWeights(base.current);
}

void SkinnedObject3d::UpdateBlendWeights(AnimationTrack& track) {
    if (track.clips.size() < 2) {
        return;
    }
    if (track.blendPositions.size() != track.clips.size()) {
        // PlayBlend（2本を割合で）
        track.clips[0].weight = 1.0f - track.blendRate;
        track.clips[1].weight = track.blendRate;
        return;
    }
    // PlayBlend2D: 点 i の割合は「i から他の点 j へ向かって、パラメーターがどれだけ j 寄りか」の一番小さい物（グラディエントバンド補間）。
    // 点の上ではその点だけ 1、2点の間では距離の割合で混ざり、点の並び方（四角・ひし形・十字）に関係なく使える
    const Vector2& parameter = track.blendParameter;
    float total = 0.0f;
    for (size_t i = 0; i < track.clips.size(); ++i) {
        const Vector2& from = track.blendPositions[i];
        float weight = 1.0f;
        for (size_t j = 0; j < track.clips.size(); ++j) {
            const Vector2 toOther = { track.blendPositions[j].x - from.x, track.blendPositions[j].y - from.y };
            const float lengthSquared = toOther.x * toOther.x + toOther.y * toOther.y;
            if (i == j || lengthSquared < kMinBlendPointDistance) {
                continue;
            }
            const Vector2 toParameter = { parameter.x - from.x, parameter.y - from.y };
            const float along = (toParameter.x * toOther.x + toParameter.y * toOther.y) / lengthSquared;
            weight = (std::min)(weight, 1.0f - along);
        }
        weight = (std::max)(weight, 0.0f);
        track.clips[i].weight = weight;
        total += weight;
    }
    if (total <= 0.0f) {
        // 念のため（全部 0 になることは無いはずだが）、1本目だけにする
        track.clips[0].weight = total = 1.0f;
    }
    for (BlendClip& clip : track.clips) {
        clip.weight /= total;
    }
}

int32_t SkinnedObject3d::GetBlendClipAnimation(size_t clip) const {
    return clip < base.current.clips.size() ? base.current.clips[clip].index : -1;
}

float SkinnedObject3d::GetBlendClipWeight(size_t clip) const {
    return clip < base.current.clips.size() ? base.current.clips[clip].weight : 0.0f;
}

void SkinnedObject3d::StopAnimation(float blendSeconds) {
    BeginBlend(base, blendSeconds, true);
    const bool wasLooping = base.current.isLooping;
    base.current = AnimationTrack{};
    base.current.isLooping = wasLooping;
}

void SkinnedObject3d::BeginBlend(TrackPlayer& player, float blendSeconds, bool isBase) {
    if (blendSeconds <= 0.0f || !gltfData) {
        player.isBlending = false;
        return;
    }
    if (player.isBlending || player.current.index < 0) {
        // つないでいる途中か停止中なら、今の見た目をそのまま止めた形からつなぐ（2本以上を同時に混ぜない）
        for (const Joint& joint : skeleton.GetJoints()) {
            NodePose& pose = player.blendFromPose[static_cast<size_t>(joint.index)];
            pose.translate = joint.transform.translate;
            pose.rotate = joint.rotate;
            pose.scale = joint.transform.scale;
        }
        if (isBase) {
            blendFromMorphWeights = appliedMorphWeights;
        }
        player.blendFrom = AnimationTrack{};
        player.blendFrom.isLooping = false;
    } else {
        // 再生中のアニメーションは、つなぐ間も動かし続ける
        player.blendFrom = player.current;
    }
    player.isBlending = true;
    player.blendElapsed = 0.0f;
    player.blendDuration = blendSeconds;
}

void SkinnedObject3d::SetAnimationSpeed(float speed) {
    animationSpeed = (std::max)(speed, 0.0f);
}

void SkinnedObject3d::SetCurrentAnimationSpeed(float speed) {
    base.current.speed = (std::max)(speed, 0.0f);
}

void SkinnedObject3d::SetAnimationTime(float time) {
    if (IsPlayingBlend()) {
        base.current.phase = std::clamp(time, 0.0f, 1.0f);
        return;
    }
    base.current.time = (std::max)(time, 0.0f);
    base.current.isFinished = false;
}

size_t SkinnedObject3d::GetAnimationCount() const {
    return gltfData ? gltfData->animations.size() : 0;
}

const std::string& SkinnedObject3d::GetAnimationName(size_t index) const {
    static const std::string kEmpty;
    return index < GetAnimationCount() ? gltfData->animations[index].name : kEmpty;
}

float SkinnedObject3d::GetAnimationDuration(size_t index) const {
    return index < GetAnimationCount() ? gltfData->animations[index].duration : 0.0f;
}

int32_t SkinnedObject3d::FindAnimation(const std::string& name) const {
    for (size_t i = 0; i < GetAnimationCount(); ++i) {
        if (gltfData->animations[i].name == name) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

// ===== 時刻の合図 =====

void SkinnedObject3d::AddAnimationEvent(const std::string& animation, float time, const std::string& eventName) {
    const int32_t index = FindAnimation(animation);
    if (index < 0) {
        Logger::Log("SkinnedObject3d: 合図を付けるアニメーションが見つかりません: " + animation + "\n");
        return;
    }
    animationEvents[index].push_back({ time, eventName });
}

bool SkinnedObject3d::WasAnimationEventFired(const std::string& eventName) const {
    return std::find(firedEvents.begin(), firedEvents.end(), eventName) != firedEvents.end();
}

void SkinnedObject3d::CollectEvents(int32_t animationIndex, float previous, float now, bool wrapped) {
    auto it = animationEvents.find(animationIndex);
    if (it == animationEvents.end()) {
        return;
    }
    for (const AnimationEvent& event : it->second) {
        // previous より後で now まで（ループで 0 に戻ったなら、previous より後 か now まで）
        const bool isPassed = wrapped ? (event.time > previous || event.time <= now) : (event.time > previous && event.time <= now);
        if (isPassed) {
            firedEvents.push_back(event.name);
        }
    }
}

// ===== レイヤー =====

SkinnedObject3d::Layer* SkinnedObject3d::FindLayer(size_t layer) {
    if (layer >= layers.size()) {
        Logger::Log("SkinnedObject3d: レイヤーの番号が範囲外です\n");
        return nullptr;
    }
    return &layers[layer];
}

const SkinnedObject3d::Layer* SkinnedObject3d::FindLayer(size_t layer) const {
    return layer < layers.size() ? &layers[layer] : nullptr;
}

size_t SkinnedObject3d::AddLayer(const std::string& maskRootJoint) {
    Layer layer;
    const size_t nodeCount = gltfData ? gltfData->nodes.size() : 0;
    layer.mask.assign(nodeCount, 0);
    layer.player.blendFromPose.assign(nodeCount, NodePose{});
    layers.push_back(std::move(layer));
    SetLayerMask(layers.size() - 1, maskRootJoint);
    return layers.size() - 1;
}

void SkinnedObject3d::MarkJointTree(std::vector<uint8_t>& mask, int32_t jointIndex, uint8_t value) const {
    const size_t index = static_cast<size_t>(jointIndex);
    if (index >= mask.size()) {
        return;
    }
    mask[index] = value;
    for (int32_t child : skeleton.GetJoints()[index].children) {
        MarkJointTree(mask, child, value);
    }
}

void SkinnedObject3d::SetLayerMask(size_t layerIndex, const std::string& rootJoint) {
    Layer* layer = FindLayer(layerIndex);
    if (!layer) {
        return;
    }
    if (rootJoint.empty()) {
        std::fill(layer->mask.begin(), layer->mask.end(), static_cast<uint8_t>(1));
        return;
    }
    std::fill(layer->mask.begin(), layer->mask.end(), static_cast<uint8_t>(0));
    const int32_t jointIndex = skeleton.FindJoint(rootJoint);
    if (jointIndex < 0) {
        Logger::Log("SkinnedObject3d: レイヤーの骨が見つかりません: " + rootJoint + "\n");
        return;
    }
    MarkJointTree(layer->mask, jointIndex, 1);
}

void SkinnedObject3d::ExcludeFromLayerMask(size_t layerIndex, const std::string& rootJoint) {
    Layer* layer = FindLayer(layerIndex);
    if (!layer) {
        return;
    }
    const int32_t jointIndex = skeleton.FindJoint(rootJoint);
    if (jointIndex < 0) {
        Logger::Log("SkinnedObject3d: レイヤーから外す骨が見つかりません: " + rootJoint + "\n");
        return;
    }
    MarkJointTree(layer->mask, jointIndex, 0);
}

void SkinnedObject3d::PlayLayerAnimation(size_t layerIndex, const std::string& name, bool loop, float blendSeconds) {
    Layer* layer = FindLayer(layerIndex);
    const int32_t index = FindAnimation(name);
    if (!layer || index < 0) {
        Logger::Log("SkinnedObject3d: レイヤーで再生するアニメーションが見つかりません: " + name + "\n");
        return;
    }
    if (layer->player.current.index < 0 || layer->weight <= 0.0f) {
        // 止まっていたレイヤーは、つながずに再生し、効き具合を 0 から上げる
        layer->player.isBlending = false;
    } else {
        BeginBlend(layer->player, blendSeconds, false);
    }
    layer->player.current = AnimationTrack{};
    layer->player.current.index = index;
    layer->player.current.isLooping = loop;
    SetLayerWeight(layerIndex, 1.0f, blendSeconds);
}

void SkinnedObject3d::StopLayerAnimation(size_t layerIndex, float blendSeconds) {
    Layer* layer = FindLayer(layerIndex);
    if (!layer) {
        return;
    }
    SetLayerWeight(layerIndex, 0.0f, blendSeconds);
    layer->stopWhenFaded = true;
    UpdateLayerWeight(*layer, 0.0f); // blendSeconds が 0 ならここで止まる
}

void SkinnedObject3d::SetLayerWeight(size_t layerIndex, float weight, float blendSeconds) {
    Layer* layer = FindLayer(layerIndex);
    if (!layer) {
        return;
    }
    layer->weightFrom = layer->weight;
    layer->weightTo = std::clamp(weight, 0.0f, 1.0f);
    layer->weightElapsed = 0.0f;
    layer->weightDuration = (std::max)(blendSeconds, 0.0f);
    layer->stopWhenFaded = false;
    if (layer->weightDuration <= 0.0f) {
        layer->weight = layer->weightTo;
    }
}

float SkinnedObject3d::GetLayerWeight(size_t layerIndex) const {
    const Layer* layer = FindLayer(layerIndex);
    return layer ? layer->weight : 0.0f;
}

bool SkinnedObject3d::IsLayerAnimationFinished(size_t layerIndex) const {
    const Layer* layer = FindLayer(layerIndex);
    return layer && layer->player.current.isFinished;
}

int32_t SkinnedObject3d::GetLayerAnimationIndex(size_t layerIndex) const {
    const Layer* layer = FindLayer(layerIndex);
    return layer ? layer->player.current.index : -1;
}

void SkinnedObject3d::UpdateLayerWeight(Layer& layer, float deltaTime) {
    if (layer.weightDuration > 0.0f) {
        layer.weightElapsed += deltaTime;
        const float t = (std::min)(layer.weightElapsed / layer.weightDuration, 1.0f);
        layer.weight = layer.weightFrom + (layer.weightTo - layer.weightFrom) * SmoothStep(t);
        if (t >= 1.0f) {
            layer.weightDuration = 0.0f;
        }
    }
    if (layer.stopWhenFaded && layer.weight <= 0.0f) {
        layer.player.current = AnimationTrack{};
        layer.player.isBlending = false;
        layer.stopWhenFaded = false;
    }
}

// ===== モーフ・骨 =====

const std::string& SkinnedObject3d::GetMorphTargetName(size_t targetIndex) const {
    static const std::string kEmpty;
    return gltfData && targetIndex < gltfData->morphTargetNames.size() ? gltfData->morphTargetNames[targetIndex] : kEmpty;
}

int32_t SkinnedObject3d::FindMorphTarget(const std::string& name) const {
    if (!gltfData) {
        return -1;
    }
    for (size_t i = 0; i < gltfData->morphTargetNames.size(); ++i) {
        if (gltfData->morphTargetNames[i] == name) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

void SkinnedObject3d::SetMorphWeight(size_t targetIndex, float weight) {
    morphWeightOverrides[targetIndex] = weight;
}

void SkinnedObject3d::ClearMorphWeight(size_t targetIndex) {
    morphWeightOverrides.erase(targetIndex);
}

float SkinnedObject3d::GetMorphWeight(size_t targetIndex) const {
    if (appliedMorphWeights.empty()) {
        return 0.0f;
    }
    const std::vector<float>& weights = appliedMorphWeights.begin()->second;
    return targetIndex < weights.size() ? weights[targetIndex] : 0.0f;
}

bool SkinnedObject3d::GetJointWorldMatrix(const std::string& jointName, Matrix4x4& outMatrix) const {
    const int32_t jointIndex = skeleton.FindJoint(jointName);
    if (jointIndex < 0) {
        return false;
    }
    // モデルの中での行列 * このオブジェクトのワールド行列（行ベクトル）
    outMatrix = skeleton.GetJoints()[static_cast<size_t>(jointIndex)].skeletonSpaceMatrix * GetWorldMatrix();
    return true;
}

// ===== 再生を進める・姿勢を決める =====

const GltfAnimation* SkinnedObject3d::GetAnimation(int32_t index) const {
    if (index < 0 || static_cast<size_t>(index) >= GetAnimationCount()) {
        return nullptr;
    }
    return &gltfData->animations[static_cast<size_t>(index)];
}

void SkinnedObject3d::AdvanceTrack(AnimationTrack& track, float deltaTime, bool collectEvents) {
    const GltfAnimation* animation = GetAnimation(track.index);
    if (!animation || track.isFinished) {
        return;
    }
    const float step = deltaTime * track.speed;
    if (track.clips.size() >= 2) {
        // 混ぜるときは、割合に合わせた1周の長さで進み具合（0〜1）を進める（割合は全部足すと 1）
        float cycle = 0.0f;
        const BlendClip* strongest = &track.clips[0];
        for (const BlendClip& clip : track.clips) {
            if (const GltfAnimation* clipAnimation = GetAnimation(clip.index)) {
                cycle += clipAnimation->duration * clip.weight;
            }
            if (clip.weight > strongest->weight) {
                strongest = &clip;
            }
        }
        const float previousPhase = track.phase;
        if (cycle > 0.0f) {
            track.phase = std::fmod(track.phase + step / cycle, 1.0f);
        }
        // 合図は一番強く混ざっているアニメーションの物だけ
        if (collectEvents) {
            if (const GltfAnimation* strongestAnimation = GetAnimation(strongest->index)) {
                const float duration = strongestAnimation->duration;
                const float previous = track.hasAdvanced ? previousPhase * duration : -1.0f;
                CollectEvents(strongest->index, previous, track.phase * duration, track.hasAdvanced && track.phase < previousPhase);
            }
        }
        track.hasAdvanced = true;
        return;
    }

    // 最初の Update は時刻 0 の合図も知らせる
    const float previous = track.hasAdvanced ? track.time : -1.0f;
    bool wrapped = false;
    track.time += step;
    if (animation->duration > 0.0f) {
        if (track.isLooping) {
            if (track.time >= animation->duration) {
                track.time = std::fmod(track.time, animation->duration);
                wrapped = true;
            }
        } else if (track.time >= animation->duration) {
            track.time = animation->duration;
            track.isFinished = true;
        }
    }
    if (collectEvents) {
        CollectEvents(track.index, previous, track.time, wrapped);
    }
    track.hasAdvanced = true;
}

void SkinnedObject3d::AdvancePlayer(TrackPlayer& player, float deltaTime, bool collectEvents) {
    AdvanceTrack(player.current, deltaTime, collectEvents);
    // つないでいる間の混ぜ具合（始めと終わりをゆっくりにする）
    player.blendRate = 1.0f;
    if (player.isBlending) {
        AdvanceTrack(player.blendFrom, deltaTime, false);
        player.blendElapsed += deltaTime;
        if (player.blendElapsed >= player.blendDuration) {
            player.isBlending = false;
        } else {
            player.blendRate = SmoothStep(player.blendElapsed / player.blendDuration);
        }
    }
}

void SkinnedObject3d::SampleTrackPose(const AnimationTrack& track, int32_t nodeIndex, NodePose& pose) const {
    // このアニメーションで動かないノードはレストポーズ
    const GltfNode& restNode = gltfData->nodes[static_cast<size_t>(nodeIndex)];
    if (track.clips.size() < 2) {
        Animation::SampleNode(GetAnimation(track.index), nodeIndex, restNode, track.time, pose.translate, pose.rotate, pose.scale);
        return;
    }
    // 全部を1周のうちの同じ割合の時刻で取り、割合で混ぜる（1本ずつ「ここまでの合計 : 次の1本」の比で混ぜていく）
    float total = 0.0f;
    for (const BlendClip& clip : track.clips) {
        const GltfAnimation* animation = GetAnimation(clip.index);
        if (!animation || clip.weight <= 0.0f) {
            continue;
        }
        NodePose clipPose;
        Animation::SampleNode(animation, nodeIndex, restNode, track.phase * animation->duration, clipPose.translate, clipPose.rotate,
                              clipPose.scale);
        if (total <= 0.0f) {
            pose = clipPose;
        } else {
            const float t = clip.weight / (total + clip.weight);
            pose.translate = Lerp(pose.translate, clipPose.translate, t);
            pose.rotate = Calculation::Slerp(pose.rotate, clipPose.rotate, t);
            pose.scale = Lerp(pose.scale, clipPose.scale, t);
        }
        total += clip.weight;
    }
    if (total <= 0.0f) {
        Animation::SampleNode(nullptr, nodeIndex, restNode, 0.0f, pose.translate, pose.rotate, pose.scale);
    }
}

void SkinnedObject3d::SampleTrackWeights(const AnimationTrack& track, int32_t nodeIndex, std::vector<float>& weights) const {
    // weights には glTF の最初の重みを入れて呼ぶ（アニメーションに重みが無ければそのまま）
    if (track.clips.size() < 2) {
        Animation::SampleWeights(GetAnimation(track.index), nodeIndex, track.time, weights);
        return;
    }
    std::vector<float> mixed(weights.size(), 0.0f);
    float total = 0.0f;
    for (const BlendClip& clip : track.clips) {
        const GltfAnimation* animation = GetAnimation(clip.index);
        if (!animation || clip.weight <= 0.0f) {
            continue;
        }
        std::vector<float> clipWeights = weights;
        Animation::SampleWeights(animation, nodeIndex, track.phase * animation->duration, clipWeights);
        for (size_t i = 0; i < mixed.size() && i < clipWeights.size(); ++i) {
            mixed[i] += clipWeights[i] * clip.weight;
        }
        total += clip.weight;
    }
    if (total > 0.0f) {
        for (size_t i = 0; i < weights.size(); ++i) {
            weights[i] = mixed[i] / total;
        }
    }
}

void SkinnedObject3d::SamplePlayerPose(const TrackPlayer& player, int32_t nodeIndex, NodePose& pose) const {
    SampleTrackPose(player.current, nodeIndex, pose);
    if (!player.isBlending) {
        return;
    }
    // つなぐ元の姿勢と混ぜる（blendRate が 0 でつなぐ元、1 で今のアニメーション）
    NodePose from = player.blendFromPose[static_cast<size_t>(nodeIndex)];
    if (player.blendFrom.index >= 0) {
        SampleTrackPose(player.blendFrom, nodeIndex, from);
    }
    pose.translate = Lerp(from.translate, pose.translate, player.blendRate);
    pose.rotate = Calculation::Slerp(from.rotate, pose.rotate, player.blendRate);
    pose.scale = Lerp(from.scale, pose.scale, player.blendRate);
}

void SkinnedObject3d::UpdatePose() {
    for (Joint& joint : skeleton.GetJointsMutable()) {
        const size_t nodeIndex = static_cast<size_t>(joint.index);
        NodePose pose;
        SamplePlayerPose(base, joint.index, pose);
        // レイヤーを下から順に重ねる（その骨を上書きするレイヤーだけ、効き具合の分）
        for (const Layer& layer : layers) {
            const bool isPlaying = layer.player.current.index >= 0 || layer.player.isBlending;
            if (layer.weight <= 0.0f || !isPlaying || nodeIndex >= layer.mask.size() || !layer.mask[nodeIndex]) {
                continue;
            }
            NodePose layerPose;
            SamplePlayerPose(layer.player, joint.index, layerPose);
            pose.translate = Lerp(pose.translate, layerPose.translate, layer.weight);
            pose.rotate = Calculation::Slerp(pose.rotate, layerPose.rotate, layer.weight);
            pose.scale = Lerp(pose.scale, layerPose.scale, layer.weight);
        }
        joint.transform.translate = pose.translate;
        joint.rotate = pose.rotate;
        joint.transform.scale = pose.scale;
    }
}

void SkinnedObject3d::UpdateMorphWeights() {
    for (auto& [nodeIndex, applied] : appliedMorphWeights) {
        // アニメーションの重み → 無ければ glTF の最初の重み
        const std::vector<float>& defaultWeights = gltfData->nodes[static_cast<size_t>(nodeIndex)].morphWeights;
        std::vector<float> weights = defaultWeights;
        SampleTrackWeights(base.current, nodeIndex, weights);
        if (base.isBlending) {
            std::vector<float> from = defaultWeights;
            if (base.blendFrom.index >= 0) {
                SampleTrackWeights(base.blendFrom, nodeIndex, from);
            } else if (auto it = blendFromMorphWeights.find(nodeIndex); it != blendFromMorphWeights.end()) {
                from = it->second;
            }
            for (size_t i = 0; i < weights.size(); ++i) {
                const float fromWeight = i < from.size() ? from[i] : 0.0f;
                weights[i] = fromWeight + (weights[i] - fromWeight) * base.blendRate;
            }
        }
        // 自分で決めた重みで上書き
        for (const auto& [targetIndex, weight] : morphWeightOverrides) {
            if (targetIndex < weights.size()) {
                weights[targetIndex] = weight;
            }
        }
        // 変わったときだけ頂点を作り直す（CPU で全頂点を計算するので）
        if (weights != applied) {
            ownModel->ApplyMorphWeights(nodeIndex, weights);
            applied = std::move(weights);
        }
    }
}

void SkinnedObject3d::Update() {
    firedEvents.clear();
    if (gltfData) {
        const float deltaTime = Time::GetDeltaTime() * animationSpeed;
        AdvancePlayer(base, deltaTime, true);
        for (Layer& layer : layers) {
            UpdateLayerWeight(layer, deltaTime);
            // 効いていないレイヤーの合図は知らせない
            AdvancePlayer(layer.player, deltaTime, layer.weight > 0.0f);
        }

        UpdatePose();
        UpdateMorphWeights();
        skeleton.Update();
        skinCluster.Update(skeleton);
        SetBonePalette(skinCluster.GetPaletteResource());
    }

    Object3d::Update();

    // 合図を知らせる（関数の中で Play〜 されても大丈夫なように、姿勢を決め終わってから。コピーを回す）
    if (onAnimationEvent && !firedEvents.empty()) {
        const std::vector<std::string> events = firedEvents;
        for (const std::string& eventName : events) {
            onAnimationEvent(eventName);
        }
    }
}
