#include "SettingsMenu.h"
#include "Framework.h"
#include "GameSettings.h"
#include "UIButton.h"
#include "UICanvas.h"
#include "UILabel.h"
#include "UILayoutGroup.h"
#include "UISlider.h"
#include "UIToggle.h"
#include "WinApp.h"
#include <algorithm>
#include <format>

namespace {
    constexpr float kDefaultMenuWidth = 560.0f;
    constexpr float kPadding = 24.0f;
    constexpr float kSpacing = 12.0f;
    constexpr float kRowHeight = 36.0f;
    constexpr float kButtonHeight = 44.0f;
    // スライダーの行: 名前・スライダー・値の文字の幅（スライダーは残りの幅）
    constexpr float kNameWidth = 180.0f;
    constexpr float kValueWidth = 72.0f;
    constexpr float kMinSliderWidth = 80.0f;
    constexpr float kTitleFontSize = 28.0f;
    constexpr float kItemFontSize = 22.0f;
    constexpr float kVolumeStep = 0.05f;
    const Vector4 kMenuColor = { 0.08f, 0.09f, 0.12f, 0.94f };
}

SettingsMenu::SettingsMenu(const std::string& title) : pendingTitle(title), menuWidth(kDefaultMenuWidth) {
    SetColor(kMenuColor);
}

SettingsMenu::~SettingsMenu() = default;

void SettingsMenu::OnInitialize() {
    UIPanel::OnInitialize();

    list = CreateChild<UILayoutGroup>(UILayoutDirection::kVertical);
    list->SetSize({ menuWidth, 0.0f });
    list->SetPadding(kPadding);
    list->SetSpacing(kSpacing);
    list->SetExpandChildren(true);
    list->SetFitContent(true, false); // 高さは項目に合わせる

    titleLabel = list->CreateChild<UILabel>(pendingTitle);
    titleLabel->SetFont(L"Yu Gothic UI", kTitleFontSize, true);
}

void SettingsMenu::OnBeforeLayout() {
    // 中の縦並びは先に大きさが決まっている（子から順に測る）ので、背景をそれに合わせる
    SetSize(list->GetSize());
}

void SettingsMenu::OnUpdate() {
    // 他の場所で値が変わっても表示がずれないよう、毎フレーム今の値に合わせる
    for (SliderRow& row : sliderRows) {
        if (row.item.getValue) {
            row.slider->SetValue(row.item.getValue());
        }
        row.valueLabel->SetText(FormatSliderValue(row.item, row.slider->GetValue()));
    }
    for (ToggleRow& row : toggleRows) {
        if (row.getValue) {
            row.toggle->SetOn(row.getValue());
        }
    }
    UIPanel::OnUpdate();
}

UISlider* SettingsMenu::AddSlider(const SliderItem& item) {
    UILayoutGroup* row = list->CreateChild<UILayoutGroup>(UILayoutDirection::kHorizontal);
    row->SetSize({ 0.0f, kRowHeight }); // 幅は縦並びがそろえる
    row->SetSpacing(kSpacing);
    row->SetCrossAlignment(0.5f);

    UILabel* nameLabel = row->CreateChild<UILabel>(item.text);
    nameLabel->SetFontSize(kItemFontSize);
    nameLabel->SetAutoSize(false);
    nameLabel->SetSize({ kNameWidth, kRowHeight });
    nameLabel->SetTextAnchor({ 0.0f, 0.5f });

    const float initialValue = item.getValue ? item.getValue() : item.minValue;
    UISlider* slider = row->CreateChild<UISlider>(item.minValue, item.maxValue, initialValue);
    slider->SetName(item.text);
    slider->SetSize({ GetSliderWidth(), kRowHeight });
    slider->SetStep(item.step);
    slider->SetValue(initialValue); // 刻みに合わせ直す

    UILabel* valueLabel = row->CreateChild<UILabel>(FormatSliderValue(item, slider->GetValue()));
    valueLabel->SetFontSize(kItemFontSize);
    valueLabel->SetAutoSize(false); // 値の桁が変わっても並びが動かないように幅を決めておく
    valueLabel->SetSize({ kValueWidth, kRowHeight });
    valueLabel->SetTextAnchor({ 1.0f, 0.5f });

    // ドラッグ中もそのフレームで値の文字が変わるよう、ここでも書き換える
    slider->SetOnValueChanged([item, valueLabel](float value) {
        if (item.setValue) {
            item.setValue(value);
        }
        valueLabel->SetText(FormatSliderValue(item, value));
    });

    sliderRows.push_back({ slider, valueLabel, item });
    RegisterItem(slider);
    return slider;
}

