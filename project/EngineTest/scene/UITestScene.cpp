#include "UITestScene.h"
#include "Framework.h"
#include "GameSettings.h"
#include "Input.h"
#include "Screen.h"
#include "ModelManager.h"
#include "SettingsMenu.h"
#include "SrvManager.h"
#include "TextureManager.h"
#include "UIButton.h"
#include "UILabel.h"
#include "UILayoutGroup.h"
#include "UIPanel.h"
#include "UIProgressBar.h"
#include "UIScrollView.h"
#include "UISlider.h"
#include "UITextInput.h"
#include "UIToggle.h"
#include "WinApp.h"
#include <algorithm>
#include <format>
#include <string>

namespace {
    constexpr float kScreenMargin = 16.0f;
    const Vector2 kCornerSize = { 120.0f, 40.0f };
    const Vector2 kWindowSize = { 460.0f, 410.0f };
    const Vector2 kDialogSize = { 400.0f, 180.0f };
    const Vector2 kDialogButtonSize = { 160.0f, 48.0f };
    const Vector4 kDialogDimColor = { 0.0f, 0.0f, 0.0f, 0.55f };
    const Vector4 kDialogColor = { 0.16f, 0.12f, 0.12f, 0.98f };
    const Vector2 kWindowButtonSize = { 320.0f, 48.0f };
    constexpr float kWindowButtonSpacing = 60.0f;
    const Vector4 kCornerColor = { 0.35f, 0.2f, 0.45f, 0.9f };
    const Vector4 kWindowColor = { 0.08f, 0.09f, 0.12f, 0.92f };
    const Vector4 kHelpColor = { 0.85f, 0.85f, 0.85f, 1.0f };

    // 2ページ目
    const Vector2 kSettingsPanelSize = { 420.0f, 360.0f };
    const Vector2 kHpPanelSize = { 420.0f, 300.0f };
    const Vector2 kHpButtonSize = { 80.0f, 40.0f };
    const Vector2 kListButtonSize = { 150.0f, 40.0f };
    constexpr float kMaxHp = 100.0f;

    // 4ページ目
    const Vector2 kScrollViewSize = { 380.0f, 420.0f };
    constexpr float kScrollItemHeight = 40.0f;
    constexpr int kInitialScrollItemCount = 30;
    // この数ごとに見出し（押せない文字）を挟む
    constexpr int kScrollHeadingInterval = 5;
    const char* kFrameTexture = "Resources/textures/test_frame.png";
    constexpr float kFrameSlice = 16.0f;

    // 5ページ目
    constexpr float kTextInputColumnWidth = 420.0f;
    constexpr float kTextInputHeight = 44.0f;

    constexpr int kPageCount = 5;
}

void UITestScene::Initialize() {
    BuildUI();
}

void UITestScene::Finalize() {
    // 画面いっぱいモードのまま次のシーンへ行かないように
    Screen::SetScaleMode(Screen::ScaleMode::kLetterbox);
}

void UITestScene::Update() {
    Input* input = Framework::GetInstance()->GetInput();
    // 文字入力欄に打っている間は、シーンのキー操作を止める（"r" を打つと作り直される、などを防ぐ）
    const bool isTyping = canvas.IsTextInputActive();
    if (!isTyping && input->TriggerKey(DIK_M)) {
        Screen::SetScaleMode(Screen::GetScaleMode() == Screen::ScaleMode::kLetterbox ? Screen::ScaleMode::kExpand
                                                                                       : Screen::ScaleMode::kLetterbox);
    }
    if (!isTyping && input->TriggerKey(DIK_R)) {
        BuildUI();
    }
    if (!isTyping && input->TriggerKey(DIK_N)) {
        canvas.SetNavigationEnabled(!canvas.IsNavigationEnabled());
    }
    if (!isTyping && input->TriggerKey(DIK_TAB)) {
        pageIndex = (pageIndex + 1) % kPageCount;
        BuildUI();
    }
    if (!isTyping && pageIndex == 2 && input->TriggerKey(DIK_F)) {
        // メニューの外で値を変えても、メニューの表示が付いてくるかの確認用
        WinApp* winApp = Framework::GetInstance()->GetWinApp();
        winApp->SetFullscreen(!winApp->IsFullscreen());
    }

    canvas.Update();

    if (pageIndex == 0) {
        // 関数を渡さずに、Update の後で調べるやり方
        if (countButton->IsClicked()) {
            ++clickCount;
        }
        countLabel->SetText(std::format("クリック回数: {}", clickCount));
    } else if (pageIndex == 1) {
        UpdateWidgetPage();
    } else if (pageIndex == 2) {
        UpdateSettingsMenuPage();
    } else if (pageIndex == 3) {
        UpdateScrollPage();
    } else {
        UpdateTextInputPage();
    }
    statusLabel->SetText(BuildStatusText());
}

