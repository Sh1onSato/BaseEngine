#include "BitmapText.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "TextureManager.h"

void BitmapText::Initialize(SpriteCommon* spriteCommon, size_t maxChars) {
    this->spriteCommon = spriteCommon;

    // "digitFont"アトラスが未生成なら生成する（複数のBitmapTextから呼ばれても内部でガードされる）
    TextureManager::GetInstance()->CreateInternalDigitFontTexture();

    charSprites.clear();
    charSprites.reserve(maxChars);
    for (size_t i = 0; i < maxChars; ++i) {
        auto sprite = std::make_unique<Sprite>();
        sprite->Initialize(spriteCommon, "digitFont");
        sprite->SetTexSize({ kGlyphCellWidth, kGlyphCellHeight });
        sprite->SetSize({ kGlyphCellWidth * scale, kGlyphCellHeight * scale });
        sprite->SetColor(color);
        charSprites.push_back(std::move(sprite));
    }
}

int BitmapText::GlyphIndex(char c) {
    // 小文字は大文字にフォールバック（小文字専用のビットパターンは持たない）
    if (c >= 'a' && c <= 'z') {
        c = static_cast<char>(c - 'a' + 'A');
    }
    // TextureManager::kBitmapFontGlyphs（文字→ビットパターンの一覧テーブル）を線形探索し、
    // 一致した文字のインデックス＝グリフアトラス上の並び順を返す
    for (int i = 0; i < TextureManager::kBitmapFontGlyphCount; ++i) {
        if (TextureManager::kBitmapFontGlyphs[i].character == c) {
            return i;
        }
    }
    return kBlankGlyphIndex;
}

void BitmapText::SetText(const std::string& text) {
    size_t count = (text.size() < charSprites.size()) ? text.size() : charSprites.size();
    for (size_t i = 0; i < count; ++i) {
        int glyph = GlyphIndex(text[i]);
        Sprite* sprite = charSprites[i].get();
        sprite->SetTexLeftTop({ kGlyphCellWidth * static_cast<float>(glyph), 0.0f });
        sprite->SetPosition({ position.x + kGlyphCellWidth * scale * static_cast<float>(i), position.y });
    }
    visibleCount = count;
}

void BitmapText::SetPosition(const Vector2& pos) {
    position = pos;
}

void BitmapText::SetColor(const Vector4& newColor) {
    color = newColor;
    for (auto& sprite : charSprites) {
        sprite->SetColor(color);
    }
}

void BitmapText::Update() {
    for (size_t i = 0; i < visibleCount; ++i) {
        charSprites[i]->Update();
    }
}

void BitmapText::Draw() {
    for (size_t i = 0; i < visibleCount; ++i) {
        charSprites[i]->Draw();
    }
}
