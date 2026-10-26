#include "UIProgressBar.h"
#include "GameTime.h"
#include "Sprite.h"
#include <algorithm>

namespace {
    const Vector2 kDefaultBarSize = { 240.0f, 16.0f };
}

UIProgressBar::UIProgressBar() {
    SetSize(kDefaultBarSize);
    SetBlocksPointer(false);
}

UIProgressBar::~UIProgressBar() = default;

void UIProgressBar::SetRatio(float newRatio) {
    ratio = std::clamp(newRatio, 0.0f, 1.0f);
    // 増えたときは追いかける棒もすぐ合わせる（遅れて見せるのは減ったときだけ）
    if (ratio > trailRatio || trailSpeed <= 0.0f) {
        trailRatio = ratio;
    }
}

void UIProgressBar::SetValue(float current, float maxValue) {
    SetRatio(maxValue > 0.0f ? current / maxValue : 0.0f);
}

void UIProgressBar::OnInitialize() {
    for (std::unique_ptr<Sprite>* sprite : { &background, &trail, &fill }) {
        *sprite = std::make_unique<Sprite>();
        (*sprite)->Initialize(GetSpriteCommon(), "white");
        (*sprite)->SetAnchorPoint({ 0.0f, 0.0f });
    }
}

void UIProgressBar::OnUpdate() {
    if (trailRatio > ratio) {
        trailRatio = (std::max)(trailRatio - trailSpeed * Time::GetDeltaTime(), ratio);
    }
    PlaceBar(background.get(), 1.0f, colors.background);
    PlaceBar(trail.get(), trailRatio, colors.trail);
    PlaceBar(fill.get(), ratio, colors.fill);
}

void UIProgressBar::OnDraw() {
    background->Draw();
    if (trailRatio > ratio) {
        trail->Draw();
    }
    fill->Draw();
}

void UIProgressBar::PlaceBar(Sprite* sprite, float amount, const Vector4& color) {
    const Rect& rect = GetWorldRect();
    const Vector2 worldSize = GetWorldSize();
    Vector2 position = rect.min;
    Vector2 size = worldSize;
    switch (fillDirection) {
    case UIFillDirection::kLeftToRight:
        size.x = worldSize.x * amount;
        break;
    case UIFillDirection::kRightToLeft:
        size.x = worldSize.x * amount;
        position.x = rect.max.x - size.x;
        break;
    case UIFillDirection::kBottomToTop:
        size.y = worldSize.y * amount;
        position.y = rect.max.y - size.y;
        break;
    case UIFillDirection::kTopToBottom:
        size.y = worldSize.y * amount;
        break;
    }
    sprite->SetPosition(position);
    sprite->SetSize(size);
    sprite->SetColor(color);
    sprite->SetLayer(GetDrawLayer());
    sprite->Update();
}
