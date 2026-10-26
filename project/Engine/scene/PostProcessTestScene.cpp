#include "PostProcessTestScene.h"
#include "Camera.h"
#include "CameraManager.h"
#include "Framework.h"
#include "GameTime.h"
#include "Input.h"
#include "ModelManager.h"
#include "Object3d.h"
#include "Screen.h"
#include "SpriteCommon.h"
#include <array>
#include <cmath>
#include <format>

namespace {
    const char* kCameraName = "PostProcessTest";
    constexpr int kUiLayer = 100;
    constexpr float kAutoCycleSeconds = 2.5f; // 見本を自動で切り替える間隔
    constexpr float kDamageSeconds = 0.6f;    // 被弾の赤い縁が消えるまでの秒数（見本では繰り返す）

    const std::array<const char*, 8> kPresetNames = {
        "効果なし", "ブルーム", "画面全体のぼかし", "白黒（彩度 0）", "セピア", "ヴィネット（黒）", "被弾の赤い縁（時間で薄くなる）", "夕方の色味",
    };

    // 置く物（位置・大きさ・色・光の当たり方を無視して明るく光るか）
    struct Placement {
        bool isSphere;
        Vector3 position;
        Vector3 scale;
        Vector4 color;
        bool isGlowing;
    };
    const Placement kPlacements[] = {
        { false, { 0.0f, -0.1f, 0.0f }, { 20.0f, 0.2f, 12.0f }, { 0.5f, 0.5f, 0.55f, 1.0f }, false }, // 床
        { true, { -6.0f, 1.0f, 1.0f }, { 2.0f, 2.0f, 2.0f }, { 0.9f, 0.25f, 0.2f, 1.0f }, false },
        { true, { -3.0f, 1.0f, 1.0f }, { 2.0f, 2.0f, 2.0f }, { 0.95f, 0.75f, 0.2f, 1.0f }, false },
        { true, { 0.0f, 1.0f, 1.0f }, { 2.0f, 2.0f, 2.0f }, { 0.3f, 0.8f, 0.3f, 1.0f }, false },
        { true, { 3.0f, 1.0f, 1.0f }, { 2.0f, 2.0f, 2.0f }, { 0.25f, 0.45f, 0.95f, 1.0f }, false },
        { true, { 6.0f, 1.0f, 1.0f }, { 2.0f, 2.0f, 2.0f }, { 0.9f, 0.9f, 0.9f, 1.0f }, false },
        // 光って見える物（ライトを無視して色そのまま = 明るさ 1 前後なのでブルームで光る）
        { true, { -4.0f, 3.5f, 4.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, true },
        { true, { 0.0f, 3.5f, 4.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 0.95f, 0.5f, 1.0f }, true },
        { true, { 4.0f, 3.5f, 4.0f }, { 1.0f, 1.0f, 1.0f }, { 0.5f, 1.0f, 1.0f, 1.0f }, true },
        { false, { 0.0f, 0.5f, -2.5f }, { 6.0f, 0.15f, 0.15f }, { 1.0f, 1.0f, 1.0f, 1.0f }, true }, // 光る棒
    };
}

PostProcessTestScene::PostProcessTestScene() = default;
// Object3d の中身はヘッダーで見えないので、デストラクタは .cpp に置く
PostProcessTestScene::~PostProcessTestScene() = default;

void PostProcessTestScene::Initialize() {
    SetupCamera();
    InitializeTexts();
    InitializeObjects();
}

void PostProcessTestScene::SetupCamera() {
    // 一度空にしてから登録する（シーン読み直しで同じ名前を登録しても assert にならない）
    CameraManager* cameraManager = CameraManager::GetInstance();
    cameraManager->Initialize();
    std::unique_ptr<Camera> newCamera = std::make_unique<Camera>();
    camera = newCamera.get();
    camera->SetTranslate({ 0.0f, 6.0f, -13.0f });
    camera->SetRotate({ 0.35f, 0.0f, 0.0f });
    camera->Update();
    cameraManager->AddCamera(kCameraName, std::move(newCamera));
    cameraManager->SetActiveCamera(kCameraName);
}

void PostProcessTestScene::InitializeTexts() {
    SpriteCommon* spriteCommon = Framework::GetInstance()->GetSpriteCommon();

    titleText = std::make_unique<Text>();
    titleText->Initialize(spriteCommon, 24.0f, L"Yu Gothic UI", true);
    titleText->SetText("画面の効果テスト (POSTPROCESS_TEST)");
    titleText->SetLayer(kUiLayer);

    helpText = std::make_unique<Text>();
    helpText->Initialize(spriteCommon, 18.0f);
    helpText->SetText("[←][→] 見本を切り替え  [Space] 自動で切り替え ON/OFF  [M] 黒帯  [F6] 調整の窓  [F9] 内部解像度");
    helpText->SetLayer(kUiLayer);

    statusText = std::make_unique<Text>();
    statusText->Initialize(spriteCommon, 22.0f);
    statusText->SetLayer(kUiLayer);
}

void PostProcessTestScene::InitializeObjects() {
    Object3dCommon* object3dCommon = Framework::GetInstance()->GetObject3dCommon();
    ModelManager* models = ModelManager::GetInstance();
    for (const Placement& placement : kPlacements) {
        auto object = std::make_unique<Object3d>();
        object->Initialize(object3dCommon);
        object->SetModel(placement.isSphere ? models->GetSphereModel() : models->GetCubeModel());
        object->SetTranslate(placement.position);
        object->SetScale(placement.scale);
        object->SetColor(placement.color);
        object->SetEnableLighting(!placement.isGlowing);
        objects.push_back(std::move(object));
    }
}

void PostProcessTestScene::Finalize() {
    // 画面の効果は SceneManager がシーンの切り替えで既定に戻す
    Screen::SetScaleMode(Screen::ScaleMode::kLetterbox); // [M] で変えた表示モードを戻す（MyGame の既定）
    // 登録したカメラを消す（次のシーンに残さない）
    CameraManager::GetInstance()->Initialize();
}

void PostProcessTestScene::Update() {
    UpdateInput();
    presetTimer += Time::GetDeltaTime();
    if (isAutoCycle && presetTimer >= kAutoCycleSeconds) {
        presetIndex = (presetIndex + 1) % kPresetNames.size();
        presetTimer = 0.0f;
    }
    // 毎フレーム作り直して入れる（被弾の赤い縁のように、値を時間で動かす使い方の例）
    Framework::GetInstance()->GetPostProcess()->GetSettings() = MakePreset(presetIndex, presetTimer);

    camera->Update();
    for (const std::unique_ptr<Object3d>& object : objects) {
        object->Update();
    }

    statusText->SetText(BuildStatusText());
    // 画面いっぱいモードではゲーム座標の幅が変わるので、端に付ける文字は毎フレーム置き直す
    titleText->SetPosition(Screen::GetAnchoredPosition({ 0.0f, 0.0f }, { 16.0f, 12.0f }));
    helpText->SetPosition(Screen::GetAnchoredPosition({ 0.0f, 0.0f }, { 16.0f, 46.0f }));
    statusText->SetPosition(Screen::GetAnchoredPosition({ 0.0f, 0.0f }, { 16.0f, 76.0f }));
    titleText->Update();
    helpText->Update();
    statusText->Update();
}

void PostProcessTestScene::UpdateInput() {
    Input* input = Framework::GetInstance()->GetInput();
    if (input->TriggerKey(DIK_RIGHT)) {
        presetIndex = (presetIndex + 1) % kPresetNames.size();
        presetTimer = 0.0f;
    }
    if (input->TriggerKey(DIK_LEFT)) {
        presetIndex = (presetIndex + kPresetNames.size() - 1) % kPresetNames.size();
        presetTimer = 0.0f;
    }
    if (input->TriggerKey(DIK_SPACE)) {
        isAutoCycle = !isAutoCycle;
        presetTimer = 0.0f;
    }
    if (input->TriggerKey(DIK_M)) {
        Screen::SetScaleMode(Screen::GetScaleMode() == Screen::ScaleMode::kLetterbox ? Screen::ScaleMode::kExpand
                                                                                     : Screen::ScaleMode::kLetterbox);
    }
}

PostEffectSettings PostProcessTestScene::MakePreset(size_t index, float time) const {
    PostEffectSettings effect;
    switch (index) {
    case 1:
        effect.bloomEnabled = true;
        effect.bloomThreshold = 0.75f;
        effect.bloomIntensity = 1.2f;
        effect.bloomRadius = 2.0f;
        break;
    case 2:
        effect.blur = 2.5f;
        break;
    case 3:
        effect.saturation = 0.0f;
        break;
    case 4:
        effect.sepia = 1.0f;
        break;
    case 5:
        effect.vignetteIntensity = 0.8f;
        effect.vignetteRadius = 0.4f;
        break;
    case 6: {
        // 被弾した瞬間が一番濃く、kDamageSeconds で消える（見本では繰り返す）
        const float t = std::fmod(time, kDamageSeconds) / kDamageSeconds;
        effect.vignetteIntensity = 0.7f * (1.0f - t);
        effect.vignetteRadius = 0.35f;
        effect.vignetteColor = { 0.7f, 0.0f, 0.0f };
        break;
    }
    case 7:
        effect.tint = { 1.0f, 0.8f, 0.6f };
        effect.contrast = 1.1f;
        effect.saturation = 1.2f;
        effect.brightness = -0.03f;
        effect.vignetteIntensity = 0.35f;
        break;
    default:
        break;
    }
    return effect;
}

void PostProcessTestScene::Draw() {
    // --- シャドウパス（ライト視点の深度のみ） ---
    DrawShadowPass();
    // シャドウパスで外れた描画先を戻す（画面の効果があるときは別の描画先なので、バックバッファを直接指定しない）
    Framework::GetInstance()->GetDxCommon()->BindMainRenderTarget();

    // --- 通常の描画パス ---
    Framework::GetInstance()->GetObject3dCommon()->PreDraw();
    for (const std::unique_ptr<Object3d>& object : objects) {
        object->Draw();
    }

    titleText->Draw();
    helpText->Draw();
    statusText->Draw();
}

void PostProcessTestScene::DrawShadowPass() {
    Object3dCommon* object3dCommon = Framework::GetInstance()->GetObject3dCommon();
    object3dCommon->PreDrawShadow();
    const Matrix4x4 lightViewProjection = object3dCommon->GetLightViewProjection();
    for (const std::unique_ptr<Object3d>& object : objects) {
        object->DrawShadow(lightViewProjection);
    }
    object3dCommon->PostDrawShadow();
}

std::string PostProcessTestScene::BuildStatusText() const {
    return std::format("見本 {}/{}: {}\n自動で切り替え: {}",
                       presetIndex + 1, kPresetNames.size(), kPresetNames[presetIndex], isAutoCycle ? "ON" : "OFF");
}
