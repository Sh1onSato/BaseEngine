#include "UIToggle.h"
#include "Sprite.h"
#include "UICanvas.h"
#include "UILabel.h"

namespace {
    const Vector2 kDefaultToggleSize = { 240.0f, 32.0f };
    // 四角と文字の間
    constexpr float kLabelGap = 10.0f;
    // ON の印の、四角の端からの内側の余白（四角の大きさに対する割合）
    constexpr float kCheckInsetRatio = 0.25f;
    constexpr float kDisabledAlpha = 0.45f;

    Vector4 WithAlpha(Vector4 color, float alphaScale) {
        color.w *= alphaScale;
        return color;
    }
}

UIToggle::UIToggle(const std::string& text, bool isOn) : pendingText(text), isOn(isOn) {
    SetSize(kDefaultToggleSize);
    SetBlocksPointer(true);
    SetFocusable(true);
}

UIToggle::~UIToggle() = default;

void UIToggle::SetText(const std::string& newText) {
    if (label) {
        label->SetText(newText);
    } else {
        pendingText = newText;
    }
}

bool UIToggle::IsValueChanged() const {
    return hasChanged && GetCanvas() && changedUpdateCount == GetCanvas()->GetUpdateCount();
}

void UIToggle::OnInitialize() {
    for (std::unique_ptr<Sprite>* sprite : { &box, &check }) {
        *sprite = std::make_unique<Sprite>();
        (*sprite)->Initialize(GetSpriteCommon(), "white");
        (*sprite)->SetAnchorPoint({ 0.0f, 0.0f });
    }
    label = CreateChild<UILabel>(pendingText);
    label->SetAlignment({ 0.0f, 0.5f });
    label->SetTextAnchor({ 0.0f, 0.5f });
}

void UIToggle::OnUpdate() {
    const Rect& rect = GetWorldRect();
    const float boxSize = GetWorldSize().y;
    const float inset = boxSize * kCheckInsetRatio;
    const bool isActive = IsHovered() || (IsFocused() && GetCanvas()->IsFocusVisible());
    const float alpha = IsInteractableInHierarchy() ? 1.0f : kDisabledAlpha;

    box->SetPosition(rect.min);
    box->SetSize({ boxSize, boxSize });
    box->SetColor(WithAlpha(isActive ? colors.boxActive : colors.box, alpha));
    check->SetPosition({ rect.min.x + inset, rect.min.y + inset });
    check->SetSize({ boxSize - inset * 2.0f, boxSize - inset * 2.0f });
    check->SetColor(WithAlpha(colors.check, alpha));
    for (Sprite* sprite : { box.get(), check.get() }) {
        sprite->SetLayer(GetDrawLayer());
        sprite->Update();
    }
    label->SetColor(WithAlpha(textColor, alpha));
}

void UIToggle::OnArrangeChildren() {
    // 文字は四角の右（四角の大きさ = 自分の高さが変わっても付いていく）
    if (label) {
        label->SetPosition({ GetWorldSize().y + kLabelGap, 0.0f });
    }
}

void UIToggle::OnDraw() {
    box->Draw();
    if (isOn) {
        check->Draw();
    }
}

void UIToggle::OnClick() {
    isOn = !isOn;
    hasChanged = true;
    changedUpdateCount = GetCanvas()->GetUpdateCount();
    if (onValueChanged) {
        onValueChanged(isOn);
    }
}
