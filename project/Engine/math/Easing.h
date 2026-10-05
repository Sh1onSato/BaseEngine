#pragma once
#include "Structs.h"
#include <cmath>
#include <numbers>

// イージング関数と補間。t は 0〜1 を想定（範囲外は Clamp01 してから渡すこと）。
// 例: float t = timer.GetProgress();
//     sprite->SetPosition(Easing::Lerp(start, end, Easing::OutCubic(t)));
namespace Easing {
    constexpr float kPi = std::numbers::pi_v<float>;

    inline float Clamp01(float t) { return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t); }

    // --- 補間 ---
    inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
    inline Vector2 Lerp(const Vector2& a, const Vector2& b, float t) { return { Lerp(a.x, b.x, t), Lerp(a.y, b.y, t) }; }
    inline Vector3 Lerp(const Vector3& a, const Vector3& b, float t) { return { Lerp(a.x, b.x, t), Lerp(a.y, b.y, t), Lerp(a.z, b.z, t) }; }
    inline Vector4 Lerp(const Vector4& a, const Vector4& b, float t) { return { Lerp(a.x, b.x, t), Lerp(a.y, b.y, t), Lerp(a.z, b.z, t), Lerp(a.w, b.w, t) }; }
    // valueがa〜bのどの位置か（Lerpの逆）
    inline float InverseLerp(float a, float b, float value) { return (b - a) != 0.0f ? (value - a) / (b - a) : 0.0f; }

    // フレームレートに依存しない指数減衰の追従（カメラ追従など）。
    // sharpnessが大きいほど速く追いつく。current = Damp(current, target, 10.0f, deltaTime);
    inline float Damp(float current, float target, float sharpness, float deltaTime) { return Lerp(current, target, 1.0f - std::exp(-sharpness * deltaTime)); }
    inline Vector3 Damp(const Vector3& current, const Vector3& target, float sharpness, float deltaTime) { return Lerp(current, target, 1.0f - std::exp(-sharpness * deltaTime)); }

    // --- イージング ---
    inline float Linear(float t) { return t; }

    inline float InSine(float t) { return 1.0f - std::cos(t * kPi * 0.5f); }
    inline float OutSine(float t) { return std::sin(t * kPi * 0.5f); }
    inline float InOutSine(float t) { return -(std::cos(kPi * t) - 1.0f) * 0.5f; }

    inline float InQuad(float t) { return t * t; }
    inline float OutQuad(float t) { return 1.0f - (1.0f - t) * (1.0f - t); }
    inline float InOutQuad(float t) { return t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) * 0.5f; }

    inline float InCubic(float t) { return t * t * t; }
    inline float OutCubic(float t) { return 1.0f - std::pow(1.0f - t, 3.0f); }
    inline float InOutCubic(float t) { return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f; }

    inline float InQuart(float t) { return t * t * t * t; }
    inline float OutQuart(float t) { return 1.0f - std::pow(1.0f - t, 4.0f); }
    inline float InOutQuart(float t) { return t < 0.5f ? 8.0f * t * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 4.0f) * 0.5f; }

    inline float InExpo(float t) { return t <= 0.0f ? 0.0f : std::pow(2.0f, 10.0f * t - 10.0f); }
    inline float OutExpo(float t) { return t >= 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t); }
    inline float InOutExpo(float t) {
        if (t <= 0.0f) return 0.0f;
        if (t >= 1.0f) return 1.0f;
        return t < 0.5f ? std::pow(2.0f, 20.0f * t - 10.0f) * 0.5f : (2.0f - std::pow(2.0f, -20.0f * t + 10.0f)) * 0.5f;
    }

    inline float InCirc(float t) { return 1.0f - std::sqrt(1.0f - t * t); }
    inline float OutCirc(float t) { return std::sqrt(1.0f - (t - 1.0f) * (t - 1.0f)); }

    // 少し行き過ぎてから戻る
    inline float InBack(float t) { constexpr float c1 = 1.70158f, c3 = c1 + 1.0f; return c3 * t * t * t - c1 * t * t; }
    inline float OutBack(float t) { constexpr float c1 = 1.70158f, c3 = c1 + 1.0f; return 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f); }
    inline float InOutBack(float t) {
        constexpr float c2 = 1.70158f * 1.525f;
        return t < 0.5f
            ? (std::pow(2.0f * t, 2.0f) * ((c2 + 1.0f) * 2.0f * t - c2)) * 0.5f
            : (std::pow(2.0f * t - 2.0f, 2.0f) * ((c2 + 1.0f) * (t * 2.0f - 2.0f) + c2) + 2.0f) * 0.5f;
    }

    // バネのように揺れて収まる
    inline float OutElastic(float t) {
        if (t <= 0.0f) return 0.0f;
        if (t >= 1.0f) return 1.0f;
        constexpr float c4 = (2.0f * kPi) / 3.0f;
        return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
    }

    // 弾むように着地する
    inline float OutBounce(float t) {
        constexpr float n1 = 7.5625f, d1 = 2.75f;
        if (t < 1.0f / d1) return n1 * t * t;
        if (t < 2.0f / d1) { t -= 1.5f / d1; return n1 * t * t + 0.75f; }
        if (t < 2.5f / d1) { t -= 2.25f / d1; return n1 * t * t + 0.9375f; }
        t -= 2.625f / d1;
        return n1 * t * t + 0.984375f;
    }
    inline float InBounce(float t) { return 1.0f - OutBounce(1.0f - t); }
}
