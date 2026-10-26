#include "UICanvas.h"
#include "Framework.h"
#include "GameTime.h"
#include "Input.h"
#include "Screen.h"
#include "Sprite.h"
#include "WinApp.h"
#include <algorithm>
#include <cmath>
#ifdef _DEBUG
#include "externals/imgui/imgui.h"
#endif

namespace {
    constexpr int kLeftMouseButton = 0;

    // アクションマップの名前
    const char* kActionUp = "UI_Up";
    const char* kActionDown = "UI_Down";
    const char* kActionLeft = "UI_Left";
    const char* kActionRight = "UI_Right";
    const char* kActionSubmit = "UI_Submit";
    const char* kActionCancel = "UI_Cancel";
    const char* kAxisHorizontal = "UI_Horizontal";
    const char* kAxisVertical = "UI_Vertical";

    // 押しっぱなしにしたとき、最初に繰り返すまでの秒数と、その後の間隔（ポーズ中も動くよう実時間で数える）
    constexpr float kRepeatDelay = 0.4f;
    constexpr float kRepeatInterval = 0.12f;
    // スティックをこれ以上倒したら上下左右を押した扱い
    constexpr float kStickThreshold = 0.5f;
    // 自動で次の部品を探すとき、向きからずれている距離をどれだけ嫌うか（大きいほどまっすぐ先の部品を選ぶ）
    constexpr float kNavigationSideWeight = 2.0f;
    // 選んでいる部品の枠と部品の間の隙間
    constexpr float kFocusFramePadding = 2.0f;

    // キャンバスはシーンの終了時（エンジンの終了処理の途中）にも消えるので、WinApp が無ければ何もしない
    void SetImeEnabled(bool enabled) {
        if (Framework* framework = Framework::GetInstance()) {
            if (WinApp* winApp = framework->GetWinApp()) {
                winApp->SetImeEnabled(enabled);
            }
        }
    }

    Vector2 ToVector(UINavigation direction) {
        switch (direction) {
        case UINavigation::kUp: return { 0.0f, -1.0f }; // 画面の座標は y が下向き
        case UINavigation::kDown: return { 0.0f, 1.0f };
        case UINavigation::kLeft: return { -1.0f, 0.0f };
        case UINavigation::kRight: return { 1.0f, 0.0f };
        }
        return { 0.0f, 0.0f };
    }
}

UICanvas::UICanvas() {
    spriteCommon = Framework::GetInstance()->GetSpriteCommon();
    root = std::make_unique<UIElement>();
    root->canvas = this;
    root->SetName("Root");
    root->SetStretch(true, true); // 画面全体

    for (std::unique_ptr<Sprite>& sprite : focusFrame) {
        sprite = std::make_unique<Sprite>();
        sprite->Initialize(spriteCommon, "white");
        sprite->SetAnchorPoint({ 0.0f, 0.0f });
    }
    BindDefaultActions();
}

UICanvas::~UICanvas() {
    // 部品のデストラクタが OnElementRemoved を呼ぶので、キャンバスのメンバーが生きているうちに消す
    root.reset();
}

void UICanvas::Update() {
    ++updateCount;
    wasTextInputActive = activeTextInput != nullptr;
    UpdateLayout();
    UpdateTextInput();
    UpdatePointer();
    UpdateNavigation();
    // クリックの中で作られた・動かされた部品も、このフレームから正しい位置に出す
    UpdateLayout();
    root->UpdateTree();
    UpdateFocusFrame();
    root->RemoveDestroyedChildren();
}

void UICanvas::Draw() {
    root->DrawTree();
    if (IsFocusVisible() && focusFrameThickness > 0.0f) {
        for (const std::unique_ptr<Sprite>& sprite : focusFrame) {
            sprite->Draw();
        }
    }
}

UIElement* UICanvas::Find(const std::string& name) const {
    return root->FindRecursive(name);
}

void UICanvas::Clear() {
    for (const std::unique_ptr<UIElement>& child : root->children) {
        if (child) {
            child->Destroy();
        }
    }
}

void UICanvas::SetFocus(UIElement* element) {
    if (element && (!IsAlive(element) || !element->CanFocus() || !GetInteractiveRoot()->IsAncestorOf(element))) {
        return;
    }
    if (element == focusedElement) {
        isFocusVisible = element != nullptr;
        return;
    }
    if (focusedElement) {
        focusedElement->isFocused = false;
        focusedElement->OnBlur();
    }
    focusedElement = element;
    if (focusedElement) {
        focusedElement->isFocused = true;
        focusedElement->OnFocus();
    }
    isFocusVisible = element != nullptr;
}

