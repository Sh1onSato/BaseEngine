#include "UIPanel.h"
#include "Sprite.h"
#include "TextureManager.h"

namespace {
    // エンジンが内部で作る1x1の白画像（読み込み不要）
    const char* kWhiteTexture = "white";
}

UIPanel::UIPanel() {
    SetBlocksPointer(true);
}

UIPanel::~UIPanel() = default;

void UIPanel::SetTexture(const std::string& filePath) {
    if (filePath == texturePath) {
        return;
    }
    texturePath = filePath;
    // Sprite はテクスチャを作るときに決めるので、作り直す（OnInitialize の前なら覚えておくだけ）
    if (sprite) {
        CreateSprite();
    }
}

void UIPanel::SetSlice(float left, float top, float right, float bottom) {
    sliceLeft = left;
    sliceTop = top;
    sliceRight = right;
    sliceBottom = bottom;
    // 9枚の Sprite がまだ無ければ作る（OnInitialize の前なら覚えておくだけ）
    if (sprite && IsSliced() && !sliceSprites[0]) {
        CreateSprite();
    }
}

void UIPanel::OnInitialize() {
    CreateSprite();
}

void UIPanel::OnUpdate() {
    if (IsSliced()) {
        UpdateSliceSprites();
        return;
    }
    const Rect& rect = GetWorldRect();
    sprite->SetPosition(rect.min);
    sprite->SetSize(GetWorldSize());
    sprite->SetColor(GetDrawColor());
    sprite->SetLayer(GetDrawLayer());
    sprite->Update();
}

void UIPanel::OnDraw() {
    if (IsSliced()) {
        for (const std::unique_ptr<Sprite>& slice : sliceSprites) {
            slice->Draw();
        }
        return;
    }
    sprite->Draw();
}

void UIPanel::CreateSprite() {
    if (texturePath != kWhiteTexture) {
        TextureManager::GetInstance()->LoadTexture(texturePath);
    }
    sprite = std::make_unique<Sprite>();
    sprite->Initialize(GetSpriteCommon(), texturePath);
    sprite->SetAnchorPoint({ 0.0f, 0.0f });

    for (std::unique_ptr<Sprite>& slice : sliceSprites) {
        slice.reset();
        if (IsSliced()) {
            slice = std::make_unique<Sprite>();
            slice->Initialize(GetSpriteCommon(), texturePath);
            slice->SetAnchorPoint({ 0.0f, 0.0f });
        }
    }
}

void UIPanel::UpdateSliceSprites() {
    // 画像全体の大きさ（Initialize 直後の切り出しは画像全体）
    const Vector2 textureSize = sprite->GetTexSize();
    const Rect& rect = GetWorldRect();
    const Vector2 worldSize = GetWorldSize();

    // 画面上の端の大きさ。パネルが端の合計より小さいときは、つぶれないよう同じ割合で縮める
    float left = sliceLeft * sliceScale;
    float right = sliceRight * sliceScale;
    float top = sliceTop * sliceScale;
    float bottom = sliceBottom * sliceScale;
    if (left + right > worldSize.x && left + right > 0.0f) {
        const float shrink = worldSize.x / (left + right);
        left *= shrink;
        right *= shrink;
    }
    if (top + bottom > worldSize.y && top + bottom > 0.0f) {
        const float shrink = worldSize.y / (top + bottom);
        top *= shrink;
        bottom *= shrink;
    }

    // 画面上の区切り（x・y それぞれ4本）と、画像上の区切り
    const float screenX[4] = { rect.min.x, rect.min.x + left, rect.max.x - right, rect.max.x };
    const float screenY[4] = { rect.min.y, rect.min.y + top, rect.max.y - bottom, rect.max.y };
    const float textureX[4] = { 0.0f, sliceLeft, textureSize.x - sliceRight, textureSize.x };
    const float textureY[4] = { 0.0f, sliceTop, textureSize.y - sliceBottom, textureSize.y };

    const Vector4 drawColor = GetDrawColor();
    for (size_t row = 0; row < 3; ++row) {
        for (size_t column = 0; column < 3; ++column) {
            Sprite* slice = sliceSprites[row * 3 + column].get();
            slice->SetPosition({ screenX[column], screenY[row] });
            slice->SetSize({ screenX[column + 1] - screenX[column], screenY[row + 1] - screenY[row] });
            slice->SetTexLeftTop({ textureX[column], textureY[row] });
            slice->SetTexSize({ textureX[column + 1] - textureX[column], textureY[row + 1] - textureY[row] });
            slice->SetColor(drawColor);
            slice->SetLayer(GetDrawLayer());
            slice->Update();
        }
    }
}
