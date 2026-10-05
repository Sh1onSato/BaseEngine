#include "EngineTestScene.h"
#include "AudioManager.h"
#include "Framework.h"
#include "GameSettings.h"
#include "GameTime.h"
#include "Input.h"
#include "SceneManager.h"
#include "Screen.h"
#include "SpriteCommon.h"
#include "WinApp.h"
#ifdef _DEBUG
#include "externals/imgui/imgui.h"
#endif
#include <algorithm>
#include <format>
#include <iterator>

namespace {
    const char* kSeName = "test_se";
    const char* kBgmAName = "test_bgm_a";
    const char* kBgmBName = "test_bgm_b";

    constexpr float kBgmFadeSeconds = 1.0f;
    constexpr float kVolumeStep = 0.1f;
    constexpr float kPitchUpRatio = 1.5f;

    const Vector4 kTitleColors[] = {
        { 1.0f, 1.0f, 1.0f, 1.0f },
        { 1.0f, 0.85f, 0.3f, 1.0f },
        { 0.4f, 0.9f, 1.0f, 1.0f },
        { 1.0f, 0.5f, 0.6f, 1.0f },
    };

    struct WindowSizePreset {
        uint32_t width;
        uint32_t height;
    };
    const WindowSizePreset kWindowSizePresets[] = {
        { 1280, 720 },
        { 1600, 900 },
        { 1920, 1080 },
    };

    struct PadButtonName {
        WORD button;
        const char* name;
    };
    const PadButtonName kPadButtonNames[] = {
        { XINPUT_GAMEPAD_A, "A" },
        { XINPUT_GAMEPAD_B, "B" },
        { XINPUT_GAMEPAD_X, "X" },
        { XINPUT_GAMEPAD_Y, "Y" },
        { XINPUT_GAMEPAD_LEFT_SHOULDER, "LB" },
        { XINPUT_GAMEPAD_RIGHT_SHOULDER, "RB" },
        { XINPUT_GAMEPAD_LEFT_THUMB, "LS" },
        { XINPUT_GAMEPAD_RIGHT_THUMB, "RS" },
        { XINPUT_GAMEPAD_START, "START" },
        { XINPUT_GAMEPAD_BACK, "BACK" },
        { XINPUT_GAMEPAD_DPAD_UP, "↑" },
        { XINPUT_GAMEPAD_DPAD_DOWN, "↓" },
        { XINPUT_GAMEPAD_DPAD_LEFT, "←" },
        { XINPUT_GAMEPAD_DPAD_RIGHT, "→" },
    };

    float StepVolume(float current, float delta) {
        return (std::clamp)(current + delta, 0.0f, 1.0f);
    }
}

