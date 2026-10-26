#include "UITextInput.h"
#include "Framework.h"
#include "GameTime.h"
#include "Input.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "UICanvas.h"
#include "WinApp.h"
#include <algorithm>
#include <cmath>
#ifdef _DEBUG
#include "externals/imgui/imgui.h"
#endif

namespace {
    const Vector2 kDefaultTextInputSize = { 280.0f, 44.0f };
    constexpr float kCaretWidth = 2.0f;
    constexpr float kUnderlineHeight = 2.0f;
    // カーソルの点滅（表示・非表示の時間。Windows の既定 530ms に合わせた）
    constexpr float kCaretBlinkInterval = 0.53f;

    Vector4 Multiply(const Vector4& a, const Vector4& b) {
        return { a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w };
    }

    std::unique_ptr<Sprite> CreateWhiteSprite(SpriteCommon* spriteCommon) {
        std::unique_ptr<Sprite> sprite = std::make_unique<Sprite>();
        sprite->Initialize(spriteCommon, "white");
        sprite->SetAnchorPoint({ 0.0f, 0.0f });
        return sprite;
    }

    bool IsPrintableAscii(char c) {
        const unsigned char code = static_cast<unsigned char>(c);
        return code >= 0x20 && code <= 0x7E;
    }

    bool IsDigit(char c) { return c >= '0' && c <= '9'; }
}

UITextInput::UITextInput(const std::string& initialText) {
    SetSize(kDefaultTextInputSize);
    SetFocusable(true);
    SetText(initialText);
}

UITextInput::~UITextInput() = default;

void UITextInput::SetText(const std::string& newText) {
    // 先頭から1文字ずつ、打てる文字だけを足していく（打ったときと同じ決まりで受け付ける）
    std::string accepted;
    for (char c : newText) {
        if (maxLength != 0 && accepted.size() >= maxLength) {
            break;
        }
        if (IsAllowed(c, accepted.size(), accepted)) {
            accepted.push_back(c);
        }
    }
    text = std::move(accepted);
    caretIndex = (std::min)(caretIndex, text.size());
}

void UITextInput::SetPlaceholder(const std::string& newPlaceholder) {
    placeholder = newPlaceholder;
    if (placeholderObject) {
        placeholderObject->SetText(placeholder);
    }
}

void UITextInput::SetMaxLength(size_t length) {
    maxLength = length;
    SetText(text);
}

void UITextInput::SetFilter(UITextInputFilter newFilter) {
    filter = newFilter;
    SetText(text);
}

void UITextInput::SetCustomFilter(std::function<bool(char)> newFilter) {
    customFilter = std::move(newFilter);
    SetText(text);
}

void UITextInput::SetFont(const std::wstring& newFontName, float newFontSize, bool newBold) {
    fontName = newFontName;
    fontSize = newFontSize;
    isBold = newBold;
    isMetricsDirty = true;
    if (textObject) {
        textObject->SetFont(fontName, fontSize, isBold);
        placeholderObject->SetFont(fontName, fontSize, isBold);
    }
}

bool UITextInput::IsSubmitted() const {
    return hasBeenSubmitted && GetCanvas() && submittedUpdateCount == GetCanvas()->GetUpdateCount();
}

bool UITextInput::IsEditing() const {
    return GetCanvas() && GetCanvas()->GetActiveTextInput() == this;
}

void UITextInput::BeginEdit() {
    if (!GetCanvas() || IsEditing() || !IsInteractableInHierarchy()) {
        return;
    }
    textBeforeEdit = text;
    editStartUpdateCount = GetCanvas()->GetUpdateCount();
    caretIndex = text.size();
    RestartCaretBlink();
    GetCanvas()->BeginTextInput(this);
}

void UITextInput::EndEdit() {
    if (IsEditing()) {
        GetCanvas()->EndTextInput(); // → OnTextInputEnd で確定
    }
}

void UITextInput::CancelEdit() {
    if (!IsEditing()) {
        return;
    }
    ChangeText(textBeforeEdit, textBeforeEdit.size());
    isCancelling = true;
    GetCanvas()->EndTextInput();
    isCancelling = false;
}

void UITextInput::OnInitialize() {
    UIPanel::OnInitialize();
    textObject = std::make_unique<Text>();
    textObject->Initialize(GetSpriteCommon(), fontSize, fontName, isBold);
    placeholderObject = std::make_unique<Text>();
    placeholderObject->Initialize(GetSpriteCommon(), fontSize, fontName, isBold);
    placeholderObject->SetText(placeholder);
    caret = CreateWhiteSprite(GetSpriteCommon());
    underline = CreateWhiteSprite(GetSpriteCommon());
}