void UITestScene::Draw() {
    canvas.Draw();
}

void UITestScene::BuildUI() {
    canvas.Clear(); // 前の部品はこのフレームの canvas.Update の最後に消える
    canvas.SetModalRoot(nullptr);
    canvas.SetOnCancel([this]() { OnCancel(); });
    dialog = nullptr;
    clickCount = 0;
    window = nullptr;
    countLabel = nullptr;
    countButton = nullptr;
    targetButton = nullptr;
    dialogButton = nullptr;
    settingsMenu = nullptr;
    openSettingsButton = nullptr;
    settingsInfo = nullptr;
    scrollView = nullptr;
    scrollInfo = nullptr;
    nameInput = nullptr;
    freeInput = nullptr;
    passwordInput = nullptr;
    integerInput = nullptr;
    decimalInput = nullptr;
    textInputInfo = nullptr;

    UILabel* title = canvas.Create<UILabel>(std::format("UIテスト {}/{} (UI_TEST)  [Tab] 次のページ", pageIndex + 1, kPageCount));
    title->SetFont(L"Yu Gothic UI", 24.0f, true);
    title->SetPosition({ kScreenMargin, 12.0f });

    UILabel* help = canvas.Create<UILabel>(
        "[M] 黒帯/画面いっぱい切替  [R] 作り直す  [N] キー操作 ON/OFF\n"
        "[↑↓←→] 選ぶ  [Enter/Space] 決定  [Esc] 閉じる（ダイアログ→ウィンドウ）  パッドは十字キー・スティック・A・B");
    help->SetFontSize(18.0f);
    help->SetColor(kHelpColor);
    help->SetTextAnchor({ 0.0f, 0.0f });
    help->SetPosition({ kScreenMargin, 46.0f });

    statusLabel = canvas.Create<UILabel>();
    statusLabel->SetName("Status");
    statusLabel->SetFont(L"MS Gothic", 18.0f);
    statusLabel->SetTextAnchor({ 1.0f, 0.0f });
    statusLabel->SetAlignment({ 1.0f, 0.0f });
    statusLabel->SetPosition({ -kScreenMargin, 12.0f });

    if (pageIndex == 0) {
        BuildCorners();
        BuildWindow();
        BuildBottomButtons();
    } else if (pageIndex == 1) {
        BuildWidgetPage();
    } else if (pageIndex == 2) {
        BuildSettingsMenuPage();
    } else if (pageIndex == 3) {
        BuildScrollPage();
    } else {
        BuildTextInputPage();
    }
}

void UITestScene::BuildBottomButtons() {
    // 左下: ウィンドウを出し直すボタンと、押すと自分を消すボタン
    UIButton* showButton = canvas.Create<UIButton>("ウィンドウを表示");
    showButton->SetName("ShowButton");
    showButton->SetAlignment({ 0.0f, 1.0f });
    showButton->SetPosition({ kScreenMargin, -kScreenMargin - kCornerSize.y - 12.0f });
    showButton->SetOnClick([this]() {
        window->SetVisible(true);
        canvas.SetFocus(countButton); // パッドですぐ操作できるように
    });

    UIButton* destroyButton = canvas.Create<UIButton>("押すと消えるボタン");
    destroyButton->SetName("DestroyButton");
    destroyButton->SetAlignment({ 0.0f, 1.0f });
    destroyButton->SetPosition({ kScreenMargin, -kScreenMargin - kCornerSize.y - 72.0f });
    destroyButton->SetOnClick([destroyButton]() { destroyButton->Destroy(); }); // クリックの中で消しても安全
}

void UITestScene::BuildCorners() {
    struct Corner {
        Vector2 alignment;
        const char* name;
    };
    // 上の2つは説明の文字と重ならないよう少し下げる
    const Corner corners[] = {
        { { 0.0f, 1.0f }, "左下" },
        { { 1.0f, 1.0f }, "右下" },
        { { 1.0f, 0.0f }, "右上" },
    };
    for (const Corner& corner : corners) {
        UIPanel* panel = canvas.Create<UIPanel>();
        panel->SetName(std::string("Corner ") + corner.name);
        panel->SetAlignment(corner.alignment);
        panel->SetSize(kCornerSize);
        panel->SetColor(kCornerColor);
        const float offsetY = corner.alignment.y == 0.0f ? 190.0f : -kScreenMargin;
        panel->SetPosition({ corner.alignment.x == 0.0f ? kScreenMargin : -kScreenMargin, offsetY });

        UILabel* label = panel->CreateChild<UILabel>(corner.name);
        label->SetFontSize(18.0f);
        label->SetAlignment({ 0.5f, 0.5f });
    }
}

