#include "AnimationTestScene.h"
#include "AudioManager.h"
#include "Camera.h"
#include "CameraManager.h"
#include "DebugDraw.h"
#include "Framework.h"
#include "GameTime.h"
#include "Input.h"
#include "ModelManager.h"
#include "Object3d.h"
#include "Screen.h"
#include "SkinnedObject3d.h"
#include "SpriteCommon.h"
#include <cmath>
#include <format>
#include <numbers>

namespace {
    const char* kCameraName = "AnimationTest";
    constexpr int kUiLayer = 100;
    const char* kStepSoundName = "test_se";

    // キツネは元の単位がセンチなので小さくする。横向き（右へ進む向き）に置く
    constexpr float kFoxScale = 0.02f;
    constexpr float kFoxYaw = std::numbers::pi_v<float> * 0.5f;
    const Vector3 kLayerFoxPosition = { -3.0f, 0.0f, 0.0f };
    const Vector3 kBlendFoxPosition = { 0.0f, 0.0f, 0.0f };
    const Vector3 kEventFoxPosition = { 3.0f, 0.0f, 0.0f };

    // 左: 背中から先だけ別のアニメーション
    const char* kUpperBodyJoint = "b_Spine01_02";
    constexpr float kLayerSwitchSeconds = 3.0f;
    constexpr float kLayerBlendSeconds = 0.4f;

    // 中: 2次元で混ぜる点（止まる = 下、歩く = 左上、走る = 右上）と、パラメーターが描く円
    const std::vector<AnimationBlendPoint> kBlendPoints = {
        { "Survey", { 0.0f, 0.0f } },
        { "Walk", { -1.0f, 1.0f } },
        { "Run", { 1.0f, 1.0f } },
    };
    const Vector2 kBlendCircleCenter = { 0.0f, 0.6f };
    constexpr float kBlendCircleRadius = 0.8f;
    constexpr float kBlendCircleSpeed = 0.6f;

    // 右: Walk の中の合図（1周のうちの割合。左右の足が着く頃）と、速さのパラメーターの揺れ
    constexpr float kStepLeftRatio = 0.1f;
    constexpr float kStepRightRatio = 0.6f;
    constexpr float kSpeedWaveCenter = 1.0f;
    constexpr float kSpeedWaveAmplitude = 0.6f;
    constexpr float kSpeedWaveFrequency = 0.4f;
    constexpr float kStepMarkerSeconds = 0.3f;
    constexpr float kStepMarkerRadius = 0.12f;
    constexpr float kStepVolume = 0.5f;
}

AnimationTestScene::AnimationTestScene() = default;
// Object3d・SkinnedObject3d の中身はヘッダーで見えないので、デストラクタは .cpp に置く
AnimationTestScene::~AnimationTestScene() = default;

void AnimationTestScene::Initialize() {
    SetupCamera();
    InitializeTexts();

    Object3dCommon* object3dCommon = Framework::GetInstance()->GetObject3dCommon();
    floor = std::make_unique<Object3d>();
    floor->Initialize(object3dCommon);
    floor->SetModel(ModelManager::GetInstance()->GetCubeModel());
    floor->SetScale({ 12.0f, 0.2f, 6.0f });
    floor->SetTranslate({ 0.0f, -0.1f, 0.0f });
    floor->SetColor({ 0.5f, 0.5f, 0.55f, 1.0f });

    AudioManager::GetInstance()->Load(kStepSoundName, "Resources/sounds/test_se.wav");
    SetupLayerFox();
    SetupBlendFox();
    SetupEventFox();
}

void AnimationTestScene::SetupCamera() {
    // 一度空にしてから登録する（シーン読み直しで同じ名前を登録しても assert にならない）
    CameraManager* cameraManager = CameraManager::GetInstance();
    cameraManager->Initialize();
    std::unique_ptr<Camera> newCamera = std::make_unique<Camera>();
    camera = newCamera.get();
    camera->SetTranslate({ 0.0f, 2.2f, -7.0f });
    camera->SetRotate({ 0.2f, 0.0f, 0.0f });
    camera->Update();
    cameraManager->AddCamera(kCameraName, std::move(newCamera));
    cameraManager->SetActiveCamera(kCameraName);
}

void AnimationTestScene::InitializeTexts() {
    SpriteCommon* spriteCommon = Framework::GetInstance()->GetSpriteCommon();

    titleText = std::make_unique<Text>();
    titleText->Initialize(spriteCommon, 24.0f, L"Yu Gothic UI", true);
    titleText->SetText("アニメーション拡張テスト (ANIMATION_TEST)");
    titleText->SetLayer(kUiLayer);

    helpText = std::make_unique<Text>();
    helpText->Initialize(spriteCommon, 18.0f);
    helpText->SetText("左: 背中から先だけ別のアニメーション（3 秒ごとに重ねる / 外す）  中: 3本を2次元で混ぜる  右: 足が着く時刻の合図（球と音）・速さ\n"
                      "[1] 左のレイヤーをすぐ切り替える  [Space] 全部止める / 動かす");
    helpText->SetLayer(kUiLayer);

    statusText = std::make_unique<Text>();
    statusText->Initialize(spriteCommon, 18.0f, L"MS Gothic");
    statusText->SetLayer(kUiLayer);
}