void UITextInput::OnUpdate() {
    UIPanel::OnUpdate();

    if (IsEditing()) {
        ProcessTextInputEvents();
        caretBlinkTimer += Time::GetUnscaledDeltaTime(); // ポーズ中も点滅させる
    }
    const bool isEditing = IsEditing(); // Enter・Esc で終わっていることがある

    const std::string displayText = GetDisplayText();
    if (isMetricsDirty || displayText != measuredText) {
        metrics = Text::MeasureLine(displayText, fontName, fontSize, isBold);
        measuredText = displayText;
        isMetricsDirty = false;
    }
    if (isEditing) {
        UpdateScroll();
    } else {
        scrollX = 0.0f; // 入力していないときは先頭から見せる
    }

    // 文字の画像は半端な位置だとにじむので、整数の位置に置く
    const Rect& rect = GetWorldRect();
    const Rect textRect = GetTextRect();
    const float lineTop = std::round(rect.min.y + (GetWorldSize().y - metrics.lineHeight) * 0.5f);
    const float textLeft = std::round(textRect.min.x - scrollX);
    const float padding = static_cast<float>(Text::kPadding);
    const int drawLayer = GetDrawLayer();

    textObject->SetText(displayText);
    textObject->SetAnchorPoint({ 0.0f, 0.0f });
    textObject->SetPosition({ textLeft - padding, lineTop - padding });
    textObject->SetColor(colors.text);
    textObject->SetLayer(drawLayer);
    textObject->Update();

    placeholderObject->SetAnchorPoint({ 0.0f, 0.0f });
    placeholderObject->SetPosition({ std::round(textRect.min.x) - padding, lineTop - padding });
    placeholderObject->SetColor(colors.placeholder);
    placeholderObject->SetLayer(drawLayer);
    placeholderObject->Update();

    const float caretX = textLeft + metrics.caretPositions[(std::min)(caretIndex, metrics.caretPositions.size() - 1)];
    caret->SetPosition({ std::round(caretX), lineTop });
    caret->SetSize({ kCaretWidth, metrics.lineHeight });
    caret->SetColor(colors.caret);
    caret->SetLayer(drawLayer);
    caret->Update();

    underline->SetPosition({ rect.min.x, rect.max.y - kUnderlineHeight });
    underline->SetSize({ GetWorldSize().x, kUnderlineHeight });
    underline->SetColor(isEditing ? colors.editingLine : colors.line);
    underline->SetLayer(drawLayer);
    underline->Update();
}

void UITextInput::OnDraw() {
    UIPanel::OnDraw();
    underline->Draw();

    // 長い文字は欄の中だけに描く
    const Rect& rect = GetWorldRect();
    const Rect textRect = GetTextRect();
    SpriteCommon* spriteCommon = GetSpriteCommon();
    spriteCommon->PushClipRect({ { textRect.min.x, rect.min.y }, { textRect.max.x, rect.max.y } });
    if (text.empty()) {
        placeholderObject->Draw();
    } else {
        textObject->Draw();
    }
    const bool isCaretVisible = std::fmod(caretBlinkTimer, kCaretBlinkInterval * 2.0f) < kCaretBlinkInterval;
    if (IsEditing() && isCaretVisible) {
        caret->Draw();
    }
    spriteCommon->PopClipRect();
}

void UITextInput::OnPointerDown() {
    if (!IsInteractableInHierarchy()) {
        return;
    }
    BeginEdit(); // 入力中なら何もしない
    caretIndex = CaretIndexFromX(Framework::GetInstance()->GetInput()->GetMousePosition().x);
    RestartCaretBlink();
}

void UITextInput::OnSubmit() {
    BeginEdit();
}

void UITextInput::OnTextInputEnd() {
    if (isCancelling) {
        return;
    }
    hasBeenSubmitted = true;
    submittedUpdateCount = GetCanvas()->GetUpdateCount();
    if (onSubmit) {
        onSubmit(text);
    }
}

Vector4 UITextInput::GetDrawColor() const {
    const Vector4* stateColor = &colors.normal;
    if (!IsInteractableInHierarchy()) {
        stateColor = &colors.disabled;
    } else if (IsEditing()) {
        stateColor = &colors.editing;
    } else if (IsHovered() || (IsFocused() && GetCanvas()->IsFocusVisible())) {
        stateColor = &colors.hover;
    }
    return Multiply(*stateColor, GetColor());
}

bool UITextInput::IsAllowed(char c, size_t index, const std::string& current) const {
    if (!IsPrintableAscii(c)) {
        return false;
    }
    if (customFilter) {
        return customFilter(c);
    }
    switch (filter) {
    case UITextInputFilter::kPrintable:
        return true;
    case UITextInputFilter::kAlphanumeric:
        return IsDigit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    case UITextInputFilter::kInteger:
    case UITextInputFilter::kDecimal:
        if (IsDigit(c)) {
            // - の前には入れない
            return index > 0 || current.empty() || current.front() != '-';
        }
        if (c == '-') {
            return index == 0 && current.find('-') == std::string::npos;
        }
        if (c == '.' && filter == UITextInputFilter::kDecimal) {
            return current.find('.') == std::string::npos && (index > 0 || current.empty() || current.front() != '-');
        }
        return false;
    }
    return false;
}

