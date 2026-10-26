#pragma once
#include "Structs.h"
#include <memory>
#include <string>
#include <vector>

class Sprite;
class SpriteCommon;

// 任意の文字列（日本語・改行可）を表示するテキスト。
// WindowsにインストールされているフォントでGDIが文字列を画像に描き、それをテクスチャとしてSpriteで表示する。
// 文字列・フォントが変わった時だけ画像を作り直すので、毎フレーム同じ文字列をSetTextしても重くならない。
//
// 例:
//   text.Initialize(spriteCommon, 32.0f);          // 32px
//   text.SetText("スコア: 1200\nHIGH SCORE: 5000");
//   text.SetPosition({ 20.0f, 20.0f });
//   text.Update();   // Update中に呼ぶ（画像の作り直しはここで行う。Draw中はGPU転送できないため）
//   text.Draw();     // 描画キューに積む（実際の描画はSceneManagerがFlushで行う）
class Text {
public:
    enum class Align {
        kLeft,
        kCenter,
        kRight,
    };

    // 文字画像の周りの余白(px)。文字の左上は画像の左上から (kPadding, kPadding) の位置
    static constexpr int kPadding = 2;

    // 1行の文字列を測った結果（文字入力欄のカーソル位置用）
    struct LineMetrics {
        // 文字の境目の x(px)。[0] は先頭（0）、[i] は i 文字目の後ろ。要素数は UTF-16 の文字数 + 1
        std::vector<float> caretPositions;
        // 行の高さ(px)
        float lineHeight = 0.0f;
    };
    // 改行を含まない文字列を、Text と同じフォントで描いたときの大きさを測る（GDI を使うので毎フレームは呼ばない）
    static LineMetrics MeasureLine(const std::string& text, const std::wstring& fontName, float fontSize, bool bold = false);

    Text();
    ~Text();

    // fontSize: 文字の高さ(px)。fontName: フォント名（例 L"Yu Gothic UI", L"Meiryo", L"MS Gothic"）
    void Initialize(SpriteCommon* spriteCommon, float fontSize = 32.0f, const std::wstring& fontName = L"Yu Gothic UI", bool bold = false);

    // UTF-8文字列
    void SetText(const std::string& text);
    const std::string& GetText() const { return text; }

    void SetFont(const std::wstring& fontName, float fontSize, bool bold = false);
    // 複数行のときの行揃え
    void SetAlign(Align align);

    void SetPosition(const Vector2& position);
    // 0,0=左上基準 / 0.5,0.5=中央基準 / 1,1=右下基準
    void SetAnchorPoint(const Vector2& anchor);
    void SetColor(const Vector4& color);
    // 文字画像の拡大率（フォントサイズを変えずに一時的に大きくする演出用。大きくするとぼやける）
    void SetScale(float scale);
    void SetRotation(float rotation);
    // 描画レイヤー（大きいほど手前。Initializeの後に呼ぶ）
    void SetLayer(int layer);

    // 文字列を描画したときの大きさ(px、Scale適用前)
    Vector2 GetSize() const { return textureSize; }

    void Update();
    void Draw();

private:
    void Rebuild();

    SpriteCommon* spriteCommon = nullptr;
    std::unique_ptr<Sprite> sprite;
    std::string textureName;

    std::string text;
    std::wstring fontName = L"Yu Gothic UI";
    float fontSize = 32.0f;
    bool bold = false;
    Align align = Align::kLeft;
    float scale = 1.0f;

    Vector2 textureSize = { 1.0f, 1.0f };
    bool isDirty = true;
    bool isEmpty = true;
};