void UICanvas::SetNavigationEnabled(bool enabled) {
    isNavigationEnabled = enabled;
    if (!enabled) {
        SetFocus(nullptr);
        heldDirection.reset();
    }
}

void UICanvas::SetFocusFrame(const Vector4& color, float thickness) {
    focusFrameColor = color;
    focusFrameThickness = thickness;
}

void UICanvas::BeginTextInput(UIElement* element) {
    if (!IsAlive(element) || element == activeTextInput) {
        return;
    }
    EndTextInput();
    activeTextInput = element;
    wasTextInputActive = true;
    SetImeEnabled(false);
}

void UICanvas::EndTextInput() {
    if (!activeTextInput) {
        return;
    }
    // 先に外しておく（OnTextInputEnd の中から BeginTextInput を呼んでもよいように）
    UIElement* element = activeTextInput;
    activeTextInput = nullptr;
    SetImeEnabled(true);
    element->OnTextInputEnd();
}

void UICanvas::SetModalRoot(UIElement* element) {
    modalRoot = IsAlive(element) ? element : nullptr;
}

void UICanvas::OnElementAdded(const UIElement* element) {
    aliveElements.insert(element);
}

void UICanvas::OnElementRemoved(const UIElement* element) {
    aliveElements.erase(element);
    // 消える部品の OnBlur などは呼ばない（もう半分壊れているため）。ポインタだけ外す
    if (hoveredElement == element) {
        hoveredElement = nullptr;
    }
    if (pressedElement == element) {
        pressedElement = nullptr;
    }
    if (focusedElement == element) {
        focusedElement = nullptr;
    }
    if (modalRoot == element) {
        modalRoot = nullptr;
    }
    if (activeTextInput == element) {
        activeTextInput = nullptr;
        SetImeEnabled(true);
    }
}

void UICanvas::UpdateLayout() {
    const Vector2 gameSize = Screen::GetGameSize();
    root->Measure();
    root->UpdateLayout(Rect{ { 0.0f, 0.0f }, gameSize });
}

void UICanvas::UpdatePointer() {
    const Input* input = Framework::GetInstance()->GetInput();
    UIElement* hit = GetInteractiveRoot()->HitTest(input->GetMousePosition());
    bool canPress = true;
#ifdef _DEBUG
    // ImGui の窓の上のマウスは ImGui のもの
    if (ImGui::GetIO().WantCaptureMouse) {
        hit = nullptr;
        canPress = false;
    }
#endif
    SetHoveredElement(hit);

    const Input::MouseMove move = input->GetMouseMove();
    if (move.lX != 0 || move.lY != 0) {
        // マウスを動かしたら枠は消す。キー・パッドで選んでいる途中なら、マウスが乗った部品を選び直す
        isFocusVisible = false;
        if (focusedElement && hoveredElement && hoveredElement != focusedElement && hoveredElement->CanFocus()) {
            UIElement* hovered = hoveredElement;
            SetFocus(hovered);
            isFocusVisible = false;
        }
    }

    if (canPress && input->TriggerMouseButton(kLeftMouseButton)) {
        // 文字入力中に入力欄の外を押したら入力を終える
        if (activeTextInput && hoveredElement != activeTextInput) {
            EndTextInput();
        }
        if (hoveredElement) {
            isFocusVisible = false;
            pressedElement = hoveredElement;
            pressedElement->isPressed = true;
            pressedElement->OnPointerDown();
        }
    }
    if (input->ReleaseMouseButton(kLeftMouseButton) && pressedElement) {
        UIElement* released = pressedElement;
        pressedElement = nullptr;
        released->isPressed = false;
        released->OnPointerUp();
        // 押した部品の上で離したときだけクリック（押したまま外へ出て離したらキャンセル）
        if (released == hoveredElement && !released->IsDestroyed() && released->IsInteractableInHierarchy()) {
            released->OnClick();
        }
    }
}