void EngineTestScene::Initialize() {
    SpriteCommon* spriteCommon = Framework::GetInstance()->GetSpriteCommon();

    // 既に読み込み済みなら何もしない（シーンを読み直した場合）
    AudioManager* audio = AudioManager::GetInstance();
    audio->Load(kSeName, "Resources/sounds/test_se.wav");
    audio->Load(kBgmAName, "Resources/sounds/test_bgm_a.wav");
    audio->Load(kBgmBName, "Resources/sounds/test_bgm_b.wav");

    titleText = std::make_unique<Text>();
    titleText->Initialize(spriteCommon, 40.0f, L"Yu Gothic UI", true);
    titleText->SetText("エンジン機能テスト (ENGINE_TEST)");
    titleText->SetPosition({ 20.0f, 16.0f });

    helpText = std::make_unique<Text>();
    helpText->Initialize(spriteCommon, 22.0f);
    helpText->SetText(
        "【音】\n"
        "[1] SE再生（連打で重ねがけ）\n"
        "[2] BGM A / [3] BGM B（1秒フェードで切り替え）\n"
        "[4] BGM停止（1秒フェードアウト）\n"
        "[5] BGM一時停止 / 再開\n"
        "[6] BGMピッチ 1.0 ⇔ 1.5\n"
        "[↑][↓] BGM音量  [←][→] SE音量\n"
        "【パッド】\n"
        "[A] SE＋弱い振動0.3秒  [B] 強い振動1秒\n"
        "[V]キー でも振動0.5秒\n"
        "【シーン遷移】\n"
        "[R] 黒フェード(1秒)で読み直し\n"
        "[W] 白フェード(0.5秒)で読み直し\n"
        "【画面・文字】\n"
        "[F11] / [Alt+Enter] フルスクリーン切替\n"
        "[M] 表示モード切替（黒帯 / 画面いっぱい）\n"
        "[9] ウィンドウサイズ 1280x720→1600x900→1920x1080\n"
        "[C] タイトル文字の色を変える");
    helpText->SetPosition({ 20.0f, 80.0f });

    statusText = std::make_unique<Text>();
    statusText->Initialize(spriteCommon, 22.0f, L"MS Gothic");
    statusText->SetPosition({ 640.0f, 80.0f });

    alignText = std::make_unique<Text>();
    alignText->Initialize(spriteCommon, 20.0f);
    alignText->SetText("揃えテスト\n中央揃え (Align::kCenter)\nアンカー右下 (1, 1)");
    alignText->SetAlign(Text::Align::kCenter);
    alignText->SetAnchorPoint({ 1.0f, 1.0f });
    alignText->SetColor({ 0.7f, 1.0f, 0.7f, 1.0f });
}

void EngineTestScene::Finalize() {
    // 読み直し後のシーンは何も鳴っていない状態から始める（画面が暗転し切ったところで止まる）
    AudioManager::GetInstance()->StopBGM();
}

void EngineTestScene::Update() {
    Input* input = Framework::GetInstance()->GetInput();

    UpdateAudio(input);
    UpdatePad(input);
    UpdateSceneChange(input);

    if (input->TriggerKey(DIK_C)) {
        titleColorIndex = (titleColorIndex + 1) % static_cast<int>(std::size(kTitleColors));
        titleText->SetColor(kTitleColors[titleColorIndex]);
    }

    if (input->TriggerKey(DIK_9)) {
        // 今のウィンドウサイズの次のプリセットにする（どれにも当てはまらなければ先頭）。終了時にsettings.jsonへ保存される
        WinApp* winApp = Framework::GetInstance()->GetWinApp();
        uint32_t width = 0;
        uint32_t height = 0;
        winApp->GetWindowedClientSize(width, height);
        size_t next = 0;
        for (size_t i = 0; i < std::size(kWindowSizePresets); ++i) {
            if (kWindowSizePresets[i].width == width && kWindowSizePresets[i].height == height) {
                next = (i + 1) % std::size(kWindowSizePresets);
                break;
            }
        }
        winApp->SetClientSize(kWindowSizePresets[next].width, kWindowSizePresets[next].height);
    }

    if (input->TriggerKey(DIK_M)) {
        Screen::SetScaleMode(Screen::GetScaleMode() == Screen::ScaleMode::kLetterbox ? Screen::ScaleMode::kExpand : Screen::ScaleMode::kLetterbox);
    }
    // 画面いっぱいモードではゲーム座標の幅が変わるので、右下に付けたい文字は毎フレーム右下基準で置き直す
    alignText->SetPosition(Screen::GetAnchoredPosition({ 1.0f, 1.0f }, { -20.0f, -20.0f }));

    fpsTimer += Time::GetUnscaledDeltaTime();
    ++fpsFrameCount;
    if (fpsTimer >= 0.5f) {
        fps = static_cast<float>(fpsFrameCount) / fpsTimer;
        fpsTimer = 0.0f;
        fpsFrameCount = 0;
    }

    statusText->SetText(BuildStatusText(input));

#ifdef _DEBUG
    UpdateDebugUI(input);
#endif

    titleText->Update();
    helpText->Update();
    statusText->Update();
    alignText->Update();
}

