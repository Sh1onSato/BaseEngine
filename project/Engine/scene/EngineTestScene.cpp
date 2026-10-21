#include "EngineTestScene.h"
#include "AudioManager.h"
#include "Calculation.h"
#include "Framework.h"
#include "GameSettings.h"
#include "GameTime.h"
#include "Input.h"
#include "SceneManager.h"
#include "Screen.h"
#include "SpriteCommon.h"
#include "TextureManager.h"
#include "WinApp.h"
#ifdef _DEBUG
#include "externals/imgui/imgui.h"
#endif
#include <algorithm>
#include <cmath>
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

    // スプライトテスト（画面左下に配置。画面いっぱいモードでも左下に付くよう毎フレーム置き直す）
    const Vector2 kSpriteTestOffset = { 20.0f, -100.0f }; // 左下基準のオフセット（テスト領域の左上）
    constexpr float kBlendSquareSize = 64.0f;
    const Vector4 kBlendBaseColors[] = {
        { 1.0f, 0.2f, 0.2f, 1.0f },
        { 0.2f, 1.0f, 0.2f, 1.0f },
        { 0.2f, 0.4f, 1.0f, 1.0f },
    };
    // レイヤーテストの表示用（kBlendBaseColorsと同じ並び）
    const char* const kBlendBaseColorNames[] = { "赤", "緑", "青" };
    const Vector4 kBlendTopColor = { 1.0f, 0.85f, 0.3f, 0.6f }; // 通常ブレンドで半透明が分かるようα=0.6
    // digitFontアトラスの先頭5グリフ（7x9セル×5）を6倍に拡大して表示する
    const Vector2 kFilterTexSize = { 35.0f, 9.0f };
    constexpr float kFilterScale = 6.0f;
    constexpr float kFilterOffsetX = 200.0f; // テスト領域左端からのX位置

    // 2Dカメラテスト（ワールド座標。原点にキャラ、原点中心にグリッド背景）
    const char* kTestSheetPath = "Resources/textures/test_sheet.png";
    const char* kTestGridPath = "Resources/textures/test_grid.png";
    constexpr int kWorldSpriteLayer = -10;            // UI（レイヤー0〜）より奥
    constexpr float kGridWorldSize = 2048.0f;         // グリッド背景の大きさ（256pxの画像をWRAPで8x8枚並べる）
    const Vector2 kSheetFrameSize = { 64.0f, 64.0f }; // test_sheet.png の1コマ
    constexpr uint32_t kSheetColumns = 4;
    constexpr uint32_t kSheetRows = 2;
    constexpr float kCharacterSize = 128.0f;
    const char* kIdleClip = "idle";
    const char* kSpinClip = "spin";
    constexpr float kCameraMoveSpeed = 300.0f;        // 画面上で1秒に動くピクセル数（ズームに関係なく同じ見た目の速さ）
    constexpr float kCameraZoomPerSecond = 2.0f;      // 1秒押し続けるとズームが2倍(1/2倍)になる
    constexpr float kCameraMinZoom = 0.25f;
    constexpr float kCameraMaxZoom = 4.0f;
    constexpr float kCameraRotateSpeed = Calculation::kPi * 0.5f; // 1秒で90度

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
    // 左下のスプライトテスト領域と重ならないよう、行をまとめて文字も少し小さくしている
    helpText->Initialize(spriteCommon, 20.0f);
    helpText->SetText(
        "【音】\n"
        "[1] SE再生（連打で重ねがけ）\n"
        "[2] BGM A / [3] BGM B（1秒フェードで切り替え）\n"
        "[4] BGM停止  [5] 一時停止/再開  [6] ピッチ1.5\n"
        "[↑][↓] BGM音量  [←][→] SE音量\n"
        "【パッド】\n"
        "[A] SE＋弱い振動0.3秒  [B] 強い振動1秒\n"
        "[V]キー でも振動0.5秒\n"
        "【シーン遷移】\n"
        "[R] 黒(1秒) / [W] 白(0.5秒) フェードで読み直し\n"
        "【画面・文字】\n"
        "[F11]/[Alt+Enter] フルスクリーン  [M] 表示モード\n"
        "[9] ウィンドウサイズ切替  [C] タイトルの色\n"
        "【スプライト（左下）】\n"
        "[B]キー ブレンド  [N]キー フィルタ  [L] レイヤー回転\n"
        "【2Dカメラ・アニメ（中央のワールド）】\n"
        "[T][F][G][H] 移動  [Q][E] ズーム  [Z][X] 回転\n"
        "[0] カメラリセット  [K] spinを1回再生");
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

    InitializeSpriteTest();
    InitializeCameraTest();
}