std::unique_ptr<SkinnedObject3d> AnimationTestScene::CreateFox(const Vector3& position) {
    Framework* framework = Framework::GetInstance();
    std::unique_ptr<SkinnedObject3d> fox = std::make_unique<SkinnedObject3d>();
    fox->Initialize(framework->GetObject3dCommon());
    fox->SetGltfData(framework->GetDxCommon(), ModelManager::GetInstance()->LoadGltfModel("Resources/gltf/khronos/Fox", "Fox.glb"));
    fox->SetTranslate(position);
    fox->SetScale({ kFoxScale, kFoxScale, kFoxScale });
    fox->SetRotate({ 0.0f, kFoxYaw, 0.0f });
    return fox;
}

void AnimationTestScene::SetupLayerFox() {
    layerFox = CreateFox(kLayerFoxPosition);
    layerFox->PlayAnimation("Run");
    // 背中から先（首・頭・前足）だけを上書きするレイヤー。後ろ足と尻尾は Run のまま
    upperBodyLayer = layerFox->AddLayer(kUpperBodyJoint);
}

void AnimationTestScene::SetupBlendFox() {
    blendFox = CreateFox(kBlendFoxPosition);
    blendParameter = { kBlendCircleCenter.x, kBlendCircleCenter.y + kBlendCircleRadius };
    blendFox->PlayBlend2D(kBlendPoints, blendParameter);
}

void AnimationTestScene::SetupEventFox() {
    eventFox = CreateFox(kEventFoxPosition);
    // 合図は SetGltfData の後で付ける（時刻は秒。ここでは Walk の長さに対する割合から決める）
    const int32_t walk = eventFox->FindAnimation("Walk");
    const float walkDuration = eventFox->GetAnimationDuration(static_cast<size_t>(walk));
    eventFox->AddAnimationEvent("Walk", walkDuration * kStepLeftRatio, "StepL");
    eventFox->AddAnimationEvent("Walk", walkDuration * kStepRightRatio, "StepR");
    eventFox->SetOnAnimationEvent([this](const std::string& eventName) { OnStep(eventName); });

    // 状態遷移で動かす（Walk の速さを Speed パラメーターで変える）
    eventAnimator.Initialize(eventFox.get());
    eventAnimator.AddState("Walk", { .animation = "Walk", .speedParameter = "Speed" });
    eventAnimator.SetFloat("Speed", kSpeedWaveCenter);
    eventAnimator.Start("Walk");
}

void AnimationTestScene::Finalize() {
    // 登録したカメラを消す（次のシーンに残さない）
    CameraManager::GetInstance()->Initialize();
}

void AnimationTestScene::Update() {
    UpdateInput();
    if (!isPaused) {
        elapsedTime += Time::GetDeltaTime();
    }
    camera->Update();

    UpdateLayerFox();
    UpdateBlendFox();
    UpdateEventFox();
    floor->Update();

    statusText->SetText(BuildStatusText());
    titleText->SetPosition(Screen::GetAnchoredPosition({ 0.0f, 0.0f }, { 16.0f, 12.0f }));
    helpText->SetPosition(Screen::GetAnchoredPosition({ 0.0f, 0.0f }, { 16.0f, 46.0f }));
    statusText->SetPosition(Screen::GetAnchoredPosition({ 0.0f, 0.0f }, { 16.0f, 98.0f }));
    titleText->Update();
    helpText->Update();
    statusText->Update();
}

void AnimationTestScene::UpdateInput() {
    Input* input = Framework::GetInstance()->GetInput();
    if (input->TriggerKey(DIK_1)) {
        layerTimer = kLayerSwitchSeconds; // 次の UpdateLayerFox で切り替わる
    }
    if (input->TriggerKey(DIK_SPACE)) {
        isPaused = !isPaused;
        const float speed = isPaused ? 0.0f : 1.0f;
        layerFox->SetAnimationSpeed(speed);
        blendFox->SetAnimationSpeed(speed);
        eventFox->SetAnimationSpeed(speed);
    }
}