void UITestScene::BuildWindow() {
    window = canvas.Create<UIPanel>();
    window->SetName("Window");
    window->SetAlignment({ 0.5f, 0.5f });
    window->SetSize(kWindowSize);
    window->SetColor(kWindowColor);

    UILabel* header = window->CreateChild<UILabel>("ウィンドウ（パネルの中の部品）");
    header->SetFontSize(20.0f);
    header->SetAlignment({ 0.5f, 0.0f });
    header->SetPosition({ 0.0f, 14.0f });

    countLabel = window->CreateChild<UILabel>("クリック回数: 0");
    countLabel->SetAlignment({ 0.5f, 0.0f });
    countLabel->SetPosition({ 0.0f, 50.0f });

    float y = 96.0f;
    auto createButton = [this, &y](const std::string& text, const std::string& name) {
        UIButton* button = window->CreateChild<UIButton>(text);
        button->SetName(name);
        button->SetAlignment({ 0.5f, 0.0f });
        button->SetPosition({ 0.0f, y });
        button->SetSize(kWindowButtonSize);
        y += kWindowButtonSpacing;
        return button;
    };

    // IsClicked で調べるボタン（Update 参照）
    countButton = createButton("カウント +1（IsClicked）", "CountButton");

    // 関数を渡すボタン
    targetButton = createButton("カウント +10（関数）", "TargetButton");
    targetButton->SetOnClick([this]() { clickCount += 10; });

    UIButton* toggleButton = createButton("上のボタンを押せなくする", "ToggleButton");
    toggleButton->SetOnClick([this, toggleButton]() {
        const bool interactable = !targetButton->IsInteractable();
        targetButton->SetInteractable(interactable);
        toggleButton->SetText(interactable ? "上のボタンを押せなくする" : "上のボタンを押せるようにする");
    });

    dialogButton = createButton("確認ダイアログ（モーダル）", "DialogButton");
    dialogButton->SetOnClick([this]() { OpenDialog(); });

    UIButton* hideButton = createButton("ウィンドウを隠す", "HideButton");
    hideButton->SetOnClick([this]() { window->SetVisible(false); });

    // 手動指定: 一番下で↓を押すと一番上へ、一番上で↑を押すと一番下へ回る（指定しなければ止まる）
    hideButton->SetNavigation(UINavigation::kDown, countButton);
    countButton->SetNavigation(UINavigation::kUp, hideButton);
}

void UITestScene::OpenDialog() {
    if (dialog) {
        return;
    }
    // 画面全体を暗くするパネル（マウスを受け止めるので、後ろのボタンは押せない）
    dialog = canvas.Create<UIPanel>();
    dialog->SetName("DialogDim");
    dialog->SetStretch(true, true);
    dialog->SetColor(kDialogDimColor);

    UIPanel* box = dialog->CreateChild<UIPanel>();
    box->SetName("Dialog");
    box->SetAlignment({ 0.5f, 0.5f });
    box->SetSize(kDialogSize);
    box->SetColor(kDialogColor);

    UILabel* message = box->CreateChild<UILabel>("クリック回数を 0 に戻しますか？");
    message->SetAlignment({ 0.5f, 0.0f });
    message->SetPosition({ 0.0f, 36.0f });

    UIButton* yesButton = box->CreateChild<UIButton>("はい");
    yesButton->SetName("DialogYes");
    yesButton->SetAlignment({ 0.5f, 1.0f });
    yesButton->SetPosition({ -90.0f, -24.0f });
    yesButton->SetSize(kDialogButtonSize);
    yesButton->SetOnClick([this]() {
        clickCount = 0;
        CloseDialog();
    });

    UIButton* noButton = box->CreateChild<UIButton>("いいえ");
    noButton->SetName("DialogNo");
    noButton->SetAlignment({ 0.5f, 1.0f });
    noButton->SetPosition({ 90.0f, -24.0f });
    noButton->SetSize(kDialogButtonSize);
    noButton->SetOnClick([this]() { CloseDialog(); });

    canvas.SetModalRoot(dialog);
    canvas.SetFocus(noButton); // 間違えて決定しても戻せない操作にならない方を選んでおく
}

void UITestScene::CloseDialog() {
    if (!dialog) {
        return;
    }
    dialog->Destroy();
    dialog = nullptr;
    // モーダルを解除してから、開いたボタンに選択を戻す（モーダル中は外の部品を選べないため）
    canvas.SetModalRoot(nullptr);
    canvas.SetFocus(dialogButton);
}

void UITestScene::OnCancel() {
    if (dialog) {
        CloseDialog();
    } else if (settingsMenu && settingsMenu->IsVisible()) {
        CloseSettingsMenu();
    } else if (window && window->IsVisible()) {
        window->SetVisible(false);
    }
}

