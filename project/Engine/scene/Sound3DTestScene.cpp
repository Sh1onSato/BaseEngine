#include "Sound3DTestScene.h"
#include "Calculation.h"
#include "Camera.h"
#include "CameraManager.h"
#include "DebugDraw.h"
#include "Framework.h"
#include "GameTime.h"
#include "Input.h"
#include "ModelManager.h"
#include "Object3d.h"
#include "Screen.h"
#include "SpriteCommon.h"
#include <cmath>
#include <format>

namespace {
    const char* kCameraName = "Sound3DTest";
    constexpr int kUiLayer = 100;
    const char* kOrbitSoundName = "test_bgm_a";
    const char* kDistanceSoundName = "test_bgm_b";
    const char* kPanSoundName = "test_se";

    const Vector3 kCameraPosition = { 0.0f, 6.0f, -12.0f };
    const Vector3 kOrbitCenter = { 0.0f, 1.0f, 0.0f };
    constexpr float kOrbitRadius = 6.0f;
    constexpr float kOrbitSpeed = 0.8f;       // 1 秒あたりの角度（ラジアン）
    constexpr float kOrbitMinDistance = 2.0f; // ここまでは元の音量
    constexpr float kOrbitMaxDistance = 30.0f;

    // 近づいたり遠ざかったりする音: z が kDistanceNear 〜 kDistanceFar を行き来する
    constexpr float kDistanceNear = -8.0f;
    constexpr float kDistanceFar = 30.0f;
    constexpr float kDistanceSpeed = 0.4f;
    constexpr float kDistanceMaxDistance = 25.0f; // これより遠いと聞こえない

    constexpr float kPanSpeed = 1.2f;
    constexpr float kPanRepeatSeconds = 0.5f; // 左右に振る音（効果音）を鳴らし直す間隔
    constexpr float kCameraTurnSpeed = 1.5f;
    constexpr float kSoundVolume = 0.6f;
}

Sound3DTestScene::Sound3DTestScene() = default;
// Object3d の中身はヘッダーで見えないので、デストラクタは .cpp に置く
Sound3DTestScene::~Sound3DTestScene() = default;

void Sound3DTestScene::Initialize() {
    SetupCamera();
    InitializeTexts();

    Object3dCommon* object3dCommon = Framework::GetInstance()->GetObject3dCommon();
    ModelManager* models = ModelManager::GetInstance();
    floor = std::make_unique<Object3d>();
    floor->Initialize(object3dCommon);
    floor->SetModel(models->GetCubeModel());
    floor->SetScale({ 30.0f, 0.2f, 50.0f });
    floor->SetTranslate({ 0.0f, -0.1f, 10.0f });
    floor->SetColor({ 0.5f, 0.5f, 0.55f, 1.0f });
    orbitBall = std::make_unique<Object3d>();
    orbitBall->Initialize(object3dCommon);
    orbitBall->SetModel(models->GetSphereModel());
    orbitBall->SetColor({ 1.0f, 0.5f, 0.2f, 1.0f });
    distanceBall = std::make_unique<Object3d>();
    distanceBall->Initialize(object3dCommon);
    distanceBall->SetModel(models->GetSphereModel());
    distanceBall->SetColor({ 0.3f, 0.6f, 1.0f, 1.0f });

    StartSounds();
}

void Sound3DTestScene::SetupCamera() {
    // 一度空にしてから登録する（シーン読み直しで同じ名前を登録しても assert にならない）
    CameraManager* cameraManager = CameraManager::GetInstance();
    cameraManager->Initialize();
    std::unique_ptr<Camera> newCamera = std::make_unique<Camera>();
    camera = newCamera.get();
    camera->SetTranslate(kCameraPosition);
    camera->SetRotate({ 0.35f, 0.0f, 0.0f });
    camera->Update();
    cameraManager->AddCamera(kCameraName, std::move(newCamera));
    cameraManager->SetActiveCamera(kCameraName);
}

