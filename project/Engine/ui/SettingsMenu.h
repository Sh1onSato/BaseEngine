#pragma once
#include "UIPanel.h"
#include <functional>
#include <string>
#include <vector>

class UIButton;
class UILabel;
class UILayoutGroup;
class UISlider;
class UIToggle;

/// <summary>
/// 設定メニュー（ImGui を使わないので Release でも使える）。背景のパネルの中に、項目が上から縦に並ぶ。
/// 項目は「値を読む関数」と「値を書く関数」で渡すので、GameSettings 以外の値（ゲーム固有の設定など）もつなげられる。
/// 表示中は毎フレーム「値を読む関数」で見た目を合わせる（他の場所で値が変わっても表示がずれない）。
/// 大きさ（高さ）は項目の数に合わせて自動で決まる。幅は SetMenuWidth。
///
///   SettingsMenu* menu = canvas.Create<SettingsMenu>("設定");
///   menu->SetAlignment({ 0.5f, 0.5f });
///   menu->AddDefaultItems();                                  // 音量3つとフルスクリーン
///   menu->AddSlider({ .text = "マウス感度",
///                     .minValue = GameSettings::kMouseSensitivityMin, .maxValue = GameSettings::kMouseSensitivityMax,
///                     .getValue = [] { return GameSettings::GetInstance()->GetMouseSensitivity(); },
///                     .setValue = [](float v) { GameSettings::GetInstance()->SetMouseSensitivity(v); },
///                     .formatValue = [](float v) { return std::format("{:.4f}", v); } });
///   menu->AddButton("閉じる", [menu]() { menu->SetVisible(false); });
///   menu->FocusFirstItem();                                   // キー・パッドですぐ操作できるように
///
/// 値は GameSettings に入れておけば、終了時に settings.json へ保存される（Framework が Save する）。
/// </summary>
class SettingsMenu : public UIPanel {
public:
    struct SliderItem {
        std::string text;
        float minValue = 0.0f;
        float maxValue = 1.0f;
        // 刻み（0 でなめらか）
        float step = 0.0f;
        std::function<float()> getValue;
        std::function<void(float)> setValue;
        // 右に出す値の文字（空なら小数2桁。音量なら FormatPercent）
        std::function<std::string(float)> formatValue;
    };

    explicit SettingsMenu(const std::string& title = "設定");
    ~SettingsMenu() override;

    // --- 項目を足す（上から順に並ぶ。Create した後に呼ぶ） ---
    UISlider* AddSlider(const SliderItem& item);
    UIToggle* AddToggle(const std::string& text, std::function<bool()> getValue, std::function<void(bool)> setValue);
    UIButton* AddButton(const std::string& text, std::function<void()> onClick);
    // よく使う項目（全体・BGM・効果音の音量とフルスクリーン）を足す
    void AddDefaultItems();

    void SetTitle(const std::string& text);
    // メニューの幅（スライダーの長さは幅に合わせて決まる）
    void SetMenuWidth(float width);
    // 最初の項目をキー・パッドで選ぶ
    void FocusFirstItem();

    // 0〜1 を "80%" のように出す（音量用）
    static std::string FormatPercent(float value);

protected:
    void OnInitialize() override;
    void OnBeforeLayout() override;
    void OnUpdate() override;

private:
    struct SliderRow {
        UISlider* slider = nullptr;
        UILabel* valueLabel = nullptr;
        SliderItem item;
    };
    struct ToggleRow {
        UIToggle* toggle = nullptr;
        std::function<bool()> getValue;
    };

    // 項目を足したときに、最初の項目なら覚えておく
    void RegisterItem(UIElement* element);
    float GetSliderWidth() const;
    static std::string FormatSliderValue(const SliderItem& item, float value);

    UILayoutGroup* list = nullptr;
    UILabel* titleLabel = nullptr;
    // OnInitialize の前に SetTitle されたときの文字
    std::string pendingTitle;
    float menuWidth;
    std::vector<SliderRow> sliderRows;
    std::vector<ToggleRow> toggleRows;
    UIElement* firstItem = nullptr;
};
