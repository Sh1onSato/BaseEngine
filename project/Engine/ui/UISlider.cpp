#include "UISlider.h"
#include "Framework.h"
#include "Input.h"
#include "Sprite.h"
#include "UICanvas.h"
#include <algorithm>
#include <cmath>

namespace {
    const Vector2 kDefaultSliderSize = { 240.0f, 28.0f };
    // 溝の太さ（スライダーの高さに対する割合）
    constexpr float kTrackHeightRatio = 0.3f;
    // つまみの幅（スライダーの高さに対する割合）
    constexpr float kHandleWidthRatio = 0.6f;
    // 刻みが無いとき、左右キー1回で動く量（範囲に対する割合）
    constexpr float kDefaultKeyStepRatio = 1.0f / 20.0f;
    constexpr float kDisabledAlpha = 0.45f;

    Vector4 WithAlpha(Vector4 color, float alphaScale) {
        color.w *= alphaScale;
        return color;
    }

    std::unique_ptr<Sprite> CreateWhiteSprite(SpriteCommon* spriteCommon) {
        std::unique_ptr<Sprite> sprite = std::make_unique<Sprite>();
        sprite->Initialize(spriteCommon, "white");
        sprite->SetAnchorPoint({ 0.0f, 0.0f });
        return sprite;
    }
}

UISlider::UISlider(float minValue, float maxValue, float value) : minValue(minValue), maxValue(maxValue) {
    SetSize(kDefaultSliderSize);
    SetBlocksPointer(true);
    SetFocusable(true);
    this->value = Normalize(value);
}

UISlider::~UISlider() = default;

void UISlider::SetRange(float newMinValue, float newMaxValue) {
    minValue = newMinValue;
    maxValue = newMaxValue;
    value = Normalize(value);
}

void UISlider::SetValue(float newValue) {
    value = Normalize(newValue);
}

float UISlider::GetRatio() const {
    const float range = maxValue - minValue;
    return range > 0.0f ? std::clamp((value - minValue) / range, 0.0f, 1.0f) : 0.0f;
}

bool UISlider::IsValueChanged() const {
    return hasChanged && GetCanvas() && changedUpdateCount == GetCanvas()->GetUpdateCount();
}

void UISlider::OnInitialize() {
    track = CreateWhiteSprite(GetSpriteCommon());
    fill = CreateWhiteSprite(GetSpriteCommon());
    handle = CreateWhiteSprite(GetSpriteCommon());
}

void UISlider::OnUpdate() {
    // 押している間は、外へ出てもマウスの位置に付いていく
    if (IsPressed() && IsInteractableInHierarchy()) {
        ChangeValueByUser(GetValueAtPointer());
    }

    const Rect& rect = GetWorldRect();
    const Vector2 worldSize = GetWorldSize();
    const float handleWidth = GetHandleWidth();
    const float trackHeight = (std::max)(worldSize.y * kTrackHeightRatio, 2.0f);
    const float trackTop = rect.min.y + (worldSize.y - trackHeight) * 0.5f;
    // つまみが端からはみ出さないよう、つまみの中心が動ける範囲は両端からつまみの半分ずつ内側
    const float usableWidth = (std::max)(worldSize.x - handleWidth, 0.0f);
    const float handleLeft = rect.min.x + usableWidth * GetRatio();

    const bool isActive = IsHovered() || IsPressed() || (IsFocused() && GetCanvas()->IsFocusVisible());
    const float alpha = IsInteractableInHierarchy() ? 1.0f : kDisabledAlpha;

    track->SetPosition({ rect.min.x, trackTop });
    track->SetSize({ worldSize.x, trackHeight });
    track->SetColor(WithAlpha(colors.track, alpha));
    fill->SetPosition({ rect.min.x, trackTop });
    fill->SetSize({ handleLeft - rect.min.x + handleWidth * 0.5f, trackHeight });
    fill->SetColor(WithAlpha(colors.fill, alpha));
    handle->SetPosition({ handleLeft, rect.min.y });
    handle->SetSize({ handleWidth, worldSize.y });
    handle->SetColor(WithAlpha(isActive ? colors.handleActive : colors.handle, alpha));
    for (Sprite* sprite : { track.get(), fill.get(), handle.get() }) {
        sprite->SetLayer(GetDrawLayer());
        sprite->Update();
    }
}

void UISlider::OnDraw() {
    track->Draw();
    fill->Draw();
    handle->Draw();
}

void UISlider::OnPointerDown() {
    if (IsInteractableInHierarchy()) {
        ChangeValueByUser(GetValueAtPointer());
    }
}

bool UISlider::OnNavigate(UINavigation direction) {
    if (direction != UINavigation::kLeft && direction != UINavigation::kRight) {
        return false; // 上下は次の部品へ
    }
    float amount = keyStep;
    if (amount <= 0.0f) {
        amount = step > 0.0f ? step : (maxValue - minValue) * kDefaultKeyStepRatio;
    }
    ChangeValueByUser(value + (direction == UINavigation::kRight ? amount : -amount));
    return true;
}

float UISlider::Normalize(float rawValue) const {
    const float low = (std::min)(minValue, maxValue);
    const float high = (std::max)(minValue, maxValue);
    float result = std::clamp(rawValue, low, high);
    if (step > 0.0f) {
        result = minValue + std::round((result - minValue) / step) * step;
        result = std::clamp(result, low, high);
    }
    return result;
}

void UISlider::ChangeValueByUser(float newValue) {
    const float normalized = Normalize(newValue);
    if (normalized == value) {
        return;
    }
    value = normalized;
    hasChanged = true;
    changedUpdateCount = GetCanvas()->GetUpdateCount();
    if (onValueChanged) {
        onValueChanged(value);
    }
}

float UISlider::GetValueAtPointer() const {
    const Vector2& mouse = Framework::GetInstance()->GetInput()->GetMousePosition();
    const float handleWidth = GetHandleWidth();
    const float usableWidth = GetWorldSize().x - handleWidth;
    if (usableWidth <= 0.0f) {
        return value;
    }
    const float ratio = std::clamp((mouse.x - GetWorldRect().min.x - handleWidth * 0.5f) / usableWidth, 0.0f, 1.0f);
    return minValue + (maxValue - minValue) * ratio;
}

float UISlider::GetHandleWidth() const {
    return (std::max)(GetWorldSize().y * kHandleWidthRatio, 4.0f);
}
