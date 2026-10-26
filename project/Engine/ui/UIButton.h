#pragma once
#include "UIPanel.h"
#include <cstdint>
#include <functional>
#include <string>

class UILabel;

/// <summary>
/// 押せるボタン。背景（UIPanel）と、中央の文字（子の UILabel）でできている。
/// 押して同じボタンの上で離すとクリック（SetOnClick の関数が呼ばれ、そのフレームは IsClicked() が true）。
/// SetInteractable(false) で押せなくなり、灰色になる。キー・パッドでも選べて、決定（Enter・パッド A）で押せる。
///
///   UIButton* start = canvas.Create<UIButton>("はじめる");
///   start->SetAlignment({ 0.5f, 0.5f });
///   start->SetSize({ 240.0f, 56.0f });
///   start->SetOnClick([this]() { sceneManager->ChangeScene("GAME", 0.5f); });
///   // 関数を渡す代わりに、canvas.Update() の後で if (start->IsClicked()) { ... } と調べてもよい
/// </summary>
class UIButton : public UIPanel {
public:
    // 状態ごとの背景の色（SetColor の色・画像に掛け算される）
    struct Colors {
        Vector4 normal = { 0.22f, 0.27f, 0.36f, 1.0f };
        Vector4 hover = { 0.32f, 0.40f, 0.55f, 1.0f };
        Vector4 pressed = { 0.14f, 0.17f, 0.24f, 1.0f };
        Vector4 disabled = { 0.22f, 0.22f, 0.22f, 0.6f };
    };

    explicit UIButton(const std::string& text = "");

    void SetText(const std::string& newText);
    const std::string& GetText() const;
    // 文字の色（押せないときは半透明になる）
    void SetTextColor(const Vector4& newColor) { textColor = newColor; }
    // 文字の部品（フォントの変更などに使う）
    UILabel* GetLabel() const { return label; }

    // クリックされたときに呼ぶ関数
    void SetOnClick(std::function<void()> callback) { onClick = std::move(callback); }
    // このフレームの canvas.Update() でクリックされたか
    bool IsClicked() const;

    void SetColors(const Colors& newColors) { colors = newColors; }
    const Colors& GetColors() const { return colors; }

protected:
    void OnInitialize() override;
    void OnUpdate() override;
    void OnClick() override;
    Vector4 GetDrawColor() const override;

private:
    UILabel* label = nullptr;
    // OnInitialize の前に SetText されたときの文字
    std::string pendingText;
    Vector4 textColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    Colors colors;
    std::function<void()> onClick;
    // クリックされたときのキャンバスの Update 回数（IsClicked 用）
    uint64_t clickedUpdateCount = 0;
    bool hasBeenClicked = false;
};