void AnimationTestScene::UpdateLayerFox() {
    if (!isPaused) {
        layerTimer += Time::GetDeltaTime();
    }
    if (layerTimer >= kLayerSwitchSeconds) {
        layerTimer = 0.0f;
        isLayerOn = !isLayerOn;
        if (isLayerOn) {
            layerFox->PlayLayerAnimation(upperBodyLayer, "Survey", true, kLayerBlendSeconds);
        } else {
            layerFox->StopLayerAnimation(upperBodyLayer, kLayerBlendSeconds);
        }
    }
    layerFox->Update();
}

void AnimationTestScene::UpdateBlendFox() {
    // パラメーターを円の上で回す（下 = Survey 寄り、左上 = Walk 寄り、右上 = Run 寄り）
    const float angle = elapsedTime * kBlendCircleSpeed;
    blendParameter = { kBlendCircleCenter.x + std::sin(angle) * kBlendCircleRadius,
                       kBlendCircleCenter.y + std::cos(angle) * kBlendCircleRadius };
    blendFox->SetBlend2DParameter(blendParameter);
    blendFox->Update();
}

void AnimationTestScene::UpdateEventFox() {
    // 速さのパラメーターを揺らす（足の運びが速くなったり遅くなったりしても、合図は足が着く時に来る）
    eventAnimator.SetFloat("Speed", kSpeedWaveCenter + kSpeedWaveAmplitude * std::sin(elapsedTime * kSpeedWaveFrequency * 2.0f * std::numbers::pi_v<float>));
    eventAnimator.Update();
    eventFox->Update();
}

void AnimationTestScene::OnStep(const std::string& eventName) {
    ++stepCount;
    lastEvent = eventName;
    // 合図の名前の足の位置に、少しの間だけ球を出す
    const char* footJoint = eventName == "StepL" ? "b_LeftFoot02_018" : "b_RightFoot02_022";
    Matrix4x4 footMatrix;
    if (eventFox->GetJointWorldMatrix(footJoint, footMatrix)) {
        const Vector3 footPosition = { footMatrix.m[3][0], footMatrix.m[3][1], footMatrix.m[3][2] };
        DebugDraw::Sphere({ footPosition, kStepMarkerRadius }, eventName == "StepL" ? DebugDraw::kCyan : DebugDraw::kYellow,
                          kStepMarkerSeconds, false);
        AudioManager::GetInstance()->Play3D(kStepSoundName, footPosition, false, kStepVolume);
    }
}

void AnimationTestScene::Draw() {
    // --- シャドウパス（ライト視点の深度のみ） ---
    DrawShadowPass();
    Framework::GetInstance()->GetDxCommon()->BindMainRenderTarget();

    // --- 通常の描画パス ---
    Framework::GetInstance()->GetObject3dCommon()->PreDraw();
    floor->Draw();
    layerFox->Draw();
    blendFox->Draw();
    eventFox->Draw();

    titleText->Draw();
    helpText->Draw();
    statusText->Draw();
}

void AnimationTestScene::DrawShadowPass() {
    Object3dCommon* object3dCommon = Framework::GetInstance()->GetObject3dCommon();
    object3dCommon->PreDrawShadow();
    const Matrix4x4 lightViewProjection = object3dCommon->GetLightViewProjection();
    floor->DrawShadow(lightViewProjection);
    layerFox->DrawShadow(lightViewProjection);
    blendFox->DrawShadow(lightViewProjection);
    eventFox->DrawShadow(lightViewProjection);
    object3dCommon->PostDrawShadow();
}

std::string AnimationTestScene::BuildStatusText() const {
    std::string blendWeights;
    for (size_t i = 0; i < blendFox->GetBlendClipCount(); ++i) {
        const int32_t animation = blendFox->GetBlendClipAnimation(i);
        blendWeights += std::format("{} {:.2f}  ", blendFox->GetAnimationName(static_cast<size_t>(animation)), blendFox->GetBlendClipWeight(i));
    }
    const int32_t layerAnimation = layerFox->GetLayerAnimationIndex(upperBodyLayer);
    return std::format("左 レイヤー: {}  効き具合 {:.2f}\n", layerAnimation >= 0 ? layerFox->GetAnimationName(static_cast<size_t>(layerAnimation)) : "なし",
                       layerFox->GetLayerWeight(upperBodyLayer)) +
           std::format("中 2次元: パラメーター ({:+.2f}, {:+.2f})  {}\n", blendParameter.x, blendParameter.y, blendWeights) +
           std::format("右 合図: {} 回  最後 {}  速さ x{:.2f}  時刻 {:.2f} / {:.2f} 秒{}", stepCount, lastEvent.empty() ? "-" : lastEvent,
                       eventFox->GetCurrentAnimationSpeed(), eventFox->GetAnimationTime(),
                       eventFox->GetAnimationDuration(static_cast<size_t>(eventFox->GetCurrentAnimationIndex())), isPaused ? "  （止めています）" : "");
}
