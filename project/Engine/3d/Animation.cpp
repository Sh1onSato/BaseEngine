#include "Animation.h"
#include "Calculation.h"

namespace {
    // Vector3の線形補間（Calculation.hにはQuaternion用のSlerpしか無いため自前で用意する）
    Vector3 Lerp(const Vector3& a, const Vector3& b, float t) {
        return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
    }

    // keys の time の値を求める（keys が空なら fallback）。時間が範囲外ならクランプ（端のキー値をそのまま返す）。
    // kStep なら次のキーまで前のキーの値のまま、kLinear なら interpolate でつなぐ
    template <typename Key, typename Value, typename Interpolate>
    Value SampleKeys(const std::vector<Key>& keys, float time, GltfInterpolation interpolation, const Value& fallback, Interpolate interpolate) {
        if (keys.empty()) { return fallback; }
        if (time <= keys.front().time) { return keys.front().value; }
        if (time >= keys.back().time) { return keys.back().value; }

        for (size_t i = 0; i + 1 < keys.size(); ++i) {
            if (time >= keys[i].time && time <= keys[i + 1].time) {
                if (interpolation == GltfInterpolation::kStep) {
                    return keys[i].value;
                }
                float span = keys[i + 1].time - keys[i].time;
                float t = (span > 0.0f) ? (time - keys[i].time) / span : 0.0f;
                return interpolate(keys[i].value, keys[i + 1].value, t);
            }
        }
        return keys.back().value;
    }
}

void Animation::SampleNode(const GltfAnimation* animation, int32_t nodeIndex, const GltfNode& restNode, float time,
                           Vector3& translate, Quaternion& rotate, Vector3& scale) {
    translate = restNode.translate;
    rotate = restNode.rotate;
    scale = restNode.scale;
    if (!animation) {
        return;
    }
    auto it = animation->nodeAnimations.find(nodeIndex);
    if (it == animation->nodeAnimations.end()) {
        return;
    }
    const GltfNodeAnimation& channels = it->second;
    translate = SampleKeys(channels.translate, time, channels.translateInterpolation, restNode.translate, Lerp);
    rotate = SampleKeys(channels.rotate, time, channels.rotateInterpolation, restNode.rotate, Calculation::Slerp);
    scale = SampleKeys(channels.scale, time, channels.scaleInterpolation, restNode.scale, Lerp);
}

bool Animation::SampleWeights(const GltfAnimation* animation, int32_t nodeIndex, float time, std::vector<float>& weights) {
    if (!animation) {
        return false;
    }
    auto it = animation->weightsAnimations.find(nodeIndex);
    if (it == animation->weightsAnimations.end() || it->second.times.empty()) {
        return false;
    }
    const GltfWeightsAnimation& channel = it->second;
    const std::vector<float>& times = channel.times;

    // time を挟む2つのキーと、その間の割合 t（範囲外は端のキー）
    size_t from = 0;
    size_t to = 0;
    float t = 0.0f;
    if (time >= times.back()) {
        from = to = times.size() - 1;
    } else if (time > times.front()) {
        while (from + 1 < times.size() && times[from + 1] < time) { ++from; }
        to = from + 1;
        const float span = times[to] - times[from];
        t = (channel.interpolation == GltfInterpolation::kStep || span <= 0.0f) ? 0.0f : (time - times[from]) / span;
    }
    const std::vector<float>& a = channel.weights[from];
    const std::vector<float>& b = channel.weights[to];
    weights.resize(a.size());
    for (size_t i = 0; i < a.size(); ++i) {
        const float next = i < b.size() ? b[i] : a[i];
        weights[i] = a[i] + (next - a[i]) * t;
    }
    return true;
}