void EngineTestScene::InitializeCameraTest() {
    SpriteCommon* spriteCommon = Framework::GetInstance()->GetSpriteCommon();
    // SetUseCamera(true) のSpriteはこのカメラで描かれる（Finalizeで必ず外す）
    camera2D = Camera2D();
    spriteCommon->SetCamera2D(&camera2D);

    TextureManager::GetInstance()->LoadTexture(kTestGridPath);
    TextureManager::GetInstance()->LoadTexture(kTestSheetPath);

    // グリッド背景：切り出し範囲を画像より大きくして、WRAPサンプラーでタイル状に繰り返す
    worldGridSprite = std::make_unique<Sprite>();
    worldGridSprite->Initialize(spriteCommon, kTestGridPath);
    worldGridSprite->SetTexLeftTop({ 0.0f, 0.0f });
    worldGridSprite->SetTexSize({ kGridWorldSize, kGridWorldSize });
    worldGridSprite->SetSize({ kGridWorldSize, kGridWorldSize });
    worldGridSprite->SetAnchorPoint({ 0.5f, 0.5f });
    worldGridSprite->SetPosition({ 0.0f, 0.0f });
    worldGridSprite->SetUseCamera(true);
    worldGridSprite->SetLayer(kWorldSpriteLayer);

    // アニメーションするキャラ（ドット絵風にくっきり見えるようポイントフィルタ）
    characterSprite = std::make_unique<Sprite>();
    characterSprite->Initialize(spriteCommon, kTestSheetPath);
    characterSprite->SetSize({ kCharacterSize, kCharacterSize });
    characterSprite->SetAnchorPoint({ 0.5f, 0.5f });
    characterSprite->SetPosition({ 0.0f, 0.0f });
    characterSprite->SetTextureFilter(Sprite::TextureFilter::kPoint);
    characterSprite->SetUseCamera(true);
    characterSprite->SetLayer(kWorldSpriteLayer);

    characterAnimation = SpriteAnimation();
    characterAnimation.SetSheet(kSheetColumns, kSheetRows, kSheetFrameSize);
    characterAnimation.AddClip(kIdleClip, 0, 4, 6.0f, true);   // 1〜4コマ目をループ
    characterAnimation.AddClip(kSpinClip, 4, 4, 12.0f, false); // 5〜8コマ目を1回だけ
    characterAnimation.Play(kIdleClip);
}

void EngineTestScene::UpdateCameraTest(Input* input) {
    UpdateCameraControl(input);

    // [K]でspinを最初から1回再生し、終わったらidleに戻す
    if (input->TriggerKey(DIK_K)) {
        characterAnimation.Play(kSpinClip, true);
    }
    if (characterAnimation.GetCurrentClipName() == kSpinClip && characterAnimation.IsFinished()) {
        characterAnimation.Play(kIdleClip);
    }
    characterAnimation.Update(Time::GetDeltaTime());
    // 切り出し範囲を変えてから Update で頂点に反映する
    characterAnimation.Apply(*characterSprite);

    worldGridSprite->Update();
    characterSprite->Update();
}

