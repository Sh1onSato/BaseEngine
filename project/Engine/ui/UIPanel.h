#pragma once
#include "UIElement.h"
#include <array>
#include <memory>
#include <string>

class Sprite;

/// <summary>
/// 色の付いた四角（または画像）を表示する UI 部品。ウィンドウの背景・枠・アイコンなどに使う。
/// マウスを受け止める（後ろにクリックが通らない）。通したいときは SetBlocksPointer(false)。
///
///   UIPanel* window = canvas.Create<UIPanel>();
///   window->SetAlignment({ 0.5f, 0.5f });
///   window->SetSize({ 400.0f, 300.0f });
///   window->SetColor({ 0.1f, 0.1f, 0.15f, 0.9f });
///   window->SetTexture("Resources/textures/frame.png");   // 画像を貼るなら（色は画像に掛け算される）
///   window->SetSlice(16.0f);                              // 角の丸い枠なら 9スライス（画像の端 16px は伸ばさない）
/// </summary>
class UIPanel : public UIElement {
public:
    UIPanel();
    ~UIPanel() override;

    void SetColor(const Vector4& newColor) { color = newColor; }
    const Vector4& GetColor() const { return color; }
    // 画像を貼る（まだ読み込んでいなければ読み込む）。"white" で色だけの四角に戻る
    void SetTexture(const std::string& filePath);
    const std::string& GetTexture() const { return texturePath; }
    // 9スライス: 画像の端（左・上・右・下。画像の px）は伸ばさず、真ん中だけ伸ばす。
    // 角の丸い枠などを、どんな大きさにしても角がつぶれずに出せる。全部 0 で普通に画像全体を伸ばす
    void SetSlice(float left, float top, float right, float bottom);
    void SetSlice(float all) { SetSlice(all, all, all, all); }
    // 画面に出すときの端の大きさの倍率（画像の 16px の端を 32px で出すなら 2）
    void SetSliceScale(float scale) { sliceScale = scale; }

protected:
    void OnInitialize() override;
    void OnUpdate() override;
    void OnDraw() override;
    // 実際に描く色（ボタンは押した・乗った状態で変える）
    virtual Vector4 GetDrawColor() const { return color; }

private:
    void CreateSprite();
    bool IsSliced() const { return sliceLeft > 0.0f || sliceTop > 0.0f || sliceRight > 0.0f || sliceBottom > 0.0f; }
    // 9スライスの9枚を置く（左上から右へ、行ごと）
    void UpdateSliceSprites();

    std::unique_ptr<Sprite> sprite;
    // 9スライスのときだけ作る（ボタンなど普通のパネルでは作らない）
    std::array<std::unique_ptr<Sprite>, 9> sliceSprites;
    std::string texturePath = "white";
    Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
    float sliceLeft = 0.0f;
    float sliceTop = 0.0f;
    float sliceRight = 0.0f;
    float sliceBottom = 0.0f;
    float sliceScale = 1.0f;
};