void UITestScene::BuildWidgetPage() {
    hp = kMaxHp;
    growingItemCount = 0;
    BuildSettingsPanel();
    BuildHpPanel();
    BuildGrowingList();
}

void UITestScene::BuildSettingsPanel() {
    // 左: 背景のパネルの中に縦並び（幅は子をそろえる）
    UIPanel* panel = canvas.Create<UIPanel>();
    panel->SetAlignment({ 0.0f, 0.5f });
    panel->SetPosition({ kScreenMargin, 40.0f });
    panel->SetSize(kSettingsPanelSize);
    panel->SetColor(kWindowColor);

    UILayoutGroup* list = panel->CreateChild<UILayoutGroup>(UILayoutDirection::kVertical);
    list->SetStretch(true, true);
    list->SetPadding(20.0f);
    list->SetSpacing(10.0f);
    list->SetExpandChildren(true);

    UILabel* header = list->CreateChild<UILabel>("設定（縦並び・スライダー・トグル）");
    header->SetFontSize(20.0f);
    header->SetTextAnchor({ 0.0f, 0.5f });

    // 関数で受け取るスライダー
    volumeLabel = list->CreateChild<UILabel>("音量: 0.80");
    volumeLabel->SetTextAnchor({ 0.0f, 0.5f });
    UISlider* volumeSlider = list->CreateChild<UISlider>(0.0f, 1.0f, 0.8f);
    volumeSlider->SetName("VolumeSlider");
    volumeSlider->SetOnValueChanged([this](float value) { volumeLabel->SetText(std::format("音量: {:.2f}", value)); });

    // IsValueChanged で調べるスライダー（刻み 5）
    brightnessLabel = list->CreateChild<UILabel>("明るさ: 50");
    brightnessLabel->SetTextAnchor({ 0.0f, 0.5f });
    brightnessSlider = list->CreateChild<UISlider>(0.0f, 100.0f, 50.0f);
    brightnessSlider->SetName("BrightnessSlider");
    brightnessSlider->SetStep(5.0f);

    UIToggle* subtitleToggle = list->CreateChild<UIToggle>("字幕を表示", true);
    subtitleToggle->SetName("SubtitleToggle");

    UIToggle* lockToggle = list->CreateChild<UIToggle>("明るさを変えられなくする");
    lockToggle->SetName("LockToggle");
    lockToggle->SetOnValueChanged([this](bool isOn) { brightnessSlider->SetInteractable(!isOn); });

    canvas.SetFocus(volumeSlider);
}

void UITestScene::BuildHpPanel() {
    // 右: 進捗バーと、横並びのボタン
    UIPanel* panel = canvas.Create<UIPanel>();
    panel->SetAlignment({ 1.0f, 0.5f });
    panel->SetPosition({ -kScreenMargin, 40.0f });
    panel->SetSize(kHpPanelSize);
    panel->SetColor(kWindowColor);

    UILayoutGroup* column = panel->CreateChild<UILayoutGroup>(UILayoutDirection::kVertical);
    column->SetStretch(true, true);
    column->SetPadding(20.0f);
    column->SetSpacing(12.0f);

    UILabel* header = column->CreateChild<UILabel>("HP（減ると黄色が遅れて追う）");
    header->SetFontSize(20.0f);

    hpBar = column->CreateChild<UIProgressBar>();
    hpBar->SetSize({ 360.0f, 22.0f });
    hpBar->SetTrailSpeed(0.4f);

    // 横並び（大きさはボタンに合わせる）
    UILayoutGroup* row = column->CreateChild<UILayoutGroup>(UILayoutDirection::kHorizontal);
    row->SetSpacing(10.0f);
    row->SetFitContent(true, true);
    const struct {
        const char* text;
        float amount;
    } hpButtons[] = { { "-10", -10.0f }, { "-35", -35.0f }, { "+10", 10.0f }, { "全回復", kMaxHp } };
    for (const auto& hpButton : hpButtons) {
        UIButton* button = row->CreateChild<UIButton>(hpButton.text);
        button->SetSize(kHpButtonSize);
        const float amount = hpButton.amount;
        button->SetOnClick([this, amount]() { hp = std::clamp(hp + amount, 0.0f, kMaxHp); });
    }

    // 縦向きの進捗バー（下から上へ伸びる）を横に並べる
    UILayoutGroup* verticalRow = column->CreateChild<UILayoutGroup>(UILayoutDirection::kHorizontal);
    verticalRow->SetSpacing(12.0f);
    verticalRow->SetFitContent(true, true);
    verticalRow->SetCrossAlignment(1.0f); // 下をそろえる
    hpVerticalBar = verticalRow->CreateChild<UIProgressBar>();
    hpVerticalBar->SetSize({ 24.0f, 100.0f });
    hpVerticalBar->SetFillDirection(UIFillDirection::kBottomToTop);
    UILabel* verticalLabel = verticalRow->CreateChild<UILabel>("← 下から上へ伸びる向き");
    verticalLabel->SetFontSize(18.0f);
}