#ifdef _DEBUG
void EngineTestScene::UpdateDebugUI(Input* input) {
    // ImGuiの表示位置とクリック位置がバックバッファのサイズ変更後もずれないかの確認用
    // （ImGuiの既定フォントは日本語を表示できないので英語で書く）
    ImGui::Begin("Screen");
    ImGui::Text("BackBuffer: %u x %u", Screen::GetBackBufferWidth(), Screen::GetBackBufferHeight());
    Vector2 gameSize = Screen::GetGameSize();
    ImGui::Text("GameSize: %.0f x %.0f", gameSize.x, gameSize.y);
    const D3D12_VIEWPORT& viewport = Screen::GetViewport();
    ImGui::Text("Viewport: (%.0f, %.0f) %.0f x %.0f", viewport.TopLeftX, viewport.TopLeftY, viewport.Width, viewport.Height);
    ImGui::Text("Mouse (game): %.0f, %.0f", input->GetMousePosition().x, input->GetMousePosition().y);
    ImGui::Text("Mouse (ImGui): %.0f, %.0f", ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y);
    ImGui::Separator();
    if (ImGui::Button("Toggle Fullscreen")) {
        Framework::GetInstance()->GetWinApp()->ToggleFullscreen();
    }
    ImGui::SameLine();
    ImGui::Text("click count: %d", debugClickCount);
    if (ImGui::Button("Click Test")) {
        ++debugClickCount;
    }
    GameSettings* settings = GameSettings::GetInstance();
    float bgmVolume = settings->GetBgmVolume();
    if (ImGui::SliderFloat("BGM Volume", &bgmVolume, 0.0f, 1.0f)) {
        settings->SetBgmVolume(bgmVolume);
    }
    ImGui::End();
}
#endif

void EngineTestScene::Draw() {
    Framework::GetInstance()->GetSpriteCommon()->PreDraw();
    titleText->Draw();
    helpText->Draw();
    statusText->Draw();
    alignText->Draw();
}

void EngineTestScene::UpdateAudio(Input* input) {
    AudioManager* audio = AudioManager::GetInstance();

    if (input->TriggerKey(DIK_1)) {
        audio->Play(kSeName);
    }
    if (input->TriggerKey(DIK_2)) {
        PlayBgm(kBgmAName);
    }
    if (input->TriggerKey(DIK_3)) {
        PlayBgm(kBgmBName);
    }
    if (input->TriggerKey(DIK_4)) {
        audio->StopBGM(kBgmFadeSeconds);
        bgmName.clear();
        isBgmPaused = false;
    }
    if (input->TriggerKey(DIK_5) && !bgmName.empty()) {
        if (isBgmPaused) {
            audio->Resume(audio->GetCurrentBGM());
        } else {
            audio->Pause(audio->GetCurrentBGM());
        }
        isBgmPaused = !isBgmPaused;
    }
    if (input->TriggerKey(DIK_6) && !bgmName.empty()) {
        isPitchUp = !isPitchUp;
        audio->SetPitch(audio->GetCurrentBGM(), isPitchUp ? kPitchUpRatio : 1.0f);
    }

    // 音量はGameSettings経由で変える（AudioManagerへ即反映され、終了時にsettings.jsonへ保存される）
    GameSettings* settings = GameSettings::GetInstance();
    if (input->TriggerKey(DIK_UP)) {
        settings->SetBgmVolume(StepVolume(settings->GetBgmVolume(), kVolumeStep));
    }
    if (input->TriggerKey(DIK_DOWN)) {
        settings->SetBgmVolume(StepVolume(settings->GetBgmVolume(), -kVolumeStep));
    }
    if (input->TriggerKey(DIK_RIGHT)) {
        settings->SetSeVolume(StepVolume(settings->GetSeVolume(), kVolumeStep));
    }
    if (input->TriggerKey(DIK_LEFT)) {
        settings->SetSeVolume(StepVolume(settings->GetSeVolume(), -kVolumeStep));
    }
}

