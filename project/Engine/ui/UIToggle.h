#pragma once
#include "UIElement.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

class Sprite;
class UILabel;

/// <summary>
/// ON / OFF を切り替えるチェックボックス（左に四角、右に文字）。文字の部分を押しても切り替わる。
/// キー・パッドでは決定で切り替わる。
///
///   UIToggle* fullscreen = list->CreateChild<UIToggle>("フルスクリーン", true);
///   fullscreen->SetOnValueChanged([](bool isOn) { ... });
///
/// SetOnValueChanged の関数は、操作で切り替わったときだけ呼ばれる（SetOn では呼ばれない）。
/// </summary>
class UIToggle : public UIElement {
public:
    struct Colors {
        Vector4 box = { 0.10f, 0.11f, 0.14f, 1.0f };        // 四角
        Vector4 boxActive = { 0.22f, 0.25f, 0.32f, 1.0f };  // 四角（乗った・選ばれた）
        Vector4 check = { 0.35f, 0.60f, 0.95f, 1.0f };      // ON のときの中の印
    };

    explicit UIToggle(const std::string& text = "", bool isOn = false);
    ~UIToggle() override;

    // ON / OFF を決める（SetOnValueChanged の関数は呼ばない）
    void SetOn(bool on) { isOn = on; }
    bool IsOn() const { return isOn; }
    void SetText(const std::string& newText);
    // 文字の色（押せないときは半透明になる）
    void SetTextColor(const Vector4& newColor) { textColor = newColor; }
    UILabel* GetLabel() const { return label; }

    // 操作で切り替わったときに呼ぶ関数
    void SetOnValueChanged(std::function<void(bool)> callback) { onValueChanged = std::move(callback); }
    // このフレームの canvas.Update() で操作によって切り替わったか
    bool IsValueChanged() const;

    void SetColors(const Colors& newColors) { colors = newColors; }

protected:
    void OnInitialize() override;
    void OnUpdate() override;
    void OnDraw() override;
    void OnClick() override;
    void OnArrangeChildren() override;

private:
    std::unique_ptr<Sprite> box;
    std::unique_ptr<Sprite> check;
    UILabel* label = nullptr;
    // OnInitialize の前に SetText されたときの文字
    std::string pendingText;
    Colors colors;
    Vector4 textColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    bool isOn = false;
    std::function<void(bool)> onValueChanged;
    uint64_t changedUpdateCount = 0;
    bool hasChanged = false;
};