void Sound3DTestScene::InitializeTexts() {
    SpriteCommon* spriteCommon = Framework::GetInstance()->GetSpriteCommon();

    titleText = std::make_unique<Text>();
    titleText->Initialize(spriteCommon, 24.0f, L"Yu Gothic UI", true);
    titleText->SetText("3D サウンドテスト (SOUND3D_TEST)");
    titleText->SetLayer(kUiLayer);

    helpText = std::make_unique<Text>();
    helpText->Initialize(spriteCommon, 18.0f);
    helpText->SetText("[1] 回る音  [2] 遠ざかる音  [3] 左右に振る音（3D でない）の ON/OFF   [←][→] カメラを回す（耳の向きも回る）");
    helpText->SetLayer(kUiLayer);

    statusText = std::make_unique<Text>();
    statusText->Initialize(spriteCommon, 20.0f, L"MS Gothic");
    statusText->SetLayer(kUiLayer);
}

void Sound3DTestScene::StartSounds() {
    AudioManager* audio = AudioManager::GetInstance();
    audio->Load(kOrbitSoundName, "Resources/sounds/test_bgm_a.wav");
    audio->Load(kDistanceSoundName, "Resources/sounds/test_bgm_b.wav");
    audio->Load(kPanSoundName, "Resources/sounds/test_se.wav");

    orbitSource.label = "回る音";
    distanceSource.label = "遠ざかる音";
    panSource.label = "左右に振る音";
    UpdateSources();
    orbitSource.handle = audio->Play3D(kOrbitSoundName, orbitSource.position, true, kSoundVolume, kOrbitMinDistance, kOrbitMaxDistance);
    distanceSource.handle = audio->Play3D(kDistanceSoundName, distanceSource.position, true, kSoundVolume, 1.0f, kDistanceMaxDistance);
}

void Sound3DTestScene::Finalize() {
    AudioManager* audio = AudioManager::GetInstance();
    audio->Stop(orbitSource.handle);
    audio->Stop(distanceSource.handle);
    audio->Stop(panSource.handle);
    // 登録したカメラを消す（次のシーンに残さない。リスナーは次のシーンのカメラに付いていく）
    CameraManager::GetInstance()->Initialize();
}

void Sound3DTestScene::Update() {
    UpdateInput();
    elapsedTime += Time::GetDeltaTime();

    camera->SetRotate({ 0.35f, cameraYaw, 0.0f });
    camera->Update();
    UpdateSources();

    AudioManager* audio = AudioManager::GetInstance();
    audio->SetSoundPosition(orbitSource.handle, orbitSource.position);
    audio->SetSoundPosition(distanceSource.handle, distanceSource.position);
    // 左右に振る音（効果音を一定間隔で鳴らし直し、その時の左右の位置で振る）
    if (panSource.isEnabled && !audio->IsPlaying(panSource.handle) &&
        std::fmod(elapsedTime, kPanRepeatSeconds) < Time::GetDeltaTime()) {
        panSource.handle = audio->Play(kPanSoundName, false, kSoundVolume);
    }
    audio->SetPan(panSource.handle, panSource.position.x);

    floor->Update();
    orbitBall->SetTranslate(orbitSource.position);
    orbitBall->Update();
    distanceBall->SetTranslate(distanceSource.position);
    distanceBall->Update();

    // 回る音の「元の音量で聞こえる範囲」と、遠ざかる音の「聞こえる範囲」を床に描く
    const Vector3& listener = audio->GetListenerPosition();
    DebugDraw::Circle({ listener.x, 0.05f, listener.z }, { 0.0f, 1.0f, 0.0f }, kOrbitMinDistance, DebugDraw::kYellow);
    DebugDraw::Circle({ listener.x, 0.05f, listener.z }, { 0.0f, 1.0f, 0.0f }, kDistanceMaxDistance, DebugDraw::kCyan);
    // 耳の向き（カメラの真下の床に。カメラの位置から描くと画面いっぱいに写ってしまう）
    const Vector3 listenerOnFloor = { listener.x, 0.05f, listener.z };
    DebugDraw::Arrow(listenerOnFloor, listenerOnFloor + Vector3{ std::sin(cameraYaw), 0.0f, std::cos(cameraYaw) } * 2.0f,
                     DebugDraw::kWhite, 0.0f, false);

    statusText->SetText(BuildStatusText());
    titleText->SetPosition(Screen::GetAnchoredPosition({ 0.0f, 0.0f }, { 16.0f, 12.0f }));
    helpText->SetPosition(Screen::GetAnchoredPosition({ 0.0f, 0.0f }, { 16.0f, 46.0f }));
    statusText->SetPosition(Screen::GetAnchoredPosition({ 0.0f, 0.0f }, { 16.0f, 76.0f }));
    titleText->Update();
    helpText->Update();
    statusText->Update();
}

