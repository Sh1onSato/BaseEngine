#include "UILayoutGroup.h"
#include <algorithm>

namespace {
    // 縦並びなら y が並べる向き、x が直角の向き
    float MainOf(const Vector2& value, bool isVertical) { return isVertical ? value.y : value.x; }
    float CrossOf(const Vector2& value, bool isVertical) { return isVertical ? value.x : value.y; }
    Vector2 MakeVector(float main, float cross, bool isVertical) {
        return isVertical ? Vector2{ cross, main } : Vector2{ main, cross };
    }
}

UILayoutGroup::UILayoutGroup(UILayoutDirection direction) : direction(direction) {
    SetBlocksPointer(false);
}

void UILayoutGroup::SetPadding(float left, float top, float right, float bottom) {
    paddingLeft = left;
    paddingTop = top;
    paddingRight = right;
    paddingBottom = bottom;
}

void UILayoutGroup::SetFitContent(bool mainAxis, bool crossAxis) {
    fitMainAxis = mainAxis;
    fitCrossAxis = crossAxis;
}

void UILayoutGroup::OnBeforeLayout() {
    if (!fitMainAxis && !fitCrossAxis) {
        return;
    }
    // 中身の大きさ = 並べる向きは子の合計 + 間、直角の向きは一番大きい子
    const bool isVertical = IsVertical();
    float contentMain = 0.0f;
    float contentCross = 0.0f;
    int visibleCount = 0;
    for (const std::unique_ptr<UIElement>& child : GetChildren()) {
        if (!child || !child->IsVisible() || child->IsDestroyed()) {
            continue;
        }
        contentMain += MainOf(child->GetSize(), isVertical);
        contentCross = (std::max)(contentCross, CrossOf(child->GetSize(), isVertical));
        ++visibleCount;
    }
    if (visibleCount > 1) {
        contentMain += spacing * static_cast<float>(visibleCount - 1);
    }
    const Vector2 padding = { paddingLeft + paddingRight, paddingTop + paddingBottom };
    Vector2 newSize = GetSize();
    if (fitMainAxis) {
        (isVertical ? newSize.y : newSize.x) = contentMain + MainOf(padding, isVertical);
    }
    if (fitCrossAxis) {
        (isVertical ? newSize.x : newSize.y) = contentCross + CrossOf(padding, isVertical);
    }
    SetSize(newSize);
}

void UILayoutGroup::OnArrangeChildren() {
    const bool isVertical = IsVertical();
    const Vector2 worldSize = GetWorldSize();
    const Vector2 innerSize = { worldSize.x - paddingLeft - paddingRight, worldSize.y - paddingTop - paddingBottom };
    const float innerMain = MainOf(innerSize, isVertical);
    const float innerCross = CrossOf(innerSize, isVertical);

    // 中身の長さ（並べる向きの揃え方に使う）
    float contentMain = 0.0f;
    int visibleCount = 0;
    for (const std::unique_ptr<UIElement>& child : GetChildren()) {
        if (child && child->IsVisible() && !child->IsDestroyed()) {
            contentMain += MainOf(child->GetSize(), isVertical);
            ++visibleCount;
        }
    }
    if (visibleCount > 1) {
        contentMain += spacing * static_cast<float>(visibleCount - 1);
    }

    const Vector2 paddingStart = { paddingLeft, paddingTop };
    float cursor = MainOf(paddingStart, isVertical) + (std::max)(innerMain - contentMain, 0.0f) * mainAlignment;
    for (const std::unique_ptr<UIElement>& child : GetChildren()) {
        if (!child || !child->IsVisible() || child->IsDestroyed()) {
            continue;
        }
        Vector2 childSize = child->GetSize();
        if (expandChildren) {
            childSize = MakeVector(MainOf(childSize, isVertical), innerCross, isVertical);
            child->SetSize(childSize);
        }
        const float cross = CrossOf(paddingStart, isVertical) + (innerCross - CrossOf(childSize, isVertical)) * crossAlignment;
        // 子は自分の左上を基準に置く（親の UpdateLayout がこの後に子の矩形を計算する）
        child->SetStretch(false, false);
        child->SetAnchor({ 0.0f, 0.0f });
        child->SetPivot({ 0.0f, 0.0f });
        child->SetPosition(MakeVector(cursor, cross, isVertical));
        cursor += MainOf(childSize, isVertical) + spacing;
    }
}
