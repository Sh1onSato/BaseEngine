#include "SkinnedObject3d.h"
#include "Calculation.h"
#include "GameTime.h"
#include "Logger.h"
#include <algorithm>
#include <cmath>

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

    if (GetAnimationCount() > 0) {
        PlayAnimation(size_t{ 0 }, isLooping);
    } else {
        StopAnimation();
    }
}

void SkinnedObject3d::PlayAnimation(const std::string& name, bool loop) {
    for (size_t i = 0; i < GetAnimationCount(); ++i) {
        if (gltfData->animations[i].name == name) {
            PlayAnimation(i, loop);
            return;
        }
    }
    Logger::Log("SkinnedObject3d: アニメーションが見つかりません: " + name + "\n");
}

void SkinnedObject3d::PlayAnimation(size_t index, bool loop) {
    if (index >= GetAnimationCount()) {
        return;
    }
    currentAnimationIndex = static_cast<int32_t>(index);
    isLooping = loop;
    animationTime = 0.0f;
    isFinished = false;
}

void SkinnedObject3d::StopAnimation() {
    currentAnimationIndex = -1;
    animationTime = 0.0f;
    isFinished = false;
}

void SkinnedObject3d::SetAnimationSpeed(float speed) {
    animationSpeed = (std::max)(speed, 0.0f);
}

void SkinnedObject3d::SetAnimationTime(float time) {
    animationTime = (std::max)(time, 0.0f);
    isFinished = false;
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

void SkinnedObject3d::UpdateMorphWeights(const GltfAnimation* animation) {
    for (auto& [nodeIndex, applied] : appliedMorphWeights) {
        // アニメーションの重み → 無ければ glTF の最初の重み → 自分で決めた重みで上書き
        std::vector<float> weights = gltfData->nodes[static_cast<size_t>(nodeIndex)].morphWeights;
        Animation::SampleWeights(animation, nodeIndex, animationTime, weights);
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

bool SkinnedObject3d::GetJointWorldMatrix(const std::string& jointName, Matrix4x4& outMatrix) const {
    const int32_t jointIndex = skeleton.FindJoint(jointName);
    if (jointIndex < 0) {
        return false;
    }
    // モデルの中での行列 * このオブジェクトのワールド行列（行ベクトル）
    outMatrix = skeleton.GetJoints()[static_cast<size_t>(jointIndex)].skeletonSpaceMatrix * GetWorldMatrix();
    return true;
}

void SkinnedObject3d::AdvanceAnimationTime() {
    if (currentAnimationIndex < 0 || isFinished) {
        return;
    }
    const float duration = gltfData->animations[static_cast<size_t>(currentAnimationIndex)].duration;
    animationTime += Time::GetDeltaTime() * animationSpeed;
    if (duration <= 0.0f) {
        return;
    }
    if (isLooping) {
        animationTime = std::fmod(animationTime, duration);
    } else if (animationTime >= duration) {
        animationTime = duration;
        isFinished = true;
    }
}

void SkinnedObject3d::Update() {
    if (gltfData) {
        AdvanceAnimationTime();
        const GltfAnimation* animation = currentAnimationIndex >= 0
            ? &gltfData->animations[static_cast<size_t>(currentAnimationIndex)] : nullptr;

        // 全部のノードを今の時刻の姿勢にする（このアニメーションで動かないノードはレストポーズに戻る）
        for (Joint& joint : skeleton.GetJointsMutable()) {
            Animation::SampleNode(animation, joint.index, gltfData->nodes[static_cast<size_t>(joint.index)], animationTime,
                                  joint.transform.translate, joint.rotate, joint.transform.scale);
        }

        UpdateMorphWeights(animation);
        skeleton.Update();
        skinCluster.Update(skeleton);
        SetBonePalette(skinCluster.GetPaletteResource());
    }

    Object3d::Update();
}