void EngineTestScene::UpdateCameraControl(Input* input) {
    const float deltaTime = Time::GetDeltaTime();

    // 移動：画面上の上下左右に動くよう、画面方向の移動量をカメラの回転・ズームでワールドに戻す
    Vector2 screenDir = { 0.0f, 0.0f };
    if (input->PushKey(DIK_T)) { screenDir.y -= 1.0f; }
    if (input->PushKey(DIK_G)) { screenDir.y += 1.0f; }
    if (input->PushKey(DIK_F)) { screenDir.x -= 1.0f; }
    if (input->PushKey(DIK_H)) { screenDir.x += 1.0f; }
    float c = std::cos(camera2D.GetRotation());
    float s = std::sin(camera2D.GetRotation());
    Vector2 worldDir = { screenDir.x * c - screenDir.y * s, screenDir.x * s + screenDir.y * c };
    camera2D.Move(worldDir * (kCameraMoveSpeed * deltaTime / camera2D.GetZoom()));

    // ズーム：掛け算で変える（どの倍率でも同じ感覚で拡大縮小する）
    float zoomRate = 0.0f;
    if (input->PushKey(DIK_E)) { zoomRate += 1.0f; }
    if (input->PushKey(DIK_Q)) { zoomRate -= 1.0f; }
    float zoom = camera2D.GetZoom() * std::pow(kCameraZoomPerSecond, zoomRate * deltaTime);
    camera2D.SetZoom((std::clamp)(zoom, kCameraMinZoom, kCameraMaxZoom));

    // 回転
    if (input->PushKey(DIK_Z)) { camera2D.SetRotation(camera2D.GetRotation() - kCameraRotateSpeed * deltaTime); }
    if (input->PushKey(DIK_X)) { camera2D.SetRotation(camera2D.GetRotation() + kCameraRotateSpeed * deltaTime); }

    if (input->TriggerKey(DIK_0)) {
        camera2D = Camera2D();
    }
}

void EngineTestScene::InitializeSpriteTest() {
    SpriteCommon* spriteCommon = Framework::GetInstance()->GetSpriteCommon();

    // ブレンドテスト用の四角（内部生成の1x1白テクスチャを色で塗る）
    for (size_t i = 0; i < kBlendBaseSpriteCount; ++i) {
        blendBaseSprites[i] = std::make_unique<Sprite>();
        blendBaseSprites[i]->Initialize(spriteCommon, "white");
        blendBaseSprites[i]->SetSize({ kBlendSquareSize, kBlendSquareSize });
        blendBaseSprites[i]->SetColor(kBlendBaseColors[i]);
    }
    blendTopSprite = std::make_unique<Sprite>();
    blendTopSprite->Initialize(spriteCommon, "white");
    blendTopSprite->SetSize({ kBlendSquareSize * 1.5f, kBlendSquareSize });
    blendTopSprite->SetColor(kBlendTopColor);
    blendTopSprite->SetBlendMode(blendTestMode);
    blendTopSprite->SetLayer(kBlendTopLayer); // 3色のレイヤーをどう回しても常に一番手前
    ApplyLayerTest();

    // フィルタテスト用の拡大文字（"digitFont"は未生成なら生成される）
    TextureManager::GetInstance()->CreateInternalDigitFontTexture();
    filterTestSprite = std::make_unique<Sprite>();
    filterTestSprite->Initialize(spriteCommon, "digitFont");
    filterTestSprite->SetTexLeftTop({ 0.0f, 0.0f });
    filterTestSprite->SetTexSize(kFilterTexSize);
    filterTestSprite->SetSize({ kFilterTexSize.x * kFilterScale, kFilterTexSize.y * kFilterScale });
    filterTestSprite->SetTextureFilter(usePointFilter ? Sprite::TextureFilter::kPoint : Sprite::TextureFilter::kLinear);
}

void EngineTestScene::UpdateSpriteTest(Input* input) {
    if (input->TriggerKey(DIK_B)) {
        size_t next = (static_cast<size_t>(blendTestMode) + 1) % kSpriteBlendModeCount;
        blendTestMode = static_cast<SpriteBlendMode>(next);
        blendTopSprite->SetBlendMode(blendTestMode);
    }
    if (input->TriggerKey(DIK_N)) {
        usePointFilter = !usePointFilter;
        filterTestSprite->SetTextureFilter(usePointFilter ? Sprite::TextureFilter::kPoint : Sprite::TextureFilter::kLinear);
    }
    if (input->TriggerKey(DIK_L)) {
        layerTestShift = (layerTestShift + 1) % static_cast<int>(kBlendBaseSpriteCount);
        ApplyLayerTest();
    }

    // 左下基準で配置（3色は16pxずつ重ね、黄色はその上に跨るように置く）
    Vector2 origin = Screen::GetAnchoredPosition({ 0.0f, 1.0f }, kSpriteTestOffset);
    const float step = kBlendSquareSize * 0.75f;
    for (size_t i = 0; i < kBlendBaseSpriteCount; ++i) {
        blendBaseSprites[i]->SetPosition({ origin.x + step * static_cast<float>(i), origin.y });
        blendBaseSprites[i]->Update();
    }
    blendTopSprite->SetPosition({ origin.x + kBlendSquareSize * 0.5f, origin.y + kBlendSquareSize * 0.5f });
    blendTopSprite->Update();
    filterTestSprite->SetPosition({ origin.x + kFilterOffsetX, origin.y });
    filterTestSprite->Update();
}

