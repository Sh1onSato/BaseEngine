#include "UIElement.h"
#include "Logger.h"
#include "SpriteCommon.h"
#include "UICanvas.h"
#include <algorithm>
#include <cassert>

UIElement::~UIElement() {
    // マウスが乗っている・押している要素として覚えられたまま消えないように知らせる
    if (canvas) {
        canvas->OnElementRemoved(this);
    }
}

void UIElement::SetParent(UIElement* newParent) {
    assert(canvas && parent && "UIElement: キャンバスの一番上（ルート）の親は変えられません");
    if (!canvas || !parent) {
        return;
    }
    UIElement* target = newParent ? newParent : canvas->GetRoot();
    if (target == parent) {
        return;
    }
    if (target == this || IsAncestorOf(target) || target->GetCanvas() != canvas) {
        Logger::Log("[UIElement] SetParent: 自分・自分の子孫・別のキャンバスの要素は親にできません\n");
        return;
    }
    std::unique_ptr<UIElement> self = DetachFromParent();
    parent = target;
    target->children.push_back(std::move(self));
}

void UIElement::SetAlignment(const Vector2& value) {
    anchor = value;
    pivot = value;
}

void UIElement::SetMargins(float left, float top, float right, float bottom) {
    marginLeft = left;
    marginTop = top;
    marginRight = right;
    marginBottom = bottom;
}

Vector2 UIElement::GetWorldCenter() const {
    return { (worldRect.min.x + worldRect.max.x) * 0.5f, (worldRect.min.y + worldRect.max.y) * 0.5f };
}

bool UIElement::ContainsPoint(const Vector2& point) const {
    return Collision::PointInRect(point, worldRect);
}

bool UIElement::IsVisibleInHierarchy() const {
    for (const UIElement* element = this; element; element = element->parent) {
        if (!element->isVisible) {
            return false;
        }
    }
    return true;
}

bool UIElement::IsInteractableInHierarchy() const {
    for (const UIElement* element = this; element; element = element->parent) {
        if (!element->isInteractable) {
            return false;
        }
    }
    return true;
}

SpriteCommon* UIElement::GetSpriteCommon() const {
    return canvas ? canvas->GetSpriteCommon() : nullptr;
}

int UIElement::GetDrawLayer() const {
    return canvas ? canvas->GetLayer() : UICanvas::kDefaultLayer;
}

void UIElement::RecalculateLayout() {
    // 自分の大きさで並び方が変わる親（入れ子なら一番外側）から計算し直す
    UIElement* target = this;
    while (target->parent && target->parent->ControlsChildLayout() && target->parent->parent) {
        target = target->parent;
    }
    if (target->parent) {
        target->Measure();
        target->UpdateLayout(target->parent->worldRect);
    }
}

void UIElement::AttachChild(std::unique_ptr<UIElement> child) {
    child->canvas = canvas;
    child->parent = this;
    UIElement* raw = child.get();
    children.push_back(std::move(child));
    canvas->OnElementAdded(raw);
    raw->OnInitialize();
    // 作ったフレームから正しい位置に出るよう、今の親の矩形で一度計算しておく
    raw->Measure();
    raw->UpdateLayout(worldRect);
}

std::unique_ptr<UIElement> UIElement::DetachFromParent() {
    // 一覧から erase すると、親が子を順に処理している途中（クリックの中で SetParent した等）で順番がずれるので、
    // 中身だけ取り出して nullptr を残す（RemoveDestroyedChildren で片付ける）
    for (std::unique_ptr<UIElement>& child : parent->children) {
        if (child.get() == this) {
            return std::move(child);
        }
    }
    assert(false && "UIElement: 親の子一覧に自分がいません");
    return nullptr;
}

bool UIElement::IsAncestorOf(const UIElement* element) const {
    for (const UIElement* current = element; current; current = current->parent) {
        if (current == this) {
            return true;
        }
    }
    return false;
}

void UIElement::Measure() {
    // 子の大きさが先に決まっていないと、子に合わせる親（入れ子のグループ）の大きさが1フレーム遅れるので、子から先に
    for (const std::unique_ptr<UIElement>& child : children) {
        if (child) {
            child->Measure();
        }
    }
    OnBeforeLayout();
}

