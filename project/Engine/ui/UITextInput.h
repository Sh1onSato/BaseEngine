#pragma once
#include "Text.h"
#include "UIPanel.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

class Sprite;

// 入力欄に打てる文字の種類
enum class UITextInputFilter {
    kPrintable,    // 英字・数字・記号・スペース（ASCII の表示できる文字すべて。既定）
    kAlphanumeric, // 英字・数字・_ だけ（名前・ID 向け）
    kInteger,      // 数字と、先頭の -
    kDecimal,      // 数字と、先頭の - と、. 1つ
};

/// <summary>
/// 英数字の文字入力欄（名前入力・パスワード・数値など）。日本語は打てない（入力中は IME を OFF にする）。
/// クリックするか、キー・パッドで選んで決定（Enter・パッド A）すると入力が始まり、カーソルが点滅する。
///   打つ / Backspace・Delete で消す / ←→・Home・End でカーソル移動 / Ctrl+V で貼り付け
///   Enter か入力欄の外をクリックで確定（SetOnSubmit の関数が呼ばれる）/ Esc で入力前の文字に戻して終える
///   パッドは A・B で確定して終える（画面上のキーボードはまだ無い）
/// 入力中は UICanvas::IsTextInputActive() が true になるので、ゲーム側のキー操作はその間無視する。
/// 文字の範囲選択・コピーはまだ無い。
///
///   UITextInput* nameInput = canvas.Create<UITextInput>();
///   nameInput->SetAlignment({ 0.5f, 0.5f });
///   nameInput->SetSize({ 320.0f, 44.0f });
///   nameInput->SetPlaceholder("名前を入力");
///   nameInput->SetMaxLength(12);
///   nameInput->SetFilter(UITextInputFilter::kAlphanumeric);
///   nameInput->SetOnSubmit([this](const std::string& text) { playerName = text; });
/// </summary>
class UITextInput : public UIPanel {
public:
    struct Colors {
        // 背景（SetColor の色・画像に掛け算される）
        Vector4 normal = { 0.10f, 0.11f, 0.14f, 1.0f };
        Vector4 hover = { 0.14f, 0.16f, 0.21f, 1.0f };
        Vector4 editing = { 0.05f, 0.06f, 0.08f, 1.0f };
        Vector4 disabled = { 0.15f, 0.15f, 0.15f, 0.6f };
        // 下線（入力中は editingLine）
        Vector4 line = { 0.40f, 0.45f, 0.55f, 1.0f };
        Vector4 editingLine = { 0.35f, 0.65f, 1.0f, 1.0f };
        Vector4 text = { 1.0f, 1.0f, 1.0f, 1.0f };
        Vector4 placeholder = { 0.55f, 0.57f, 0.62f, 1.0f };
        Vector4 caret = { 1.0f, 1.0f, 1.0f, 1.0f };
    };

    static constexpr size_t kDefaultMaxLength = 64;

    explicit UITextInput(const std::string& text = "");
    ~UITextInput() override;

    // 文字を入れる（打てない文字は除き、最大文字数で切る）。SetOnChanged の関数は呼ばない
    void SetText(const std::string& newText);
    const std::string& GetText() const { return text; }
    // 空のときに薄く出す説明（日本語も可）
    void SetPlaceholder(const std::string& newPlaceholder);
    const std::string& GetPlaceholder() const { return placeholder; }
    // 打てる最大の文字数（0 で制限なし）
    void SetMaxLength(size_t length);
    size_t GetMaxLength() const { return maxLength; }
    void SetFilter(UITextInputFilter newFilter);
    // 1文字ずつ「打ってよいか」を決める関数（SetFilter より優先。nullptr で SetFilter に戻る）。ASCII の表示できる文字だけが渡される
    void SetCustomFilter(std::function<bool(char)> filter);
    // true なら文字を * で隠す
    void SetPassword(bool password) { isPassword = password; }
    bool IsPassword() const { return isPassword; }

