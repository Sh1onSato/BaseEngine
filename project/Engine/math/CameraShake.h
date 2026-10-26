#pragma once
#include "GameTime.h"
#include "Structs.h"
#include <cmath>
#include <numbers>

/// <summary>
/// カメラの揺れ（Camera::Shake / Camera2D::Shake が使う）。強さは時間とともに弱くなって 0 になる。
/// 実際の時間（Time::GetUnscaledTotalTime）で進むので、ヒットストップやポーズの間も揺れ続ける。
/// 揺れは何本かの sin を混ぜた物で、1 秒に frequency 回くらい行ったり来たりする。
/// </summary>
class CameraShake {
public:
    // strength: 一番強い時の揺れ幅、seconds: 揺れる秒数、frequency: 1 秒あたりの揺れの回数（大きいほど細かく速い）。
    // 揺れている途中で呼ぶと、今の揺れより強ければ新しい揺れにする（弱ければ何もしない）
    void Start(float newStrength, float newSeconds, float newFrequency) {
        if (GetAmplitude() > newStrength) {
            return;
        }
        startTime = Time::GetUnscaledTotalTime();
        strength = newStrength;
        seconds = newSeconds > 0.0f ? newSeconds : 0.0f;
        frequency = newFrequency;
    }
    void Stop() { seconds = 0.0f; }
    bool IsShaking() const { return GetAmplitude() > 0.0f; }

    // 今の揺れの強さ（strength から 0 へ。終わりに近いほどゆっくり弱まる）
    float GetAmplitude() const {
        if (seconds <= 0.0f) {
            return 0.0f;
        }
        const float t = static_cast<float>(Time::GetUnscaledTotalTime() - startTime) / seconds;
        if (t >= 1.0f) {
            return 0.0f;
        }
        const float remaining = 1.0f - t;
        return strength * remaining * remaining;
    }

    // 今の揺れ（x・y・z それぞれ -強さ〜強さ。軸ごとに違う揺れ方）
    Vector3 GetOffset() const {
        const float amplitude = GetAmplitude();
        if (amplitude <= 0.0f) {
            return { 0.0f, 0.0f, 0.0f };
        }
        const float time = static_cast<float>(Time::GetUnscaledTotalTime() - startTime);
        return { Wave(time, 0.0f) * amplitude, Wave(time, 2.1f) * amplitude, Wave(time, 4.3f) * amplitude };
    }

private:
    // 周期の違う2本の sin を混ぜた -1〜1（同じ形の繰り返しに見えにくい）
    float Wave(float time, float phase) const {
        constexpr float kTwoPi = std::numbers::pi_v<float> * 2.0f;
        const float angle = time * frequency * kTwoPi;
        return std::sin(angle + phase) * 0.6f + std::sin(angle * 1.73f + phase * 2.7f) * 0.4f;
    }

    double startTime = 0.0;
    float strength = 0.0f;
    float seconds = 0.0f;
    float frequency = 25.0f;
};