void UIElement::UpdateLayout(const Rect& parentRect) {
    const float parentWidth = parentRect.max.x - parentRect.min.x;
    const float parentHeight = parentRect.max.y - parentRect.min.y;
    if (stretchX) {
        worldRect.min.x = parentRect.min.x + marginLeft;
        worldRect.max.x = parentRect.max.x - marginRight;
    } else {
        worldRect.min.x = parentRect.min.x + parentWidth * anchor.x + position.x - size.x * pivot.x;
        worldRect.max.x = worldRect.min.x + size.x;
    }
    if (stretchY) {
        worldRect.min.y = parentRect.min.y + marginTop;
        worldRect.max.y = parentRect.max.y - marginBottom;
    } else {
        worldRect.min.y = parentRect.min.y + parentHeight * anchor.y + position.y - size.y * pivot.y;
        worldRect.max.y = worldRect.min.y + size.y;
    }
    OnArrangeChildren();
    for (const std::unique_ptr<UIElement>& child : children) {
        if (child) {
            child->UpdateLayout(worldRect);
        }
    }
}

void UIElement::UpdateTree() {
    if (!isVisible || isDestroyed) {
        return;
    }
    OnUpdate();
    // OnUpdate の中で子が増えても大丈夫なよう、添字で回す
    for (size_t i = 0; i < children.size(); ++i) {
        if (children[i]) {
            children[i]->UpdateTree();
        }
    }
}

void UIElement::DrawTree() {
    if (!isVisible || isDestroyed) {
        return;
    }
    OnDraw();
    SpriteCommon* spriteCommon = ClipsChildren() ? GetSpriteCommon() : nullptr;
    if (spriteCommon) {
        spriteCommon->PushClipRect(worldRect);
    }
    for (const std::unique_ptr<UIElement>& child : children) {
        if (child) {
            child->DrawTree();
        }
    }
    if (spriteCommon) {
        spriteCommon->PopClipRect();
    }
}

UIElement* UIElement::HitTest(const Vector2& point) {
    if (!isVisible || isDestroyed) {
        return nullptr;
    }
    // 子を自分の中だけに描く部品なら、外にはみ出た子はマウスを受けない
    const bool canHitChildren = !ClipsChildren() || ContainsPoint(point);
    // 後の子ほど手前に描かれるので、後ろから調べる
    for (size_t i = canHitChildren ? children.size() : 0; i > 0; --i) {
        if (children[i - 1]) {
            if (UIElement* hit = children[i - 1]->HitTest(point)) {
                return hit;
            }
        }
    }
    return (blocksPointer && ContainsPoint(point)) ? this : nullptr;
}

void UIElement::RemoveDestroyedChildren() {
    std::erase_if(children, [](const std::unique_ptr<UIElement>& child) { return !child || child->isDestroyed; });
    for (const std::unique_ptr<UIElement>& child : children) {
        child->RemoveDestroyedChildren();
    }
}

void UIElement::CollectFocusable(std::vector<UIElement*>& out) {
    if (!isVisible || isDestroyed) {
        return;
    }
    if (isFocusable && IsInteractableInHierarchy()) {
        out.push_back(this);
    }
    for (const std::unique_ptr<UIElement>& child : children) {
        if (child) {
            child->CollectFocusable(out);
        }
    }
}

bool UIElement::CanFocus() const {
    if (!isFocusable || !IsVisibleInHierarchy() || !IsInteractableInHierarchy()) {
        return false;
    }
    for (const UIElement* element = this; element; element = element->parent) {
        if (element->isDestroyed) {
            return false;
        }
    }
    return true;
}

UIElement* UIElement::FindRecursive(const std::string& targetName) {
    if (!isDestroyed && name == targetName) {
        return this;
    }
    for (const std::unique_ptr<UIElement>& child : children) {
        if (child) {
            if (UIElement* found = child->FindRecursive(targetName)) {
                return found;
            }
        }
    }
    return nullptr;
}