void UICanvas::UpdateNavigation() {
    isCancelTriggered = false;
    // 選んでいた部品が隠れた・押せなくなった・モーダルの外になったら選ぶのをやめる
    if (focusedElement && (!focusedElement->CanFocus() || !GetInteractiveRoot()->IsAncestorOf(focusedElement))) {
        SetFocus(nullptr);
    }
    if (activeTextInput) {
        // 入力中の矢印・Enter・Esc・Backspace・Space は入力欄が使う。
        // 入力を終えた直後に、押したままの矢印で選ぶ部品が動かないよう、今押している向きを覚えておく
        heldDirection = ReadHeldDirection();
        // パッドでは文字を打てないので、A・B で入力を終えられるようにする
        const Input* input = Framework::GetInstance()->GetInput();
        if (input->TriggerPadButton(XINPUT_GAMEPAD_A) || input->TriggerPadButton(XINPUT_GAMEPAD_B)) {
            EndTextInput();
        }
        return;
    }
    if (!isNavigationEnabled) {
        return;
    }

    // 上下左右: 押した瞬間に1回、押しっぱなしなら少し待ってから繰り返す
    const std::optional<UINavigation> held = ReadHeldDirection();
    if (held != heldDirection) {
        heldDirection = held;
        if (held) {
            Navigate(*held);
            repeatTimer = kRepeatDelay;
        }
    } else if (held) {
        repeatTimer -= Time::GetUnscaledDeltaTime();
        if (repeatTimer <= 0.0f) {
            Navigate(*held);
            repeatTimer = kRepeatInterval;
        }
    }

    const Input* input = Framework::GetInstance()->GetInput();
    if (input->TriggerAction(kActionSubmit) && focusedElement) {
        isFocusVisible = true;
        focusedElement->OnSubmit();
    }
    if (input->TriggerAction(kActionCancel)) {
        isCancelTriggered = true;
        if (onCancel) {
            onCancel();
        }
    }
}

void UICanvas::UpdateTextInput() {
    if (activeTextInput && (!activeTextInput->IsVisibleInHierarchy() || !activeTextInput->IsInteractableInHierarchy() ||
                            activeTextInput->IsDestroyed() || !GetInteractiveRoot()->IsAncestorOf(activeTextInput))) {
        EndTextInput();
    }
}

void UICanvas::SetHoveredElement(UIElement* element) {
    if (element == hoveredElement) {
        return;
    }
    if (hoveredElement) {
        hoveredElement->isHovered = false;
        hoveredElement->OnPointerExit();
    }
    hoveredElement = element;
    if (hoveredElement) {
        hoveredElement->isHovered = true;
        hoveredElement->OnPointerEnter();
    }
}

std::optional<UINavigation> UICanvas::ReadHeldDirection() const {
    const Input* input = Framework::GetInstance()->GetInput();
    if (input->PushAction(kActionUp)) { return UINavigation::kUp; }
    if (input->PushAction(kActionDown)) { return UINavigation::kDown; }
    if (input->PushAction(kActionLeft)) { return UINavigation::kLeft; }
    if (input->PushAction(kActionRight)) { return UINavigation::kRight; }
    const float x = input->GetAxis(kAxisHorizontal);
    const float y = input->GetAxis(kAxisVertical);
    if ((std::max)(std::abs(x), std::abs(y)) < kStickThreshold) {
        return std::nullopt;
    }
    if (std::abs(x) > std::abs(y)) {
        return x > 0.0f ? UINavigation::kRight : UINavigation::kLeft;
    }
    return y > 0.0f ? UINavigation::kUp : UINavigation::kDown; // スティックは上が +
}

void UICanvas::Navigate(UINavigation direction) {
    if (!focusedElement) {
        SetFocus(FindFirstFocusTarget()); // 最初の1回は選ぶだけ（動かさない）
        return;
    }
    isFocusVisible = true;
    if (focusedElement->OnNavigate(direction)) {
        return; // 部品が自分で使った（スライダーの左右など）
    }
    if (UIElement* target = FindNavigationTarget(focusedElement, direction)) {
        SetFocus(target);
    }
}

