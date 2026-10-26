#pragma once
#include "UIElement.h"
#include <memory>
#include <string>

class Text;

/// <summary>
/// 文字（日本語・改行可）を表示する UI 部品。中身は Text（GDI で任意フォント）。マウスは受け止めない。
/// 既定では大きさが文字に合わせて自動で決まる（SetAutoSize(false) にすると SetSize / SetStretch の大きさの中に置く）。
///
///   UILabel* label = canvas.Create<UILabel>("スコア: 0");
///   label->SetAlignment({ 0.0f, 0.0f });
///   label->SetPosition({ 20.0f, 20.0f });
///   label->SetFontSize(28.0f);
/// </summary>
class UILabel : public UIElement {
public:
    explicit UILabel(const std::string& text = "");
    ~UILabel() override;

    void SetText(const std::string& newText);
    const std::string& GetText() const { return text; }
    void SetFontSize(float newFontSize);
    // fontName: L"Yu Gothic UI", L"Meiryo", L"MS Gothic" など
    void SetFont(const std::wstring& newFontName, float newFontSize, bool newBold = false);
    void SetColor(const Vector4& newColor) { color = newColor; }
    const Vector4& GetColor() const { return color; }
    // 自分の矩形の中のどこに文字を置くか（0〜1。{0,0.5}=左寄せ、{0.5,0.5}=中央、{1,0.5}=右寄せ）。
    // 複数行のときの行揃えも x に合わせる（0=左 / 0.5=中央 / 1=右）
    void SetTextAnchor(const Vector2& anchor) { textAnchor = anchor; }
    // true（既定）なら大きさを文字に合わせる
    void SetAutoSize(bool enable) { isAutoSize = enable; }
    // 文字を描いたときの大きさ（px）
    Vector2 GetTextSize() const;

protected:
    void OnInitialize() override;
    void OnBeforeLayout() override;
    void OnUpdate() override;
    void OnDraw() override;

private:
    std::unique_ptr<Text> textObject;
    std::string text;
    std::wstring fontName = L"Yu Gothic UI";
    float fontSize = 24.0f;
    bool isBold = false;
    Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
    Vector2 textAnchor = { 0.5f, 0.5f };
    bool isAutoSize = true;
};
