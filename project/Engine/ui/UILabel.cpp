#include "UILabel.h"
#include "Text.h"
#include <cmath>

namespace {
    // 複数行のときの行揃え（文字を置く位置の x に合わせる）
    Text::Align ToTextAlign(float anchorX) {
        if (anchorX < 0.25f) {
            return Text::Align::kLeft;
        }
        if (anchorX > 0.75f) {
            return Text::Align::kRight;
        }
        return Text::Align::kCenter;
    }
}

UILabel::UILabel(const std::string& text) : text(text) {
    SetBlocksPointer(false);
    SetSize({ 0.0f, 0.0f });
}

UILabel::~UILabel() = default;

void UILabel::SetText(const std::string& newText) {
    text = newText;
    if (textObject) {
        textObject->SetText(text); // 同じ文字列なら作り直さない
    }
}

void UILabel::SetFontSize(float newFontSize) {
    SetFont(fontName, newFontSize, isBold);
}

void UILabel::SetFont(const std::wstring& newFontName, float newFontSize, bool newBold) {
    fontName = newFontName;
    fontSize = newFontSize;
    isBold = newBold;
    if (textObject) {
        textObject->SetFont(fontName, fontSize, isBold);
    }
}

Vector2 UILabel::GetTextSize() const {
    return textObject ? textObject->GetSize() : Vector2{ 0.0f, 0.0f };
}

void UILabel::OnInitialize() {
    textObject = std::make_unique<Text>();
    textObject->Initialize(GetSpriteCommon(), fontSize, fontName, isBold);
    textObject->SetText(text);
}

void UILabel::OnBeforeLayout() {
    if (!isAutoSize || !textObject) {
        return;
    }
    // 配置の計算の前に文字の画像を作り直して大きさを決める（並べる親が、変わった大きさでこのフレームから並べられる）
    textObject->SetAlign(ToTextAlign(textAnchor.x));
    textObject->Update();
    SetSize(textObject->GetSize());
}

void UILabel::OnUpdate() {
    textObject->SetAlign(ToTextAlign(textAnchor.x));
    // 文字・フォントが変わっていればここで画像を作り直す（大きさもここで決まる）
    textObject->Update();

    if (isAutoSize) {
        const Vector2 textSize = textObject->GetSize();
        if (textSize.x != GetSize().x || textSize.y != GetSize().y) {
            SetSize(textSize);
            RecalculateLayout(); // 大きさが変わったら、このフレームのうちに位置を直す
        }
    }

    // 文字の画像は半端な位置だとにじむので、左上が整数になるように置く
    const Rect& rect = GetWorldRect();
    const Vector2 worldSize = GetWorldSize();
    const Vector2 textSize = textObject->GetSize();
    const Vector2 leftTop = {
        std::round(rect.min.x + (worldSize.x - textSize.x) * textAnchor.x),
        std::round(rect.min.y + (worldSize.y - textSize.y) * textAnchor.y),
    };
    textObject->SetAnchorPoint({ 0.0f, 0.0f });
    textObject->SetPosition(leftTop);
    textObject->SetColor(color);
    textObject->SetLayer(GetDrawLayer());
    textObject->Update();
}

void UILabel::OnDraw() {
    textObject->Draw();
}