void UITestScene::BuildGrowingList() {
    // 下中央: 中身に合わせて大きさが変わる縦並び（項目を足すと上に伸びる）
    growingList = canvas.Create<UILayoutGroup>(UILayoutDirection::kVertical);
    growingList->SetAlignment({ 0.5f, 1.0f });
    growingList->SetPosition({ 0.0f, -kScreenMargin - 60.0f });
    growingList->SetSpacing(6.0f);
    growingList->SetFitContent(true, true);
    growingList->SetCrossAlignment(0.5f);

    growingListInfo = canvas.Create<UILabel>();
    growingListInfo->SetAlignment({ 0.5f, 1.0f });
    growingListInfo->SetPosition({ 0.0f, -kScreenMargin - 34.0f });
    growingListInfo->SetFontSize(18.0f);

    UILayoutGroup* buttons = canvas.Create<UILayoutGroup>(UILayoutDirection::kHorizontal);
    buttons->SetAlignment({ 0.5f, 1.0f });
    buttons->SetPosition({ 0.0f, -kScreenMargin });
    buttons->SetFitContent(true, true);
    UIButton* addButton = buttons->CreateChild<UIButton>("項目を追加");
    addButton->SetSize(kListButtonSize);
    addButton->SetOnClick([this]() {
        ++growingItemCount;
        UILabel* item = growingList->CreateChild<UILabel>(std::format("項目 {}", growingItemCount));
        item->SetFontSize(18.0f);
    });
    UIButton* removeButton = buttons->CreateChild<UIButton>("最後を削除");
    removeButton->SetSize(kListButtonSize);
    removeButton->SetOnClick([this]() {
        // 最後の（まだ消していない）項目を消す
        const auto& items = growingList->GetChildren();
        for (auto it = items.rbegin(); it != items.rend(); ++it) {
            if (*it && !(*it)->IsDestroyed()) {
                (*it)->Destroy();
                --growingItemCount;
                break;
            }
        }
    });
}

void UITestScene::UpdateWidgetPage() {
    if (brightnessSlider->IsValueChanged()) {
        brightnessLabel->SetText(std::format("明るさ: {:.0f}", brightnessSlider->GetValue()));
    }
    hpBar->SetValue(hp, kMaxHp);
    hpVerticalBar->SetValue(hp, kMaxHp);
    const Vector2 listSize = growingList->GetSize();
    growingListInfo->SetText(std::format("上の縦並びの大きさ: {:.0f} x {:.0f}（項目 {} 個）", listSize.x, listSize.y,
                                         growingItemCount));
}

void UITestScene::BuildSettingsMenuPage() {
    settingsMenu = canvas.Create<SettingsMenu>("設定");
    settingsMenu->SetName("SettingsMenu");
    settingsMenu->SetAlignment({ 0.5f, 0.5f });
    settingsMenu->SetPosition({ 0.0f, 30.0f });
    settingsMenu->AddDefaultItems();
    // ゲーム固有の項目も、読む関数と書く関数を渡せば足せる
    GameSettings* settings = GameSettings::GetInstance();
    settingsMenu->AddSlider({ .text = "マウス感度",
                              .minValue = GameSettings::kMouseSensitivityMin,
                              .maxValue = GameSettings::kMouseSensitivityMax,
                              .getValue = [settings] { return settings->GetMouseSensitivity(); },
                              .setValue = [settings](float value) { settings->SetMouseSensitivity(value); },
                              .formatValue = [](float value) { return std::format("{:.4f}", value); } });
    settingsMenu->AddToggle(
        "カメラの上下を反転", [settings] { return settings->GetInvertY(); },
        [settings](bool isOn) { settings->SetInvertY(isOn); });
    settingsMenu->AddButton("閉じる", [this]() { CloseSettingsMenu(); });
    settingsMenu->FocusFirstItem();

    // 閉じた後に開き直すボタン
    openSettingsButton = canvas.Create<UIButton>("設定を開く");
    openSettingsButton->SetName("OpenSettingsButton");
    openSettingsButton->SetAlignment({ 0.0f, 1.0f });
    openSettingsButton->SetPosition({ kScreenMargin, -kScreenMargin });
    openSettingsButton->SetSize(kListButtonSize);
    openSettingsButton->SetOnClick([this]() { OpenSettingsMenu(); });

    // GameSettings に入っている値（メニューの表示と一致するか）
    settingsInfo = canvas.Create<UILabel>();
    settingsInfo->SetFont(L"MS Gothic", 18.0f);
    settingsInfo->SetTextAnchor({ 1.0f, 1.0f });
    settingsInfo->SetAlignment({ 1.0f, 1.0f });
    settingsInfo->SetPosition({ -kScreenMargin, -kScreenMargin });
}