bool UITextInput::InsertAtCaret(const std::string& chars) {
    std::string newText = text;
    size_t newCaret = caretIndex;
    for (char c : chars) {
        if (maxLength != 0 && newText.size() >= maxLength) {
            break;
        }
        if (IsAllowed(c, newCaret, newText)) {
            newText.insert(newCaret, 1, c);
            ++newCaret;
        }
    }
    if (newCaret == caretIndex) {
        return false;
    }
    ChangeText(newText, newCaret);
    return true;
}

void UITextInput::ProcessTextInputEvents() {
    // 入力を始めたフレームの入力（始めるのに押した Enter・Space など）は使わない
    if (editStartUpdateCount == GetCanvas()->GetUpdateCount()) {
        return;
    }
#ifdef _DEBUG
    // ImGui の入力欄に打っているときは ImGui のもの
    if (ImGui::GetIO().WantTextInput) {
        return;
    }
#endif
    using Type = TextInputEvent::Type;
    const Input* input = Framework::GetInstance()->GetInput();
    for (const TextInputEvent& event : input->GetTextInputEvents()) {
        switch (event.type) {
        case Type::kCharacter:
            if (event.character < 0x80) {
                InsertAtCaret(std::string(1, static_cast<char>(event.character)));
            }
            break;
        case Type::kPaste:
            InsertAtCaret(Framework::GetInstance()->GetWinApp()->GetClipboardText());
            break;
        case Type::kBackspace:
            if (caretIndex > 0) {
                ChangeText(std::string(text).erase(caretIndex - 1, 1), caretIndex - 1);
            }
            break;
        case Type::kDelete:
            if (caretIndex < text.size()) {
                ChangeText(std::string(text).erase(caretIndex, 1), caretIndex);
            }
            break;
        case Type::kLeft:
            caretIndex = caretIndex > 0 ? caretIndex - 1 : 0;
            break;
        case Type::kRight:
            caretIndex = (std::min)(caretIndex + 1, text.size());
            break;
        case Type::kHome:
            caretIndex = 0;
            break;
        case Type::kEnd:
            caretIndex = text.size();
            break;
        case Type::kEnter:
            EndEdit();
            return; // 残りの入力は使わない
        case Type::kEscape:
            CancelEdit();
            return;
        }
        RestartCaretBlink(); // 打っている間はカーソルを出したままにする
    }
}

void UITextInput::ChangeText(const std::string& newText, size_t newCaretIndex) {
    caretIndex = (std::min)(newCaretIndex, newText.size());
    if (newText == text) {
        return;
    }
    text = newText;
    if (onChanged) {
        onChanged(text);
    }
}

std::string UITextInput::GetDisplayText() const {
    return isPassword ? std::string(text.size(), '*') : text;
}

Rect UITextInput::GetTextRect() const {
    const Rect& rect = GetWorldRect();
    const float left = rect.min.x + textPadding;
    const float right = (std::max)(rect.max.x - textPadding, left);
    return { { left, rect.min.y }, { right, rect.max.y } };
}

size_t UITextInput::CaretIndexFromX(float x) const {
    if (metrics.caretPositions.empty()) {
        return text.size(); // まだ一度も測っていない（作ったフレームに押された）
    }
    // 一番近い文字の境目
    const float localX = x - (GetTextRect().min.x - scrollX);
    size_t best = 0;
    float bestDistance = std::abs(metrics.caretPositions.front() - localX);
    for (size_t i = 1; i < metrics.caretPositions.size(); ++i) {
        const float distance = std::abs(metrics.caretPositions[i] - localX);
        if (distance < bestDistance) {
            best = i;
            bestDistance = distance;
        }
    }
    return (std::min)(best, text.size());
}

void UITextInput::UpdateScroll() {
    const Rect textRect = GetTextRect();
    const float viewWidth = (std::max)(textRect.max.x - textRect.min.x - kCaretWidth, 0.0f);
    const float caretX = metrics.caretPositions[(std::min)(caretIndex, metrics.caretPositions.size() - 1)];
    if (caretX - scrollX > viewWidth) {
        scrollX = caretX - viewWidth;
    } else if (caretX < scrollX) {
        scrollX = caretX;
    }
    // 後ろを消して文字が短くなったら、右に空きができないよう戻す
    const float maxScroll = (std::max)(metrics.caretPositions.back() - viewWidth, 0.0f);
    scrollX = std::clamp(scrollX, 0.0f, maxScroll);
}