UIToggle* SettingsMenu::AddToggle(const std::string& text, std::function<bool()> getValue,
                                  std::function<void(bool)> setValue) {
    UIToggle* toggle = list->CreateChild<UIToggle>(text, getValue ? getValue() : false);
    toggle->SetName(text);
    toggle->SetSize({ 0.0f, kRowHeight });
    toggle->GetLabel()->SetFontSize(kItemFontSize);
    toggle->SetOnValueChanged(std::move(setValue));

    toggleRows.push_back({ toggle, std::move(getValue) });
    RegisterItem(toggle);
    return toggle;
}

UIButton* SettingsMenu::AddButton(const std::string& text, std::function<void()> onClick) {
    UIButton* button = list->CreateChild<UIButton>(text);
    button->SetName(text);
    button->SetSize({ 0.0f, kButtonHeight });
    button->GetLabel()->SetFontSize(kItemFontSize);
    button->SetOnClick(std::move(onClick));
    RegisterItem(button);
    return button;
}

void SettingsMenu::AddDefaultItems() {
    AddSlider({ .text = "全体の音量",
                .step = kVolumeStep,
                .getValue = [] { return GameSettings::GetInstance()->GetMasterVolume(); },
                .setValue = [](float value) { GameSettings::GetInstance()->SetMasterVolume(value); },
                .formatValue = FormatPercent });
    AddSlider({ .text = "BGM",
                .step = kVolumeStep,
                .getValue = [] { return GameSettings::GetInstance()->GetBgmVolume(); },
                .setValue = [](float value) { GameSettings::GetInstance()->SetBgmVolume(value); },
                .formatValue = FormatPercent });
    AddSlider({ .text = "効果音",
                .step = kVolumeStep,
                .getValue = [] { return GameSettings::GetInstance()->GetSeVolume(); },
                .setValue = [](float value) { GameSettings::GetInstance()->SetSeVolume(value); },
                .formatValue = FormatPercent });
    // 画面の切り替えは WinApp。GameSettings にも入れておく（終了時は Framework が WinApp の状態で上書きして保存する）
    AddToggle(
        "フルスクリーン", [] { return Framework::GetInstance()->GetWinApp()->IsFullscreen(); },
        [](bool isOn) {
            Framework::GetInstance()->GetWinApp()->SetFullscreen(isOn);
            GameSettings::GetInstance()->SetFullscreen(isOn);
        });
}

void SettingsMenu::SetTitle(const std::string& text) {
    if (titleLabel) {
        titleLabel->SetText(text);
    } else {
        pendingTitle = text;
    }
}

void SettingsMenu::SetMenuWidth(float width) {
    menuWidth = width;
    if (list) {
        list->SetSize({ menuWidth, list->GetSize().y });
    }
    for (SliderRow& row : sliderRows) {
        row.slider->SetSize({ GetSliderWidth(), kRowHeight });
    }
}

void SettingsMenu::FocusFirstItem() {
    if (firstItem && GetCanvas()) {
        GetCanvas()->SetFocus(firstItem);
    }
}

std::string SettingsMenu::FormatPercent(float value) {
    return std::format("{:.0f}%", value * 100.0f);
}

void SettingsMenu::RegisterItem(UIElement* element) {
    if (!firstItem) {
        firstItem = element;
    }
}

float SettingsMenu::GetSliderWidth() const {
    // 名前・スライダー・値の3つ（間は2つ）が、内側の余白を除いた幅に収まるように
    const float width = menuWidth - kPadding * 2.0f - kNameWidth - kValueWidth - kSpacing * 2.0f;
    return (std::max)(width, kMinSliderWidth);
}

std::string SettingsMenu::FormatSliderValue(const SliderItem& item, float value) {
    return item.formatValue ? item.formatValue(value) : std::format("{:.2f}", value);
}