UIElement* UICanvas::FindNavigationTarget(UIElement* from, UINavigation direction) {
    UIElement* manual = from->GetNavigation(direction);
    if (IsAlive(manual) && manual->CanFocus() && GetInteractiveRoot()->IsAncestorOf(manual)) {
        return manual;
    }

    std::vector<UIElement*> candidates;
    GetInteractiveRoot()->CollectFocusable(candidates);
    const Vector2 forward = ToVector(direction);
    const Vector2 origin = from->GetWorldCenter();
    UIElement* best = nullptr;
    float bestScore = 0.0f;
    for (UIElement* candidate : candidates) {
        if (candidate == from) {
            continue;
        }
        const Vector2 center = candidate->GetWorldCenter();
        const Vector2 offset = { center.x - origin.x, center.y - origin.y };
        const float ahead = offset.x * forward.x + offset.y * forward.y;
        if (ahead <= 1.0f) {
            continue; // 向きの先に無い
        }
        const float side = std::abs(offset.x * forward.y - offset.y * forward.x);
        const float score = ahead + side * kNavigationSideWeight;
        if (!best || score < bestScore) {
            best = candidate;
            bestScore = score;
        }
    }
    return best;
}

UIElement* UICanvas::FindFirstFocusTarget() {
    if (hoveredElement && hoveredElement->CanFocus() && GetInteractiveRoot()->IsAncestorOf(hoveredElement)) {
        return hoveredElement;
    }
    std::vector<UIElement*> candidates;
    GetInteractiveRoot()->CollectFocusable(candidates);
    // 一番上の行の、一番左（同じ行とみなすのは上端の差が 1px 未満）
    UIElement* best = nullptr;
    for (UIElement* candidate : candidates) {
        if (!best) {
            best = candidate;
            continue;
        }
        const Rect& rect = candidate->GetWorldRect();
        const Rect& bestRect = best->GetWorldRect();
        const bool isSameRow = std::abs(rect.min.y - bestRect.min.y) < 1.0f;
        if ((isSameRow && rect.min.x < bestRect.min.x) || (!isSameRow && rect.min.y < bestRect.min.y)) {
            best = candidate;
        }
    }
    return best;
}

void UICanvas::BindDefaultActions() {
    // 何個キャンバスを作っても1回だけ（後からゲーム側が BindAction し直した物を上書きしない）
    static bool isBound = false;
    if (isBound) {
        return;
    }
    isBound = true;
    Input* input = Framework::GetInstance()->GetInput();
    auto bindIfMissing = [input](const char* name, const Input::ActionBinding& binding) {
        if (!input->GetActionBinding(name)) {
            input->BindAction(name, binding);
        }
    };
    bindIfMissing(kActionUp, { { DIK_UP }, { XINPUT_GAMEPAD_DPAD_UP }, {} });
    bindIfMissing(kActionDown, { { DIK_DOWN }, { XINPUT_GAMEPAD_DPAD_DOWN }, {} });
    bindIfMissing(kActionLeft, { { DIK_LEFT }, { XINPUT_GAMEPAD_DPAD_LEFT }, {} });
    bindIfMissing(kActionRight, { { DIK_RIGHT }, { XINPUT_GAMEPAD_DPAD_RIGHT }, {} });
    bindIfMissing(kActionSubmit, { { DIK_RETURN, DIK_NUMPADENTER, DIK_SPACE }, { XINPUT_GAMEPAD_A }, {} });
    bindIfMissing(kActionCancel, { { DIK_ESCAPE, DIK_BACK }, { XINPUT_GAMEPAD_B }, {} });
    input->BindAxis(kAxisHorizontal, { {}, {}, PadAxis::kLeftX });
    input->BindAxis(kAxisVertical, { {}, {}, PadAxis::kLeftY });
}

void UICanvas::UpdateFocusFrame() {
    if (!IsFocusVisible() || focusFrameThickness <= 0.0f) {
        return;
    }
    const Rect& rect = focusedElement->GetWorldRect();
    const float t = focusFrameThickness;
    const float outer = t + kFocusFramePadding;
    const float left = rect.min.x - outer;
    const float top = rect.min.y - outer;
    const float width = rect.max.x - rect.min.x + outer * 2.0f;
    const float height = rect.max.y - rect.min.y + outer * 2.0f;
    // 上・下・左・右
    const Vector2 positions[4] = { { left, top }, { left, top + height - t }, { left, top }, { left + width - t, top } };
    const Vector2 sizes[4] = { { width, t }, { width, t }, { t, height }, { t, height } };
    for (size_t i = 0; i < focusFrame.size(); ++i) {
        focusFrame[i]->SetPosition(positions[i]);
        focusFrame[i]->SetSize(sizes[i]);
        focusFrame[i]->SetColor(focusFrameColor);
        focusFrame[i]->SetLayer(layer);
        focusFrame[i]->Update();
    }
}
