#include "Text.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "TextureManager.h"
#include "StringUtility.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "gdi32.lib")

namespace {
    // テクスチャ名の重複を避けるための通し番号
    uint32_t textInstanceCounter = 0;
    // 文字の縁がテクスチャ端で切れないようにする余白(px)
    constexpr int kPadding = 2;
}

Text::Text() = default;
Text::~Text() = default;

void Text::Initialize(SpriteCommon* common, float size, const std::wstring& font, bool isBold) {
    spriteCommon = common;
    fontSize = size;
    fontName = font;
    bold = isBold;
    textureName = "__text_" + std::to_string(textInstanceCounter++);

    // 最初は1x1の透明テクスチャ（Spriteの初期化にテクスチャが必要なため）
    const uint8_t transparent[4] = { 0, 0, 0, 0 };
    TextureManager::GetInstance()->CreateOrUpdateTexture(textureName, 1, 1, transparent);

    sprite = std::make_unique<Sprite>();
    sprite->Initialize(spriteCommon, textureName);
    isDirty = true;
}

void Text::SetText(const std::string& newText) {
    if (newText != text) {
        text = newText;
        isDirty = true;
    }
}

void Text::SetFont(const std::wstring& newFontName, float newFontSize, bool newBold) {
    if (newFontName != fontName || newFontSize != fontSize || newBold != bold) {
        fontName = newFontName;
        fontSize = newFontSize;
        bold = newBold;
        isDirty = true;
    }
}

void Text::SetAlign(Align newAlign) {
    if (newAlign != align) {
        align = newAlign;
        isDirty = true;
    }
}

void Text::SetPosition(const Vector2& position) { sprite->SetPosition(position); }
void Text::SetAnchorPoint(const Vector2& anchor) { sprite->SetAnchorPoint(anchor); }
void Text::SetColor(const Vector4& color) { sprite->SetColor(color); }
void Text::SetRotation(float rotation) { sprite->SetRotation(rotation); }

void Text::SetScale(float newScale) {
    scale = newScale;
    sprite->SetSize({ textureSize.x * scale, textureSize.y * scale });
}

void Text::Update() {
    if (isDirty) {
        Rebuild();
        isDirty = false;
    }
    sprite->Update();
}

void Text::Draw() {
    if (!isEmpty) {
        sprite->Draw();
    }
}

void Text::Rebuild() {
    std::wstring wideText = StringUtility::ConvertString(text);
    isEmpty = wideText.empty();
    if (isEmpty) {
        return;
    }

    HDC dc = CreateCompatibleDC(nullptr);
    HFONT font = CreateFontW(
        -static_cast<int>(std::lround(fontSize)), 0, 0, 0,
        bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, // ClearTypeだと文字の縁に色が付くので、グレースケールのアンチエイリアスにする
        DEFAULT_PITCH | FF_DONTCARE, fontName.c_str());
    HGDIOBJ oldFont = SelectObject(dc, font);

    UINT alignFlag = align == Align::kCenter ? DT_CENTER : (align == Align::kRight ? DT_RIGHT : DT_LEFT);
    UINT format = DT_NOPREFIX | DT_EXPANDTABS | alignFlag;

    // 必要な大きさを測る
    RECT measure = { 0, 0, 0, 0 };
    DrawTextW(dc, wideText.c_str(), -1, &measure, format | DT_CALCRECT);
    int width = (std::max)(1, static_cast<int>(measure.right) + kPadding * 2);
    int height = (std::max)(1, static_cast<int>(measure.bottom) + kPadding * 2);

    // 上から下へ並ぶ32bitのビットマップに、黒地に白で文字を描く
    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = width;
    bitmapInfo.bmiHeader.biHeight = -height; // 負の値でトップダウン
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &bitmapInfo, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);

    std::memset(bits, 0, static_cast<size_t>(width) * height * 4);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    RECT drawRect = { kPadding, kPadding, width - kPadding, height - kPadding };
    DrawTextW(dc, wideText.c_str(), -1, &drawRect, format);
    GdiFlush();

    // 明るさ（=文字の濃さ）をアルファにした白いテクスチャに変換する。色はSprite側のSetColorで付ける
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    const uint8_t* src = static_cast<const uint8_t*>(bits);
    for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
        uint8_t coverage = (std::max)({ src[i * 4 + 0], src[i * 4 + 1], src[i * 4 + 2] });
        pixels[i * 4 + 0] = 255;
        pixels[i * 4 + 1] = 255;
        pixels[i * 4 + 2] = 255;
        pixels[i * 4 + 3] = coverage;
    }

    SelectObject(dc, oldBitmap);
    SelectObject(dc, oldFont);
    DeleteObject(bitmap);
    DeleteObject(font);
    DeleteDC(dc);

    TextureManager::GetInstance()->CreateOrUpdateTexture(textureName, width, height, pixels.data());

    textureSize = { static_cast<float>(width), static_cast<float>(height) };
    sprite->SetTexLeftTop({ 0.0f, 0.0f });
    sprite->SetTexSize(textureSize);
    sprite->SetSize({ textureSize.x * scale, textureSize.y * scale });
}