void EngineTestScene::UpdatePad(Input* input) {
    if (input->TriggerPadButton(XINPUT_GAMEPAD_A)) {
        AudioManager::GetInstance()->Play(kSeName);
        input->SetVibration(0.3f, 0.3f, 0.3f);
    }
    if (input->TriggerPadButton(XINPUT_GAMEPAD_B)) {
        input->SetVibration(1.0f, 1.0f, 1.0f);
    }
    if (input->TriggerKey(DIK_V)) {
        input->SetVibration(0.6f, 0.6f, 0.5f);
    }
}

void EngineTestScene::UpdateSceneChange(Input* input) {
    if (input->TriggerKey(DIK_R)) {
        sceneManager->ChangeScene("ENGINE_TEST", 1.0f);
    }
    if (input->TriggerKey(DIK_W)) {
        sceneManager->ChangeScene("ENGINE_TEST", 0.5f, { 1.0f, 1.0f, 1.0f, 1.0f });
    }
}

void EngineTestScene::PlayBgm(const std::string& name) {
    // PlayBGMは再生中の曲があればフェードアウトさせて切り替える
    SoundHandle handle = AudioManager::GetInstance()->PlayBGM(name, 1.0f, kBgmFadeSeconds);
    if (handle == kInvalidSoundHandle) {
        return;
    }
    bgmName = name;
    isBgmPaused = false;
    isPitchUp = false;
}

std::string EngineTestScene::BuildStatusText(Input* input) const {
    AudioManager* audio = AudioManager::GetInstance();
    GameSettings* settings = GameSettings::GetInstance();
    WinApp* winApp = Framework::GetInstance()->GetWinApp();

    std::string bgmState = "なし";
    if (!bgmName.empty()) {
        bgmState = isBgmPaused ? "一時停止" : "再生中";
    }

    std::string text = std::format(
        "FPS: {:.1f}  ({:.2f}ms)\n"
        "フルスクリーン: {}\n"
        "表示モード: {}\n"
        "描画: {}x{}  ゲーム座標: {:.0f}x{:.0f}\n"
        "マウス: ({:.0f}, {:.0f})\n"
        "\n"
        "BGM: {} [{}]\n"
        "ピッチ: {:.1f}\n"
        "音量 Master:{:.1f} BGM:{:.1f} SE:{:.1f}\n"
        "\n",
        fps, fps > 0.0f ? 1000.0f / fps : 0.0f,
        winApp->IsFullscreen() ? "ON" : "OFF",
        Screen::GetScaleMode() == Screen::ScaleMode::kLetterbox ? "黒帯 (kLetterbox)" : "画面いっぱい (kExpand)",
        Screen::GetBackBufferWidth(), Screen::GetBackBufferHeight(), Screen::GetGameSize().x, Screen::GetGameSize().y,
        input->GetMousePosition().x, input->GetMousePosition().y,
        bgmName.empty() ? "-" : bgmName, bgmState,
        isPitchUp ? kPitchUpRatio : 1.0f,
        settings->GetMasterVolume(), settings->GetBgmVolume(), settings->GetSeVolume());

    if (!input->IsPadConnected()) {
        text += "パッド0: 未接続";
        return text;
    }

    Vector2 leftStick = input->GetLeftStick();
    Vector2 rightStick = input->GetRightStick();
    std::string pressed;
    for (const PadButtonName& entry : kPadButtonNames) {
        if (input->PushPadButton(entry.button)) {
            pressed += entry.name;
            pressed += ' ';
        }
    }
    text += std::format(
        "パッド0: 接続\n"
        "左スティック ({:+.2f}, {:+.2f})\n"
        "右スティック ({:+.2f}, {:+.2f})\n"
        "LT {:.2f}  RT {:.2f}\n"
        "押下: {}",
        leftStick.x, leftStick.y, rightStick.x, rightStick.y,
        input->GetLeftTrigger(), input->GetRightTrigger(),
        pressed.empty() ? "-" : pressed);
    return text;
}
