#include "Animation.h"
#include "Calculation.h"

namespace {
    // Vector3の線形補間（Calculation.hにはQuaternion用のSlerpしか無いため自前で用意する）
    Vector3 Lerp(const Vector3& a, const Vector3& b, float t) {
        return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
    }
}

Animation Animation::Create(const GltfModelData& gltfData) {
    Animation animation;
    animation.duration = gltfData.animationDuration;
    animation.nodeAnimations = gltfData.nodeAnimations;

    // 各ノードのレストポーズ（アニメーションされないチャンネルのフォールバック用）を控えておく
    for (const GltfNode& node : gltfData.nodes) {
        RestPose restPose;
        restPose.translate = node.translate;
        restPose.rotate = node.rotate;
        restPose.scale = node.scale;
        animation.restPoses[node.name] = restPose;
    }
    return animation;
}

Vector3 Animation::SampleTranslate(const std::string& jointName, float time) const {
    Vector3 fallback = { 0.0f, 0.0f, 0.0f };
    if (auto restIt = restPoses.find(jointName); restIt != restPoses.end()) {
        fallback = restIt->second.translate;
    }

    auto it = nodeAnimations.find(jointName);
    if (it == nodeAnimations.end() || it->second.translate.empty()) {
        return fallback;
    }
    const std::vector<Vector3Key>& keys = it->second.translate;

    if (time <= keys.front().time) { return keys.front().value; }
    if (time >= keys.back().time) { return keys.back().value; }

    for (size_t i = 0; i + 1 < keys.size(); ++i) {
        if (time >= keys[i].time && time <= keys[i + 1].time) {
            float span = keys[i + 1].time - keys[i].time;
            float t = (span > 0.0f) ? (time - keys[i].time) / span : 0.0f;
            return Lerp(keys[i].value, keys[i + 1].value, t);
        }
    }
    return keys.back().value;
}

Quaternion Animation::SampleRotate(const std::string& jointName, float time) const {
    Quaternion fallback = Calculation::IdentityQuaternion();
    if (auto restIt = restPoses.find(jointName); restIt != restPoses.end()) {
        fallback = restIt->second.rotate;
    }

    auto it = nodeAnimations.find(jointName);
    if (it == nodeAnimations.end() || it->second.rotate.empty()) {
        return fallback;
    }
    const std::vector<QuaternionKey>& keys = it->second.rotate;

    if (time <= keys.front().time) { return keys.front().value; }
    if (time >= keys.back().time) { return keys.back().value; }

    for (size_t i = 0; i + 1 < keys.size(); ++i) {
        if (time >= keys[i].time && time <= keys[i + 1].time) {
            float span = keys[i + 1].time - keys[i].time;
            float t = (span > 0.0f) ? (time - keys[i].time) / span : 0.0f;
            return Calculation::Slerp(keys[i].value, keys[i + 1].value, t);
        }
    }
    return keys.back().value;
}

Vector3 Animation::SampleScale(const std::string& jointName, float time) const {
    Vector3 fallback = { 1.0f, 1.0f, 1.0f };
    if (auto restIt = restPoses.find(jointName); restIt != restPoses.end()) {
        fallback = restIt->second.scale;
    }

    auto it = nodeAnimations.find(jointName);
    if (it == nodeAnimations.end() || it->second.scale.empty()) {
        return fallback;
    }
    const std::vector<Vector3Key>& keys = it->second.scale;

    if (time <= keys.front().time) { return keys.front().value; }
    if (time >= keys.back().time) { return keys.back().value; }

    for (size_t i = 0; i + 1 < keys.size(); ++i) {
        if (time >= keys[i].time && time <= keys[i + 1].time) {
            float span = keys[i + 1].time - keys[i].time;
            float t = (span > 0.0f) ? (time - keys[i].time) / span : 0.0f;
            return Lerp(keys[i].value, keys[i + 1].value, t);
        }
    }
    return keys.back().value;
}