    // fontName: L"Yu Gothic UI", L"Consolas" など
    void SetFont(const std::wstring& newFontName, float newFontSize, bool newBold = false);
    void SetFontSize(float newFontSize) { SetFont(fontName, newFontSize, isBold); }
    // 左右の端と文字の間
    void SetTextPadding(float padding) { textPadding = padding; }
    void SetColors(const Colors& newColors) { colors = newColors; }
    const Colors& GetColors() const { return colors; }

    // 文字が変わるたび（打つ・消す・貼り付け・Esc で戻す）に呼ぶ関数
    void SetOnChanged(std::function<void(const std::string&)> callback) { onChanged = std::move(callback); }
    // 確定したとき（Enter・外をクリック・パッド A/B。Esc は除く）に呼ぶ関数
    void SetOnSubmit(std::function<void(const std::string&)> callback) { onSubmit = std::move(callback); }
    // このフレームの canvas.Update() で確定したか
    bool IsSubmitted() const;

    // 入力中か
    bool IsEditing() const;
    // 入力を始める（カーソルは末尾）/ 確定して終える / 入力前の文字に戻して終える
    void BeginEdit();
    void EndEdit();
    void CancelEdit();
    // カーソルの位置（何文字目の前か）
    size_t GetCaretIndex() const { return caretIndex; }

protected:
    void OnInitialize() override;
    void OnUpdate() override;
    void OnDraw() override;
    void OnPointerDown() override;
    void OnSubmit() override;
    void OnTextInputEnd() override;
    Vector4 GetDrawColor() const override;

private:
    // c を text の index の位置に入れてよいか
    bool IsAllowed(char c, size_t index, const std::string& current) const;
    // chars をカーソルの位置に入れる（打てない文字は飛ばし、最大文字数で止める）。入ったら true
    bool InsertAtCaret(const std::string& chars);
    void ProcessTextInputEvents();
    // 文字を変えて、変わっていれば SetOnChanged の関数を呼ぶ
    void ChangeText(const std::string& newText, size_t newCaretIndex);
    // 画面に出す文字列（パスワードなら *）
    std::string GetDisplayText() const;
    // 文字を出す範囲（左右の余白を除いた矩形）
    Rect GetTextRect() const;
    size_t CaretIndexFromX(float x) const;
    // カーソルが見えるように文字を横にずらす
    void UpdateScroll();
    void RestartCaretBlink() { caretBlinkTimer = 0.0f; }

    std::unique_ptr<Text> textObject;
    std::unique_ptr<Text> placeholderObject;
    std::unique_ptr<Sprite> caret;
    std::unique_ptr<Sprite> underline;

    std::string text;
    std::string placeholder;
    size_t maxLength = kDefaultMaxLength;
    UITextInputFilter filter = UITextInputFilter::kPrintable;
    std::function<bool(char)> customFilter;
    bool isPassword = false;
    std::wstring fontName = L"Yu Gothic UI";
    float fontSize = 22.0f;
    bool isBold = false;
    float textPadding = 10.0f;
    Colors colors;
    std::function<void(const std::string&)> onChanged;
    std::function<void(const std::string&)> onSubmit;

    size_t caretIndex = 0;
    float caretBlinkTimer = 0.0f;
    // 文字を左へずらしている量（長い文字の後ろの方を打っているとき）
    float scrollX = 0.0f;
    // 入力を始めたときの文字（Esc で戻す）
    std::string textBeforeEdit;
    // 入力を始めたフレーム（始めた Enter・Space をそのまま文字として受け取らないため）
    uint64_t editStartUpdateCount = 0;
    bool isCancelling = false;
    uint64_t submittedUpdateCount = 0;
    bool hasBeenSubmitted = false;

    // 測った文字列と結果（変わったときだけ測り直す）
    std::string measuredText;
    bool isMetricsDirty = true;
    Text::LineMetrics metrics;
};
