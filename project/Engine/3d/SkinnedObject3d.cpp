#include "SkinnedObject3d.h"
#include "Calculation.h"
#include "GameTime.h"
#include "Logger.h"
#include <algorithm>
#include <cmath>

namespace {
    Vector3 Lerp(const Vector3& a, const Vector3& b, float t) {
        return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
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

    isBlending = false;
    blendFromPose.assign(this->gltfData->nodes.size(), NodePose{});
    blendFromMorphWeights.clear();
    if (GetAnimationCount() > 0) {
        PlayAnimation(size_t{ 0 }, current.isLooping);
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
    BeginBlend(blendSeconds);
    current = { static_cast<int32_t>(index), 0.0f, loop, false };
}

void SkinnedObject3d::PlayBlend(const std::string& first, const std::string& second, float rate, float blendSeconds) {
    const int32_t firstIndex = FindAnimation(first);
    const int32_t secondIndex = FindAnimation(second);
    if (firstIndex < 0 || secondIndex < 0) {
        Logger::Log("SkinnedObject3d: 混ぜるアニメーションが見つかりません: " + first + " / " + second + "\n");
        return;
    }
    BeginBlend(blendSeconds);
    current = AnimationTrack{};
    current.index = firstIndex;
    current.secondIndex = secondIndex;
    current.secondRate = std::clamp(rate, 0.0f, 1.0f);
    current.isLooping = true;
}

void SkinnedObject3d::SetBlendRate(float rate) {
    current.secondRate = std::clamp(rate, 0.0f, 1.0f);
}

void SkinnedObject3d::StopAnimation(float blendSeconds) {
    BeginBlend(blendSeconds);
    current = { -1, 0.0f, current.isLooping, false };
}

void SkinnedObject3d::BeginBlend(float blendSeconds) {
    if (blendSeconds <= 0.0f || !gltfData) {
        isBlending = false;
        return;
    }
    if (isBlending || current.index < 0) {
        // つないでいる途中か停止中なら、今の見た目をそのまま止めた形からつなぐ（2本以上を同時に混ぜない）
        for (const Joint& joint : skeleton.GetJoints()) {
            NodePose& pose = blendFromPose[static_cast<size_t>(joint.index)];
            pose.translate = joint.transform.translate;
            pose.rotate = joint.rotate;
            pose.scale = joint.transform.scale;
        }
        blendFromMorphWeights = appliedMorphWeights;
        blendFrom = { -1, 0.0f, false, false };
    } else {
        // 再生中のアニメーションは、つなぐ間も動かし続ける
        blendFrom = current;
    }
    isBlending = true;
    blendElapsed = 0.0f;
    blendDuration = blendSeconds;
}

void SkinnedObject3d::SetAnimationSpeed(float speed) {
    animationSpeed = (std::max)(speed, 0.0f);
}

void SkinnedObject3d::SetAnimationTime(float time) {
    if (IsPlayingBlend()) {
        current.phase = std::clamp(time, 0.0f, 1.0f);
        return;
    }
    current.time = (std::max)(time, 0.0f);
    current.isFinished = false;
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

const GltfAnimation* SkinnedObject3d::GetAnimation(int32_t index) const {
    if (index < 0 || static_cast<size_t>(index) >= GetAnimationCount()) {
        return nullptr;
    }
    return &gltfData->animations[static_cast<size_t>(index)];
}

void SkinnedObject3d::AdvanceTrack(AnimationTrack& track, float deltaTime) const {
    const GltfAnimation* animation = GetAnimation(track.index);
    if (!animation || track.isFinished) {
        return;
    }
    if (const GltfAnimation* second = GetAnimation(track.secondIndex)) {
        // 混ぜるときは、割合に合わせた1周の長さで進み具合（0〜1）を進める
        const float cycle = animation->duration + (second->duration - animation->duration) * track.secondRate;
        if (cycle > 0.0f) {
            track.phase = std::fmod(track.phase + deltaTime / cycle, 1.0f);
        }
        return;
    }
    track.time += deltaTime;
    if (animation->duration <= 0.0f) {
        return;
    }
    if (track.isLooping) {
        track.time = std::fmod(track.time, animation->duration);
    } else if (track.time >= animation->duration) {
        track.time = animation->duration;
        track.isFinished = true;
    }
}

void SkinnedObject3d::SampleTrackPose(const AnimationTrack& track, int32_t nodeIndex, NodePose& pose) const {
    // このアニメーションで動かないノードはレストポーズ
    const GltfNode& restNode = gltfData->nodes[static_cast<size_t>(nodeIndex)];
    const GltfAnimation* animation = GetAnimation(track.index);
    const GltfAnimation* second = GetAnimation(track.secondIndex);
    if (!second) {
        Animation::SampleNode(animation, nodeIndex, restNode, track.time, pose.translate, pose.rotate, pose.scale);
        return;
    }
    // 2本とも1周のうちの同じ割合の時刻で姿勢を取り、割合で混ぜる
    const float firstTime = animation ? track.phase * animation->duration : 0.0f;
    NodePose secondPose;
    Animation::SampleNode(animation, nodeIndex, restNode, firstTime, pose.translate, pose.rotate, pose.scale);
    Animation::SampleNode(second, nodeIndex, restNode, track.phase * second->duration, secondPose.translate, secondPose.rotate, secondPose.scale);
    pose.translate = Lerp(pose.translate, secondPose.translate, track.secondRate);
    pose.rotate = Calculation::Slerp(pose.rotate, secondPose.rotate, track.secondRate);
    pose.scale = Lerp(pose.scale, secondPose.scale, track.secondRate);
}

void SkinnedObject3d::SampleTrackWeights(const AnimationTrack& track, int32_t nodeIndex, std::vector<float>& weights) const {
    // weights には glTF の最初の重みを入れて呼ぶ（アニメーションに重みが無ければそのまま）
    const GltfAnimation* animation = GetAnimation(track.index);
    const GltfAnimation* second = GetAnimation(track.secondIndex);
    if (!second) {
        Animation::SampleWeights(animation, nodeIndex, track.time, weights);
        return;
    }
    std::vector<float> secondWeights = weights;
    Animation::SampleWeights(animation, nodeIndex, animation ? track.phase * animation->duration : 0.0f, weights);
    Animation::SampleWeights(second, nodeIndex, track.phase * second->duration, secondWeights);
    for (size_t i = 0; i < weights.size() && i < secondWeights.size(); ++i) {
        weights[i] += (secondWeights[i] - weights[i]) * track.secondRate;
    }
}

void SkinnedObject3d::UpdatePose(float blendRate) {
    for (Joint& joint : skeleton.GetJointsMutable()) {
        const size_t nodeIndex = static_cast<size_t>(joint.index);
        NodePose pose;
        SampleTrackPose(current, joint.index, pose);
        if (isBlending) {
            // つなぐ元の姿勢と混ぜる（blendRate が 0 でつなぐ元、1 で今のアニメーション）
            NodePose from = blendFromPose[nodeIndex];
            if (blendFrom.index >= 0) {
                SampleTrackPose(blendFrom, joint.index, from);
            }
            pose.translate = Lerp(from.translate, pose.translate, blendRate);
            pose.rotate = Calculation::Slerp(from.rotate, pose.rotate, blendRate);
            pose.scale = Lerp(from.scale, pose.scale, blendRate);
        }
        joint.transform.translate = pose.translate;
        joint.rotate = pose.rotate;
        joint.transform.scale = pose.scale;
    }
}

void SkinnedObject3d::UpdateMorphWeights(float blendRate) {
    for (auto& [nodeIndex, applied] : appliedMorphWeights) {
        // アニメーションの重み → 無ければ glTF の最初の重み
        const std::vector<float>& defaultWeights = gltfData->nodes[static_cast<size_t>(nodeIndex)].morphWeights;
        std::vector<float> weights = defaultWeights;
        SampleTrackWeights(current, nodeIndex, weights);
        if (isBlending) {
            std::vector<float> from = defaultWeights;
            if (blendFrom.index >= 0) {
                SampleTrackWeights(blendFrom, nodeIndex, from);
            } else if (auto it = blendFromMorphWeights.find(nodeIndex); it != blendFromMorphWeights.end()) {
                from = it->second;
            }
            for (size_t i = 0; i < weights.size(); ++i) {
                const float fromWeight = i < from.size() ? from[i] : 0.0f;
                weights[i] = fromWeight + (weights[i] - fromWeight) * blendRate;
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
    if (gltfData) {
        const float deltaTime = Time::GetDeltaTime() * animationSpeed;
        AdvanceTrack(current, deltaTime);

        // つないでいる間の混ぜ具合（始めと終わりをゆっくりにする）
        float blendRate = 1.0f;
        if (isBlending) {
            AdvanceTrack(blendFrom, deltaTime);
            blendElapsed += deltaTime;
            if (blendElapsed >= blendDuration) {
                isBlending = false;
            } else {
                const float t = blendElapsed / blendDuration;
                blendRate = t * t * (3.0f - 2.0f * t);
            }
        }

        UpdatePose(blendRate);
        UpdateMorphWeights(blendRate);
        skeleton.Update();
        skinCluster.Update(skeleton);
        SetBonePalette(skinCluster.GetPaletteResource());
    }

    Object3d::Update();
}