void UITestScene::OpenSettingsMenu() {
    settingsMenu->SetVisible(true);
    settingsMenu->FocusFirstItem();
}

void UITestScene::CloseSettingsMenu() {
    settingsMenu->SetVisible(false);
    canvas.SetFocus(openSettingsButton);
}

void UITestScene::UpdateSettingsMenuPage() {
    const GameSettings* settings = GameSettings::GetInstance();
    settingsInfo->SetText(std::format(
        "GameSettings の値  [F] メニューの外でフルスクリーン切替\n"
        "全体 {:.2f}  BGM {:.2f}  効果音 {:.2f}\n"
        "フルスクリーン {}  マウス感度 {:.4f}  上下反転 {}",
        settings->GetMasterVolume(), settings->GetBgmVolume(), settings->GetSeVolume(),
        Framework::GetInstance()->GetWinApp()->IsFullscreen() ? "ON" : "OFF", settings->GetMouseSensitivity(),
        settings->GetInvertY() ? "ON" : "OFF"));
}

void UITestScene::BuildScrollPage() {
    scrollItemCount = 0;
    lastPressedItem = 0;

    // 左: スクロールリスト（項目のボタンと、ときどき見出しの文字）
    scrollView = canvas.Create<UIScrollView>();
    scrollView->SetName("ScrollView");
    scrollView->SetAlignment({ 0.0f, 0.5f });
    scrollView->SetPosition({ kScreenMargin, 0.0f });
    scrollView->SetSize(kScrollViewSize);
    for (int i = 0; i < kInitialScrollItemCount; ++i) {
        AddScrollItem();
    }

    scrollInfo = canvas.Create<UILabel>();
    scrollInfo->SetFontSize(18.0f);
    scrollInfo->SetTextAnchor({ 0.0f, 0.0f });
    scrollInfo->SetAlignment({ 0.0f, 0.5f });
    scrollInfo->SetPosition({ kScreenMargin, kScrollViewSize.y * 0.5f + 30.0f });

    UILayoutGroup* buttons = canvas.Create<UILayoutGroup>(UILayoutDirection::kHorizontal);
    buttons->SetAlignment({ 0.0f, 0.5f });
    buttons->SetPosition({ kScreenMargin, kScrollViewSize.y * 0.5f + 90.0f });
    buttons->SetFitContent(true, true);
    UIButton* addButton = buttons->CreateChild<UIButton>("追加して一番下へ");
    addButton->SetSize({ 200.0f, 40.0f });
    addButton->SetOnClick([this]() {
        AddScrollItem();
        scrollView->ScrollToBottom(); // 足した項目の大きさが決まってから一番下に合わせる
    });
    UIButton* topButton = buttons->CreateChild<UIButton>("一番上へ");
    topButton->SetSize({ 160.0f, 40.0f });
    topButton->SetOnClick([this]() { scrollView->ScrollToTop(); });

    BuildNineSlicePanel();
}

void UITestScene::AddScrollItem() {
    ++scrollItemCount;
    UIElement* content = scrollView->GetContent();
    if (scrollItemCount % kScrollHeadingInterval == 1) {
        UILabel* heading = content->CreateChild<UILabel>(std::format("― {} 〜 ―", scrollItemCount));
        heading->SetFontSize(18.0f);
        heading->SetColor(kHelpColor);
    }
    const int number = scrollItemCount;
    UIButton* item = content->CreateChild<UIButton>(std::format("項目 {}", number));
    item->SetName(std::format("ScrollItem {}", number));
    item->SetSize({ 0.0f, kScrollItemHeight }); // 幅はリストに合わせて自動
    item->SetOnClick([this, number]() { lastPressedItem = number; });
}

