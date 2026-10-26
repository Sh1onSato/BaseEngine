#include "UIButton.h"
#include "UICanvas.h"
#include "UILabel.h"

namespace {
    const Vector2 kDefaultButtonSize = { 200.0f, 48.0f };
    constexpr float kDisabledTextAlpha = 0.5f;

    Vector4 Multiply(const Vector4& a, const Vector4& b) {
        return { a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w };
    }
}

UIButton::UIButton(const std::string& text) : pendingText(text) {
    SetSize(kDefaultButtonSize);
    SetFocusable(true);
}

void UIButton::SetText(const std::string& newText) {
    if (label) {
        label->SetText(newText);
    } else {
        pendingText = newText;
    }
}

const std::string& UIButton::GetText() const {
    return label ? label->GetText() : pendingText;
}

bool UIButton::IsClicked() const {
    return hasBeenClicked && GetCanvas() && clickedUpdateCount == GetCanvas()->GetUpdateCount();
}

void UIButton::OnInitialize() {
    UIPanel::OnInitialize();
    // 文字はボタンいっぱいの中央に置く
    label = CreateChild<UILabel>(pendingText);
    label->SetStretch(true, true);
    label->SetAutoSize(false);
    label->SetTextAnchor({ 0.5f, 0.5f });
}

void UIButton::OnUpdate() {
    UIPanel::OnUpdate();
    Vector4 color = textColor;
    if (!IsInteractableInHierarchy()) {
        color.w *= kDisabledTextAlpha;
    }
    label->SetColor(color);
}

void UIButton::OnClick() {
    hasBeenClicked = true;
    clickedUpdateCount = GetCanvas()->GetUpdateCount();
    if (onClick) {
        onClick();
    }
}

Vector4 UIButton::GetDrawColor() const {
    const Vector4* stateColor = &colors.normal;
    if (!IsInteractableInHierarchy()) {
        stateColor = &colors.disabled;
    } else if (IsPressed() && IsHovered()) {
        stateColor = &colors.pressed;
    } else if (IsHovered() || (IsFocused() && GetCanvas()->IsFocusVisible())) {
        stateColor = &colors.hover; // キー・パッドで選ばれているときも乗っているときと同じ色
    }
    return Multiply(*stateColor, GetColor());
}