void Sound3DTestScene::UpdateSources() {
    const float angle = elapsedTime * kOrbitSpeed;
    orbitSource.position = kOrbitCenter + Vector3{ std::sin(angle), 0.0f, std::cos(angle) } * kOrbitRadius;
    // 0〜1〜0 を行き来する
    const float wave = 0.5f - 0.5f * std::cos(elapsedTime * kDistanceSpeed * 2.0f);
    distanceSource.position = { 3.0f, 1.0f, kDistanceNear + (kDistanceFar - kDistanceNear) * wave };
    // 左右に振る音は x を -1〜1 の振り分けとして使う
    panSource.position = { std::sin(elapsedTime * kPanSpeed), 0.0f, 0.0f };
}

void Sound3DTestScene::UpdateInput() {
    Input* input = Framework::GetInstance()->GetInput();
    AudioManager* audio = AudioManager::GetInstance();
    if (input->TriggerKey(DIK_1)) {
        orbitSource.isEnabled = !orbitSource.isEnabled;
        orbitSource.isEnabled ? audio->Resume(orbitSource.handle) : audio->Pause(orbitSource.handle);
    }
    if (input->TriggerKey(DIK_2)) {
        distanceSource.isEnabled = !distanceSource.isEnabled;
        distanceSource.isEnabled ? audio->Resume(distanceSource.handle) : audio->Pause(distanceSource.handle);
    }
    if (input->TriggerKey(DIK_3)) {
        panSource.isEnabled = !panSource.isEnabled;
    }
    if (input->PushKey(DIK_LEFT)) { cameraYaw -= kCameraTurnSpeed * Time::GetDeltaTime(); }
    if (input->PushKey(DIK_RIGHT)) { cameraYaw += kCameraTurnSpeed * Time::GetDeltaTime(); }
}

void Sound3DTestScene::Draw() {
    // --- シャドウパス（ライト視点の深度のみ） ---
    DrawShadowPass();
    Framework::GetInstance()->GetDxCommon()->BindMainRenderTarget();

    // --- 通常の描画パス ---
    Framework::GetInstance()->GetObject3dCommon()->PreDraw();
    floor->Draw();
    orbitBall->Draw();
    distanceBall->Draw();

    titleText->Draw();
    helpText->Draw();
    statusText->Draw();
}

void Sound3DTestScene::DrawShadowPass() {
    Object3dCommon* object3dCommon = Framework::GetInstance()->GetObject3dCommon();
    object3dCommon->PreDrawShadow();
    const Matrix4x4 lightViewProjection = object3dCommon->GetLightViewProjection();
    floor->DrawShadow(lightViewProjection);
    orbitBall->DrawShadow(lightViewProjection);
    distanceBall->DrawShadow(lightViewProjection);
    object3dCommon->PostDrawShadow();
}

std::string Sound3DTestScene::BuildStatusText() const {
    const AudioManager* audio = AudioManager::GetInstance();
    const Vector3& listener = audio->GetListenerPosition();
    auto describe = [&](const Source& source, bool is3D) {
        float left = 0.0f;
        float right = 0.0f;
        if (!source.isEnabled) {
            return std::format("{}: 止めています", source.label);
        }
        if (!audio->GetOutputGains(source.handle, left, right)) {
            // 左右に振る音は短い効果音なので、鳴っていない間は次に鳴らすときの振り分けを出す
            return std::format("{}: 次を待っています（振り分け {:+.2f}）", source.label, source.position.x);
        }
        const float distance = Calculation::Length(source.position - listener);
        return is3D ? std::format("{}: 左 {:.2f}  右 {:.2f}  距離 {:5.1f}", source.label, left, right, distance)
                    : std::format("{}: 左 {:.2f}  右 {:.2f}", source.label, left, right);
    };
    return describe(orbitSource, true) + "\n" + describe(distanceSource, true) + "\n" + describe(panSource, false) +
           std::format("\n耳の位置 ({:.1f}, {:.1f}, {:.1f})  カメラに付いていく {}", listener.x, listener.y, listener.z,
                       audio->IsListenerFollowingCamera() ? "ON" : "OFF");
}