void EngineTestScene::ApplyLayerTest() {
    // i番目の四角のレイヤー = (i + shift) % 3。shiftを進めるたびに一番手前の四角が 青→緑→赤 と変わる
    for (size_t i = 0; i < kBlendBaseSpriteCount; ++i) {
        int layer = (static_cast<int>(i) + layerTestShift) % static_cast<int>(kBlendBaseSpriteCount);
        blendBaseSprites[i]->SetLayer(layer);
    }
}

size_t EngineTestScene::GetFrontBlendBaseIndex() const {
    size_t frontIndex = 0;
    for (size_t i = 1; i < kBlendBaseSpriteCount; ++i) {
        if (blendBaseSprites[i]->GetLayer() > blendBaseSprites[frontIndex]->GetLayer()) {
            frontIndex = i;
        }
    }
    return frontIndex;
}

void EngineTestScene::Finalize() {
    // 読み直し後のシーンは何も鳴っていない状態から始める（画面が暗転し切ったところで止まる）
    AudioManager::GetInstance()->StopBGM();
    // camera2Dはこのシーンと一緒に破棄されるので、SpriteCommonに残すとダングリングポインタになる
    Framework::GetInstance()->GetSpriteCommon()->SetCamera2D(nullptr);
}

void EngineTestScene::Update() {
    Input* input = Framework::GetInstance()->GetInput();

    UpdateAudio(input);
    UpdatePad(input);
    UpdateSceneChange(input);
    UpdateSpriteTest(input);
    UpdateCameraTest(input);

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
    // Drawは描画キューに積むだけ。実際の描画はSceneManagerがシーンのDraw後にFlushでレイヤー順に行う
    // 2Dカメラテスト（ワールド座標。レイヤー-10なのでUIより奥に描かれる）
    worldGridSprite->Draw();
    characterSprite->Draw();

    // スプライトテスト（3色はこの呼び出し順のまま固定。前後関係は[L]で回すレイヤーで決まる。
    // 黄色の四角はレイヤー10なので常に手前。ブレンドモードの切替はFlush時に行われる）
    for (const std::unique_ptr<Sprite>& sprite : blendBaseSprites) {
        sprite->Draw();
    }
    blendTopSprite->Draw();
    filterTestSprite->Draw();

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
    // マウスのゲーム座標をカメラで逆変換して、指しているワールド座標を出す
    Vector2 mouseWorld = camera2D.ScreenToWorld(input->GetMousePosition());

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
        "\n"
        "ブレンド: {}  フィルタ: {}\n"
        "レイヤー: {}が手前\n"
        "\n"
        "カメラ: ({:.0f}, {:.0f}) x{:.2f} {:.0f}度\n"
        "マウス(ワールド): ({:.0f}, {:.0f})\n"
        "アニメ: {} コマ{}\n"
        "\n",
        fps, fps > 0.0f ? 1000.0f / fps : 0.0f,
        winApp->IsFullscreen() ? "ON" : "OFF",
        Screen::GetScaleMode() == Screen::ScaleMode::kLetterbox ? "黒帯 (kLetterbox)" : "画面いっぱい (kExpand)",
        Screen::GetBackBufferWidth(), Screen::GetBackBufferHeight(), Screen::GetGameSize().x, Screen::GetGameSize().y,
        input->GetMousePosition().x, input->GetMousePosition().y,
        bgmName.empty() ? "-" : bgmName, bgmState,
        isPitchUp ? kPitchUpRatio : 1.0f,
        settings->GetMasterVolume(), settings->GetBgmVolume(), settings->GetSeVolume(),
        GetSpriteBlendModeName(blendTestMode), usePointFilter ? "Point" : "Linear",
        kBlendBaseColorNames[GetFrontBlendBaseIndex()],
        camera2D.GetPosition().x, camera2D.GetPosition().y, camera2D.GetZoom(),
        camera2D.GetRotation() * 180.0f / Calculation::kPi,
        mouseWorld.x, mouseWorld.y,
        characterAnimation.GetCurrentClipName(), characterAnimation.GetCurrentFrame() + 1);

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
