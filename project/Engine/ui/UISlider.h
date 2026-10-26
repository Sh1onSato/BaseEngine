#pragma once
#include "UIElement.h"
#include <cstdint>
#include <functional>
#include <memory>

class Sprite;

/// <summary>
/// 横向きのスライダー（音量・明るさなど）。つまみをドラッグするか、溝をクリックした位置に動く。
/// キー・パッドで選んでいるときは左右で動かせる（上下は次の部品へ）。
///
///   UISlider* volume = list->CreateChild<UISlider>(0.0f, 1.0f, 0.8f);   // 最小・最大・最初の値
///   volume->SetSize({ 240.0f, 28.0f });
///   volume->SetOnValueChanged([](float value) { AudioManager::GetInstance()->SetMasterVolume(value); });
///
/// SetOnValueChanged の関数は、操作で値が変わったときだけ呼ばれる（SetValue では呼ばれない）。
/// </summary>
class UISlider : public UIElement {
public:
    struct Colors {
        Vector4 track = { 0.10f, 0.11f, 0.14f, 1.0f };      // 溝
        Vector4 fill = { 0.35f, 0.60f, 0.95f, 1.0f };       // 溝の値までの部分
        Vector4 handle = { 0.80f, 0.82f, 0.88f, 1.0f };     // つまみ
        Vector4 handleActive = { 1.0f, 1.0f, 1.0f, 1.0f };  // つまみ（乗った・押した・選ばれた）
    };

    UISlider(float minValue = 0.0f, float maxValue = 1.0f, float value = 0.0f);
    ~UISlider() override;

    void SetRange(float newMinValue, float newMaxValue);
    float GetMinValue() const { return minValue; }
    float GetMaxValue() const { return maxValue; }
    // 値を決める（範囲と刻みに合わせて丸める。SetOnValueChanged の関数は呼ばない）
    void SetValue(float newValue);
    float GetValue() const { return value; }
    // 0〜1 で表した値
    float GetRatio() const;
    // 刻み（0 でなめらか。例: 1 なら整数だけ）
    void SetStep(float newStep) { step = newStep; }
    // 左右キー1回で動く量（0 なら刻み、刻みも 0 なら範囲の 1/20）
    void SetKeyStep(float newKeyStep) { keyStep = newKeyStep; }

    // 操作で値が変わったときに呼ぶ関数
    void SetOnValueChanged(std::function<void(float)> callback) { onValueChanged = std::move(callback); }
    // このフレームの canvas.Update() で操作によって値が変わったか
    bool IsValueChanged() const;

    void SetColors(const Colors& newColors) { colors = newColors; }

protected:
    void OnInitialize() override;
    void OnUpdate() override;
    void OnDraw() override;
    void OnPointerDown() override;
    bool OnNavigate(UINavigation direction) override;

private:
    // 範囲と刻みに合わせる
    float Normalize(float rawValue) const;
    // 操作で値を変える（変わったら関数を呼ぶ）
    void ChangeValueByUser(float newValue);
    // マウスの位置の値
    float GetValueAtPointer() const;
    float GetHandleWidth() const;

    std::unique_ptr<Sprite> track;
    std::unique_ptr<Sprite> fill;
    std::unique_ptr<Sprite> handle;
    Colors colors;

    float minValue = 0.0f;
    float maxValue = 1.0f;
    float value = 0.0f;
    float step = 0.0f;
    float keyStep = 0.0f;
    std::function<void(float)> onValueChanged;
    uint64_t changedUpdateCount = 0;
    bool hasChanged = false;
};
