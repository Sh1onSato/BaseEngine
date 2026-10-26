#pragma once
#include "BaseScene.h"
#include "UICanvas.h"
#include <string>

class UIButton;
class UILabel;
class UILayoutGroup;
class UIPanel;
class UIProgressBar;
class UISlider;
class SettingsMenu;
class UIScrollView;
class UITextInput;

/// <summary>
/// UI（UICanvas と部品）を実行画面で確認するための検証用シーン（ゲーム本編では使わない）。
/// 確認対象: 四隅・中央への配置（[M] で黒帯/画面いっぱいを切り替えても端に付くか）/ パネルの中の部品 /
///           ボタンの見た目（乗る・押す・無効）/ クリック（関数・IsClicked の両方）/ 押したまま外で離すと取り消し /
///           表示・非表示 / クリック中の削除 / 作り直し /
///           キー・パッドでの選択（自動・手動指定）と決定・キャンセル / モーダルの確認ダイアログ / キー操作の ON/OFF。
/// [Tab] で2ページ目: 縦並び・横並び（UILayoutGroup）/ スライダー / トグル / 進捗バー / 中身に合わせて大きさが変わるグループ。
/// 3ページ目: SettingsMenu（GameSettings の音量・フルスクリーン・マウス感度・上下反転）/ 外で値が変わったときの追従（[F]）。
/// 4ページ目: スクロールリスト（ホイール・つまみ・キーで隠れた項目へ）/ はみ出た部分を描かない・押せない / 9スライス。
/// 5ページ目: 文字入力欄（英数字・パスワード・整数・小数・無効）/ 確定・取り消し / 入力中はシーンのキー操作を止める。
/// 起動時に開くには MyGame::Initialize() の最初のシーン名を一時的に "UI_TEST" にする（恒久変更はしないこと）。
/// </summary>
class UITestScene : public BaseScene {
public:
    void Initialize() override;
    void Finalize() override;
    void Update() override;
    void Draw() override;

private:
    // 部品を全部作り直す
    void BuildUI();
    void BuildCorners();
    void BuildWindow();
    // 1ページ目の左下のボタン
    void BuildBottomButtons();
    // 2ページ目
    void BuildWidgetPage();
    void BuildSettingsPanel();
    void BuildHpPanel();
    void BuildGrowingList();
    void UpdateWidgetPage();
    // 3ページ目
    void BuildSettingsMenuPage();
    void OpenSettingsMenu();
    void CloseSettingsMenu();
    void UpdateSettingsMenuPage();
    // 4ページ目
    void BuildScrollPage();
    void BuildNineSlicePanel();
    void AddScrollItem();
    void UpdateScrollPage();
    // 5ページ目
    void BuildTextInputPage();
    UITextInput* AddTextInputRow(UIElement* column, const std::string& caption);
    void UpdateTextInputPage();
    // 確認ダイアログ（モーダル）を開く / 閉じる
    void OpenDialog();
    void CloseDialog();
    // キャンセル（Esc・パッド B）: ダイアログ → ウィンドウの順に閉じる
    void OnCancel();
    std::string BuildStatusText() const;

    UICanvas canvas;

    // 毎フレーム書き換える・操作する部品（canvas が持ち主なので借りるだけ）
    UIPanel* window = nullptr;
    UILabel* countLabel = nullptr;
    UIButton* countButton = nullptr;
    UIButton* targetButton = nullptr;
    UIButton* dialogButton = nullptr;
    UILabel* statusLabel = nullptr;
    // 開いている確認ダイアログ（画面全体を暗くするパネル。閉じていれば nullptr）
    UIPanel* dialog = nullptr;

    int clickCount = 0;

    // 2ページ目
    int pageIndex = 0;
    UILabel* volumeLabel = nullptr;
    UILabel* brightnessLabel = nullptr;
    UISlider* brightnessSlider = nullptr;
    UIProgressBar* hpBar = nullptr;
    UIProgressBar* hpVerticalBar = nullptr;
    UILayoutGroup* growingList = nullptr;
    UILabel* growingListInfo = nullptr;
    float hp = 100.0f;
    int growingItemCount = 0;

    // 3ページ目
    SettingsMenu* settingsMenu = nullptr;
    UIButton* openSettingsButton = nullptr;
    UILabel* settingsInfo = nullptr;

    // 4ページ目
    UIScrollView* scrollView = nullptr;
    UILabel* scrollInfo = nullptr;
    int scrollItemCount = 0;
    int lastPressedItem = 0;

    // 5ページ目
    UITextInput* nameInput = nullptr;
    UITextInput* freeInput = nullptr;
    UITextInput* passwordInput = nullptr;
    UITextInput* integerInput = nullptr;
    UITextInput* decimalInput = nullptr;
    UILabel* textInputInfo = nullptr;
    std::string lastSubmittedText;
    int textChangeCount = 0;
};
