#pragma once
#include "Structs.h"
#include "BitmapText.h"
#include <functional>
#include <memory>
#include <vector>

class Sprite;
class SpriteCommon;
class Input;

// ImGuiに依存しない、ゲーム内完結の設定メニュー（Releaseビルドでも調整できるようにするための土台）。
// 項目はデータ駆動（Item配列）で、増減してもレイアウト・入力処理のコードは変えなくてよい設計。
class SettingsMenu {
public:
    struct Item {
        Vector4 color{ 1.0f, 1.0f, 1.0f, 1.0f }; // その行のバー色（項目を見分ける目印）
        bool isBool = false;
        float minValue = 0.0f;
        float maxValue = 1.0f;
        float step = 0.05f; // bool項目では未使用
        std::function<float()> getValue;      // bool項目は0.0/1.0を返す
        std::function<void(float)> setValue;  // bool項目は0.5を閾値にON/OFF判定
        // 値テキストの小数点以下桁数（例：感度のような小さい値は桁数を増やす）。
        // 末尾に追加しているのは、呼び出し側が{color, isBool, min, max, step, getValue, setValue}の
        // 位置指定の集成体初期化（aggregate initialization）を使う可能性があるため、途中に挿入すると型が合わずビルドエラーになるのを避けるため
        int displayDecimals = 2;
    };

    void Initialize(SpriteCommon* spriteCommon, std::vector<Item> items);
    // isOpen=falseの間もバー自体は最新値に追従させるが、キー入力の受付はisOpen中のみ
    void Update(Input* input, bool isOpen);
    void Draw();

private:
    struct Row {
        std::unique_ptr<Sprite> background;
        std::unique_ptr<Sprite> fill;
        BitmapText valueText;
    };

    static constexpr float kRowHeight = 28.0f;
    static constexpr float kBarWidth = 220.0f;
    static constexpr float kBarHeight = 18.0f;
    static constexpr Vector2 kMenuPosition = { 760.0f, 90.0f };
    static constexpr Vector2 kCursorSize = { 10.0f, kBarHeight };

    std::vector<Item> items;
    std::vector<Row> rows;
    std::unique_ptr<Sprite> cursor; // 選択中の行を示す明るい矩形マーカー
    int selectedIndex = 0;
};
