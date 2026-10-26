#include "UIScrollView.h"
#include "Framework.h"
#include "Input.h"
#include "Sprite.h"
#include "UICanvas.h"
#include "UILayoutGroup.h"
#include <algorithm>
#include <limits>

namespace {
    const Vector2 kDefaultScrollViewSize = { 300.0f, 400.0f };
    const Vector4 kDefaultBackgroundColor = { 0.05f, 0.06f, 0.08f, 0.85f };
    constexpr float kContentPadding = 8.0f;
    constexpr float kContentSpacing = 6.0f;
    // 中身とスクロールバーの間
    constexpr float kScrollbarGap = 4.0f;
    // つまみが小さくなりすぎて掴めなくならないように
    constexpr float kMinHandleHeight = 24.0f;
    // ホイール1目盛りの値（Windows の WHEEL_DELTA）
    constexpr float kWheelDelta = 120.0f;

    std::unique_ptr<Sprite> CreateWhiteSprite(SpriteCommon* spriteCommon) {
        std::unique_ptr<Sprite> sprite = std::make_unique<Sprite>();
        sprite->Initialize(spriteCommon, "white");
        sprite->SetAnchorPoint({ 0.0f, 0.0f });
        return sprite;
    }
}

UIScrollView::UIScrollView() {
    SetSize(kDefaultScrollViewSize);
    SetColor(kDefaultBackgroundColor);
}

UIScrollView::~UIScrollView() = default;

float UIScrollView::GetMaxScrollOffset() const {
    if (!content) {
        return 0.0f;
    }
    return (std::max)(content->GetSize().y - GetWorldSize().y, 0.0f);
}

void UIScrollView::ScrollToBottom() {
    // 大きい値を入れておけば、次の配置の計算で一番下に収まる
    scrollOffset = (std::numeric_limits<float>::max)();
}

void UIScrollView::ScrollIntoView(const UIElement* element) {
    if (!element || !IsDescendant(element)) {
        return;
    }
    const Rect& target = element->GetWorldRect();
    const Rect& view = GetWorldRect();
    if (target.min.y < view.min.y) {
        scrollOffset -= view.min.y - target.min.y;
    } else if (target.max.y > view.max.y) {
        scrollOffset += target.max.y - view.max.y;
    }
}

void UIScrollView::OnInitialize() {
    UIPanel::OnInitialize();
    track = CreateWhiteSprite(GetSpriteCommon());
    handle = CreateWhiteSprite(GetSpriteCommon());

    content = CreateChild<UILayoutGroup>(UILayoutDirection::kVertical);
    content->SetName("ScrollContent");
    content->SetPadding(kContentPadding);
    content->SetSpacing(kContentSpacing);
    content->SetExpandChildren(true);
    content->SetFitContent(true, false); // 高さは中身に合わせる（幅はこのリストが決める）
}

void UIScrollView::OnArrangeChildren() {
    // 中身の高さは先に測られている（子から順に測る）ので、ここで範囲に収めて中身をずらす
    scrollOffset = std::clamp(scrollOffset, 0.0f, GetMaxScrollOffset());
    const float scrollbarSpace = IsScrollbarVisible() ? scrollbarWidth + kScrollbarGap : 0.0f;
    content->SetAlignment({ 0.0f, 0.0f });
    content->SetPosition({ 0.0f, -scrollOffset });
    content->SetSize({ (std::max)(GetWorldSize().x - scrollbarSpace, 0.0f), content->GetSize().y });
}

