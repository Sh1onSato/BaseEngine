#include "SettingsMenu.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "Input.h"
#include <cstdio>

void SettingsMenu::Initialize(SpriteCommon* spriteCommon, std::vector<Item> newItems) {
    items = std::move(newItems);
    rows.clear();
    rows.resize(items.size());

    for (size_t i = 0; i < items.size(); ++i) {
        Row& row = rows[i];
        Vector2 rowPos = { kMenuPosition.x, kMenuPosition.y + kRowHeight * static_cast<float>(i) };

        row.background = std::make_unique<Sprite>();
        row.background->Initialize(spriteCommon, "white");
        row.background->SetPosition(rowPos);
        row.background->SetSize({ kBarWidth, kBarHeight });
        row.background->SetColor({ 0.1f, 0.1f, 0.1f, 0.8f });

        row.fill = std::make_unique<Sprite>();
        row.fill->Initialize(spriteCommon, "white");
        row.fill->SetPosition(rowPos);
        row.fill->SetSize({ kBarWidth, kBarHeight });
        row.fill->SetColor(items[i].color);

        row.valueText.Initialize(spriteCommon, 7); // "0.0001"のような桁数の多い表示にも対応できる余裕を持たせる
        row.valueText.SetPosition({ rowPos.x + kBarWidth + 10.0f, rowPos.y });
        row.valueText.SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
    }

    cursor = std::make_unique<Sprite>();
    cursor->Initialize(spriteCommon, "white");
    cursor->SetSize(kCursorSize);
    cursor->SetColor({ 1.0f, 1.0f, 0.2f, 1.0f });

    selectedIndex = 0;
}

void SettingsMenu::Update(Input* input, bool isOpen) {
    // isOpenに関わらず、バーの見た目は常に現在値へ追従させておく
    for (size_t i = 0; i < items.size(); ++i) {
        Item& item = items[i];
        float value = item.getValue ? item.getValue() : 0.0f;
        float range = item.maxValue - item.minValue;
        float ratio = (range > 0.0001f) ? (value - item.minValue) / range : 0.0f;
        if (ratio < 0.0f) ratio = 0.0f;
        if (ratio > 1.0f) ratio = 1.0f;

        rows[i].fill->SetSize({ kBarWidth * ratio, kBarHeight });

        if (!item.isBool) {
            char fmt[8];
            snprintf(fmt, sizeof(fmt), "%%.%df", item.displayDecimals);
            char buf[16];
            snprintf(buf, sizeof(buf), fmt, value);
            rows[i].valueText.SetText(buf);
        } else {
            rows[i].valueText.SetText(""); // bool項目はバーの満/空だけで表現し、数値は出さない
        }

        rows[i].background->Update();
        rows[i].fill->Update();
        rows[i].valueText.Update();
    }

    if (!isOpen || items.empty()) return;

    if (input->TriggerKey(DIK_DOWN)) {
        selectedIndex = (selectedIndex + 1) % static_cast<int>(items.size());
    }
    if (input->TriggerKey(DIK_UP)) {
        selectedIndex = (selectedIndex - 1 + static_cast<int>(items.size())) % static_cast<int>(items.size());
    }

    Item& selected = items[selectedIndex];
    if (selected.isBool) {
        // bool項目は左右キーでON/OFFを直接指定する（押しっぱなし対応は不要なのでTriggerKey）
        if (input->TriggerKey(DIK_RIGHT)) selected.setValue(1.0f);
        if (input->TriggerKey(DIK_LEFT)) selected.setValue(0.0f);
    } else {
        float current = selected.getValue();
        if (input->PushKey(DIK_RIGHT)) {
            current += selected.step;
            if (current > selected.maxValue) current = selected.maxValue;
            selected.setValue(current);
        }
        if (input->PushKey(DIK_LEFT)) {
            current -= selected.step;
            if (current < selected.minValue) current = selected.minValue;
            selected.setValue(current);
        }
    }

    Vector2 cursorPos = { kMenuPosition.x - kCursorSize.x - 6.0f, kMenuPosition.y + kRowHeight * static_cast<float>(selectedIndex) };
    cursor->SetPosition(cursorPos);
    cursor->Update();
}

void SettingsMenu::Draw() {
    for (auto& row : rows) {
        row.background->Draw();
        row.fill->Draw();
        row.valueText.Draw();
    }
    if (!rows.empty()) {
        cursor->Draw();
    }
}