void UITestScene::BuildNineSlicePanel() {
    // 右: 同じ画像（48x48 の角の丸い枠）を、9スライスでいろいろな大きさに。一番下は比較用に普通に伸ばした物
    UILayoutGroup* column = canvas.Create<UILayoutGroup>(UILayoutDirection::kVertical);
    column->SetAlignment({ 1.0f, 0.5f });
    column->SetPosition({ -kScreenMargin, 20.0f });
    column->SetSize({ 420.0f, 0.0f });
    column->SetSpacing(14.0f);
    column->SetFitContent(true, false);

    UILabel* header = column->CreateChild<UILabel>("9スライス（同じ 48x48 の画像）");
    header->SetFontSize(20.0f);

    struct SliceSample {
        const char* text;
        Vector2 size;
        Vector4 color;
        float slice;
        float scale;
    };
    const SliceSample samples[] = {
        { "80x80", { 80.0f, 80.0f }, { 0.55f, 0.75f, 1.0f, 1.0f }, kFrameSlice, 1.0f },
        { "横長 400x70", { 400.0f, 70.0f }, { 1.0f, 0.85f, 0.55f, 1.0f }, kFrameSlice, 1.0f },
        { "端を2倍・高さ50（端が入りきらないので縮む）", { 400.0f, 50.0f }, { 0.65f, 1.0f, 0.7f, 1.0f }, kFrameSlice, 2.0f },
        { "比較: 9スライス無し（角がつぶれる）", { 400.0f, 70.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, 0.0f, 1.0f },
    };
    for (const SliceSample& sample : samples) {
        UIPanel* panel = column->CreateChild<UIPanel>();
        panel->SetTexture(kFrameTexture);
        panel->SetSlice(sample.slice);
        panel->SetSliceScale(sample.scale);
        panel->SetSize(sample.size);
        panel->SetColor(sample.color);
        UILabel* label = panel->CreateChild<UILabel>(sample.text);
        label->SetFontSize(16.0f);
        label->SetColor({ 0.1f, 0.1f, 0.1f, 1.0f });
        label->SetAlignment({ 0.5f, 0.5f });
    }

    // ボタンも UIPanel なので、同じように画像の枠にできる（状態の色は画像に掛け算される）
    UIButton* frameButton = column->CreateChild<UIButton>("9スライスのボタン");
    frameButton->SetTexture(kFrameTexture);
    frameButton->SetSlice(kFrameSlice);
    frameButton->SetSize({ 260.0f, 52.0f });
    frameButton->SetTextColor({ 0.05f, 0.05f, 0.1f, 1.0f });
    UIButton::Colors frameColors;
    frameColors.normal = { 0.75f, 0.8f, 0.9f, 1.0f };
    frameColors.hover = { 0.95f, 0.97f, 1.0f, 1.0f };
    frameColors.pressed = { 0.55f, 0.6f, 0.7f, 1.0f };
    frameButton->SetColors(frameColors);
}

void UITestScene::UpdateScrollPage() {
    scrollInfo->SetText(std::format("スクロール {:.0f} / {:.0f} px  項目 {} 個\n最後に押した項目: {}",
                                    scrollView->GetScrollOffset(), scrollView->GetMaxScrollOffset(), scrollItemCount,
                                    lastPressedItem == 0 ? std::string("なし") : std::to_string(lastPressedItem)));
}

void UITestScene::BuildTextInputPage() {
    lastSubmittedText.clear();
    textChangeCount = 0;

    // 左: 種類の違う入力欄を縦に並べる
    UILayoutGroup* column = canvas.Create<UILayoutGroup>(UILayoutDirection::kVertical);
    column->SetAlignment({ 0.0f, 0.5f });
    column->SetPosition({ kScreenMargin, 40.0f });
    column->SetSize({ kTextInputColumnWidth, 0.0f });
    column->SetSpacing(6.0f);
    column->SetExpandChildren(true);
    column->SetFitContent(true, false);

    nameInput = AddTextInputRow(column, "名前（英字・数字・_ だけ、12文字まで）");
    nameInput->SetName("NameInput");
    nameInput->SetPlaceholder("名前を入力");
    nameInput->SetMaxLength(12);
    nameInput->SetFilter(UITextInputFilter::kAlphanumeric);

    freeInput = AddTextInputRow(column, "自由（記号・スペース可。長く打つと横にずれる）");
    freeInput->SetName("FreeInput");
    freeInput->SetText("The quick brown fox jumps over the lazy dog.");

    passwordInput = AddTextInputRow(column, "パスワード（* で隠す）");
    passwordInput->SetName("PasswordInput");
    passwordInput->SetPassword(true);
    passwordInput->SetPlaceholder("password");

    integerInput = AddTextInputRow(column, "整数（数字と先頭の -）");
    integerInput->SetName("IntegerInput");
    integerInput->SetFilter(UITextInputFilter::kInteger);
    integerInput->SetText("-12a3"); // a は入らず -123 になる

    decimalInput = AddTextInputRow(column, "小数（数字と先頭の - と . 1つ）・等幅フォント");
    decimalInput->SetName("DecimalInput");
    decimalInput->SetFilter(UITextInputFilter::kDecimal);
    decimalInput->SetFont(L"Consolas", 22.0f);
    decimalInput->SetPlaceholder("0.0");

    UITextInput* disabledInput = AddTextInputRow(column, "押せない入力欄");
    disabledInput->SetText("disabled");
    disabledInput->SetInteractable(false);

    for (UITextInput* textInput : { nameInput, freeInput, passwordInput, integerInput, decimalInput }) {
        textInput->SetOnChanged([this](const std::string&) { ++textChangeCount; });
        textInput->SetOnSubmit([this](const std::string& text) { lastSubmittedText = text; });
    }

    // 右: 説明と状態
    textInputInfo = canvas.Create<UILabel>();
    textInputInfo->SetFontSize(18.0f);
    textInputInfo->SetTextAnchor({ 0.0f, 0.0f });
    textInputInfo->SetAlignment({ 0.0f, 0.5f });
    textInputInfo->SetPosition({ kScreenMargin * 2.0f + kTextInputColumnWidth, -30.0f });

    // 入力中に押すと、入力が確定して終わるか（入力欄の外のクリック）
    UIButton* outsideButton = canvas.Create<UIButton>("外のボタン（押すと入力が終わる）");
    outsideButton->SetSize({ 360.0f, 44.0f });
    outsideButton->SetAlignment({ 0.0f, 0.5f });
    outsideButton->SetPosition({ kScreenMargin * 2.0f + kTextInputColumnWidth, 200.0f });
    outsideButton->SetOnClick([this]() { lastSubmittedText = "(外のボタンが押された)"; });

    canvas.SetFocus(nameInput);
}

UITextInput* UITestScene::AddTextInputRow(UIElement* column, const std::string& caption) {
    UILabel* label = column->CreateChild<UILabel>(caption);
    label->SetFontSize(16.0f);
    label->SetColor(kHelpColor);
    label->SetTextAnchor({ 0.0f, 0.5f });
    UITextInput* textInput = column->CreateChild<UITextInput>();
    textInput->SetSize({ 0.0f, kTextInputHeight }); // 幅は並べるグループに合わせて自動
    return textInput;
}

void UITestScene::UpdateTextInputPage() {
    textInputInfo->SetText(std::format(
        "クリックか、選んで [Enter] で入力開始\n"
        "[Enter] 確定  [Esc] 打つ前に戻す  外をクリックで確定\n"
        "[←→ Home End] カーソル  [Backspace Delete] 消す  [Ctrl+V] 貼り付け\n"
        "入力中は日本語入力（IME）が OFF になり、[M][R][N][Tab] も効かない\n"
        "\n"
        "入力中: {}\n"
        "名前: \"{}\"\n"
        "自由: \"{}\"\n"
        "パスワード: \"{}\"\n"
        "整数: \"{}\"  小数: \"{}\"\n"
        "変わった回数: {}\n"
        "最後に確定した文字: \"{}\"",
        canvas.IsTextInputActive() ? "はい" : "いいえ",
        nameInput->GetText(), freeInput->GetText(), passwordInput->GetText(),
        integerInput->GetText(), decimalInput->GetText(),
        textChangeCount, lastSubmittedText));
}

std::string UITestScene::BuildStatusText() const {
    const Vector2& mouse = Framework::GetInstance()->GetInput()->GetMousePosition();
    const UIElement* hovered = canvas.GetHoveredElement();
    std::string hoveredName = "なし";
    if (hovered) {
        hoveredName = hovered->GetName().empty() ? "(名前なし)" : hovered->GetName();
    }
    const UIElement* focused = canvas.GetFocusedElement();
    std::string focusedName = "なし";
    if (focused) {
        focusedName = focused->GetName().empty() ? "(名前なし)" : focused->GetName();
    }
    const Vector2 gameSize = Screen::GetGameSize();
    return std::format(
        "マウス ({:.0f}, {:.0f})\n"
        "UIの上: {}\n"
        "乗っている部品: {}\n"
        "選んでいる部品: {}（枠 {}）\n"
        "キー操作: {}  モーダル: {}\n"
        "画面 {:.0f}x{:.0f} {}\n"
        "テクスチャ {} 枚  モデル {} 個  SRV {} / {}",
        mouse.x, mouse.y,
        canvas.IsPointerOverUI() ? "はい" : "いいえ",
        hoveredName,
        focusedName, canvas.IsFocusVisible() ? "表示" : "なし",
        canvas.IsNavigationEnabled() ? "ON" : "OFF", canvas.GetModalRoot() ? "中" : "なし",
        gameSize.x, gameSize.y, Screen::GetScaleMode() == Screen::ScaleMode::kLetterbox ? "黒帯" : "画面いっぱい",
        TextureManager::GetInstance()->GetLoadedCount(), ModelManager::GetInstance()->GetLoadedCount(),
        Framework::GetInstance()->GetSrvManager()->GetUsedCount(),
        SrvManager::kMaxSRVCount);
}