void UIScrollView::OnUpdate() {
    const Input* input = Framework::GetInstance()->GetInput();
    const float previousOffset = scrollOffset;

    const long wheel = input->GetMouseWheel();
    if (wheel != 0 && IsPointerInside()) {
        scrollOffset -= static_cast<float>(wheel) / kWheelDelta * wheelStep; // 奥へ回す（+）と上へ戻る
    }

    // つまみのドラッグ（押している間は外へ出ても付いていく）
    if (!IsPressed()) {
        isDraggingHandle = false;
    }
    if (isDraggingHandle) {
        const Rect trackRect = GetTrackRect();
        const Rect handleRect = GetHandleRect();
        const float movable = (trackRect.max.y - trackRect.min.y) - (handleRect.max.y - handleRect.min.y);
        if (movable > 0.0f) {
            const float handleTop = input->GetMousePosition().y - grabOffset;
            const float ratio = std::clamp((handleTop - trackRect.min.y) / movable, 0.0f, 1.0f);
            scrollOffset = ratio * GetMaxScrollOffset();
        }
    }

    // キー・パッドで中の部品が選ばれたら、見える位置まで動かす（マウスで選び直したときは動かさない）
    const UIElement* focused = GetCanvas()->GetFocusedElement();
    if (focused != lastFocused) {
        lastFocused = focused;
        if (focused && GetCanvas()->IsFocusVisible()) {
            ScrollIntoView(focused);
        }
    }

    if (scrollOffset != previousOffset) {
        RecalculateLayout(); // 中の部品の OnUpdate より前に、このフレームの位置に直す
    }

    UIPanel::OnUpdate();

    const Rect trackRect = GetTrackRect();
    const Rect handleRect = GetHandleRect();
    const bool isHandleActive = isDraggingHandle || (IsHovered() && Collision::PointInRect(input->GetMousePosition(), handleRect));
    track->SetPosition(trackRect.min);
    track->SetSize({ trackRect.max.x - trackRect.min.x, trackRect.max.y - trackRect.min.y });
    track->SetColor(colors.track);
    handle->SetPosition(handleRect.min);
    handle->SetSize({ handleRect.max.x - handleRect.min.x, handleRect.max.y - handleRect.min.y });
    handle->SetColor(isHandleActive ? colors.handleActive : colors.handle);
    for (Sprite* sprite : { track.get(), handle.get() }) {
        sprite->SetLayer(GetDrawLayer());
        sprite->Update();
    }
}

void UIScrollView::OnDraw() {
    UIPanel::OnDraw();
    if (IsScrollbarVisible()) {
        track->Draw();
        handle->Draw();
    }
}

void UIScrollView::OnPointerDown() {
    // 中の部品の上ではなく、背景かスクロールバーの上で押された
    const Vector2& mouse = Framework::GetInstance()->GetInput()->GetMousePosition();
    if (!IsScrollbarVisible() || !Collision::PointInRect(mouse, GetTrackRect())) {
        return;
    }
    const Rect handleRect = GetHandleRect();
    if (Collision::PointInRect(mouse, handleRect)) {
        grabOffset = mouse.y - handleRect.min.y; // 掴んだ位置を保ったまま動かす
    } else {
        grabOffset = (handleRect.max.y - handleRect.min.y) * 0.5f; // 溝を押したら、つまみの中心をそこへ
    }
    isDraggingHandle = true;
}

bool UIScrollView::IsPointerInside() const {
    for (const UIElement* element = GetCanvas()->GetHoveredElement(); element; element = element->GetParent()) {
        if (dynamic_cast<const UIScrollView*>(element)) {
            return element == this;
        }
    }
    return false;
}

bool UIScrollView::IsDescendant(const UIElement* element) const {
    for (const UIElement* current = element ? element->GetParent() : nullptr; current; current = current->GetParent()) {
        if (current == this) {
            return true;
        }
    }
    return false;
}

Rect UIScrollView::GetTrackRect() const {
    const Rect& rect = GetWorldRect();
    return { { rect.max.x - scrollbarWidth, rect.min.y }, rect.max };
}

Rect UIScrollView::GetHandleRect() const {
    const Rect trackRect = GetTrackRect();
    const float viewHeight = trackRect.max.y - trackRect.min.y;
    const float contentHeight = content ? content->GetSize().y : 0.0f;
    // 見えている割合がつまみの長さ
    float handleHeight = contentHeight > 0.0f ? viewHeight * viewHeight / contentHeight : viewHeight;
    handleHeight = std::clamp(handleHeight, (std::min)(kMinHandleHeight, viewHeight), viewHeight);
    const float maxOffset = GetMaxScrollOffset();
    const float ratio = maxOffset > 0.0f ? std::clamp(scrollOffset / maxOffset, 0.0f, 1.0f) : 0.0f;
    const float top = trackRect.min.y + (viewHeight - handleHeight) * ratio;
    return { { trackRect.min.x, top }, { trackRect.max.x, top + handleHeight } };
}
