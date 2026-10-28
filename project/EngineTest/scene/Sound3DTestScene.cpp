#include "Sound3DTestScene.h"
#include "Calculation.h"
#include "Camera.h"
#include "CameraManager.h"
#include "Collision.h"
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
    const char* kOrbitSoundName = "test3d_beep";
    const char* kDistanceSoundName = "test3d_chime";
    const char* kPanSoundName = "test_se";
    const char* kFlyBySoundName = "test3d_engine";

    const Vector3 kCameraPosition = { 0.0f, 5.0f, -10.0f };
    const Vector3 kOrbitCenter = { 0.0f, 1.0f, 0.0f };
    constexpr float kOrbitRadius = 6.0f;
    constexpr float kOrbitSpeed = 0.8f;       // 1 秒あたりの角度（ラジアン）
    constexpr float kOrbitMinDistance = 10.0f; // ここまでは元の音量（手前を回っている間は同じ大きさで、左右だけ動く）
    constexpr float kOrbitMaxDistance = 40.0f;

    // 近づいたり遠ざかったりする音: z が kDistanceNear 〜 kDistanceFar を行き来する
    constexpr float kDistanceNear = -6.0f;
    constexpr float kDistanceFar = 32.0f; // 耳から約 42。30 より遠い間は聞こえない
    constexpr float kDistanceSpeed = 0.3f;
    constexpr float kDistanceMinDistance = 3.0f;
    constexpr float kDistanceMaxDistance = 30.0f; // これより遠いと聞こえない

    // 前を横切る音: x が -kFlyByHalfLength 〜 kFlyByHalfLength を一方向に進み、端まで行ったら反対の端へワープする
    constexpr float kFlyByHalfLength = 80.0f;
    constexpr float kFlyBySpeed = 40.0f; // 1 秒あたり。音速 343 に対して、近づくとき約 1.13 倍・遠ざかるとき約 0.9 倍の音程
    constexpr float kFlyByZ = 4.0f;
    constexpr float kFlyByMinDistance = 4.0f;
    constexpr float kFlyByMaxDistance = 70.0f;
    const float kDopplerScales[] = { 0.0f, 1.0f, 3.0f };

    // 壁（半分の大きさ。中心から各向きへの長さ）。回る音・遠ざかる音が右奥にいるときに間に入る
    const Vector3 kWallCenter = { 4.0f, 2.0f, 2.0f };
    const Vector3 kWallHalfSize = { 3.0f, 2.0f, 0.3f };

    constexpr float kPanSpeed = 1.2f;
    constexpr float kPanRepeatSeconds = 0.5f; // 左右に振る音（効果音）を鳴らし直す間隔
    constexpr float kCameraTurnSpeed = 1.5f;
    constexpr float kSoundVolume = 1.0f;
}

Sound3DTestScene::Sound3DTestScene() = default;
// Object3d の中身はヘッダーで見えないので、デストラクタは .cpp に置く
Sound3DTestScene::~Sound3DTestScene() = default;

void Sound3DTestScene::Initialize() {
    SetupCamera();
    InitializeTexts();

    Object3dCommon* object3dCommon = Framework::GetInstance()->GetObject3dCommon();
    ModelManager* models = ModelManager::GetInstance();
    auto makeObject = [&](const std::shared_ptr<Model>& model, const Vector4& color) {
        std::unique_ptr<Object3d> object = std::make_unique<Object3d>();
        object->Initialize(object3dCommon);
        object->SetModel(model);
        object->SetColor(color);
        return object;
    };
    floor = makeObject(models->GetCubeModel(), { 0.5f, 0.5f, 0.55f, 1.0f });
    floor->SetScale({ 40.0f, 0.2f, 50.0f });
    floor->SetTranslate({ 0.0f, -0.1f, 15.0f });
    wall = makeObject(models->GetCubeModel(), { 0.8f, 0.75f, 0.65f, 1.0f });
    wall->SetScale(kWallHalfSize * 2.0f); // 立方体のモデルは 1 辺 1
    wall->SetTranslate(kWallCenter);
    orbitBall = makeObject(models->GetSphereModel(), { 1.0f, 0.5f, 0.2f, 1.0f });
    distanceBall = makeObject(models->GetSphereModel(), { 0.3f, 0.6f, 1.0f, 1.0f });
    flyByBall = makeObject(models->GetSphereModel(), { 0.4f, 1.0f, 0.4f, 1.0f });

    AudioManager::GetInstance()->SetDopplerScale(kDopplerScales[dopplerIndex]);
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
    titleText->SetText("3D サウンドテスト (SOUND3D_TEST)　音は1つずつ ON にして聞いてください");
    titleText->SetLayer(kUiLayer);

    helpText = std::make_unique<Text>();
    helpText->Initialize(spriteCommon, 18.0f);
    helpText->SetText("[1] 回る音  [2] 遠ざかる音  [3] 左右に振る音（3D でない）  [4] 横切る音（ドップラー）の ON/OFF   [←][→] カメラを回す\n"
                      "[5] 壁の ON/OFF（壁の向こうの音がこもる）  [R] 反響の種類  [D] ドップラーの強さ  [M] 効果音をまとめてこもらせる");
    helpText->SetLayer(kUiLayer);

    statusText = std::make_unique<Text>();
    statusText->Initialize(spriteCommon, 20.0f, L"MS Gothic");
    statusText->SetLayer(kUiLayer);
}

void Sound3DTestScene::StartSounds() {
    AudioManager* audio = AudioManager::GetInstance();
    audio->Load(kOrbitSoundName, "Resources/sounds/test3d_beep.wav");
    audio->Load(kDistanceSoundName, "Resources/sounds/test3d_chime.wav");
    audio->Load(kPanSoundName, "Resources/sounds/test_se.wav");
    audio->Load(kFlyBySoundName, "Resources/sounds/test3d_engine.wav");

    orbitSource.label = "[1] 回る音    ";
    distanceSource.label = "[2] 遠ざかる音";
    panSource.label = "[3] 左右に振る音";
    panSource.is3D = false;
    flyBySource.label = "[4] 横切る音  ";
    UpdateSources();
    orbitSource.handle = audio->Play3D(kOrbitSoundName, orbitSource.position, true, kSoundVolume, kOrbitMinDistance, kOrbitMaxDistance);
    distanceSource.handle = audio->Play3D(kDistanceSoundName, distanceSource.position, true, kSoundVolume, kDistanceMinDistance,
                                          kDistanceMaxDistance);
    flyBySource.handle = audio->Play3D(kFlyBySoundName, flyBySource.position, true, kSoundVolume, kFlyByMinDistance, kFlyByMaxDistance);
    // 聞き分けやすいように、最初は回る音だけ鳴らす
    orbitSource.isEnabled = true;
    audio->Pause(distanceSource.handle);
    audio->Pause(flyBySource.handle);
}

void Sound3DTestScene::Finalize() {
    AudioManager* audio = AudioManager::GetInstance();
    audio->Stop(orbitSource.handle);
    audio->Stop(distanceSource.handle);
    audio->Stop(panSource.handle);
    audio->Stop(flyBySource.handle);
    // シーンで変えた設定を戻す（反響は SceneManager が戻す）
    audio->SetDopplerScale(1.0f);
    audio->SetCategoryMuffle(AudioCategory::kSE, 0.0f);
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
    for (Source* source : { &orbitSource, &distanceSource, &flyBySource }) {
        audio->SetSoundPosition(source->handle, source->position);
        UpdateOcclusion(*source);
    }
    // 左右に振る音（効果音を一定間隔で鳴らし直し、その時の左右の位置で振る）
    if (panSource.isEnabled && !audio->IsPlaying(panSource.handle) &&
        std::fmod(elapsedTime, kPanRepeatSeconds) < Time::GetDeltaTime()) {
        panSource.handle = audio->Play(kPanSoundName, false, kSoundVolume);
    }
    audio->SetPan(panSource.handle, panSource.position.x);

    floor->Update();
    wall->Update();
    orbitBall->SetTranslate(orbitSource.position);
    orbitBall->Update();
    distanceBall->SetTranslate(distanceSource.position);
    distanceBall->Update();
    flyByBall->SetTranslate(flyBySource.position);
    flyByBall->Update();

    // 回る音の「元の音量で聞こえる範囲」と、遠ざかる音の「聞こえる範囲」を床に描く
    const Vector3& listener = audio->GetListenerPosition();
    DebugDraw::Circle({ listener.x, 0.05f, listener.z }, { 0.0f, 1.0f, 0.0f }, kOrbitMinDistance, DebugDraw::kYellow);
    DebugDraw::Circle({ listener.x, 0.05f, listener.z }, { 0.0f, 1.0f, 0.0f }, kDistanceMaxDistance, DebugDraw::kCyan);
    // 耳の向き（カメラの真下の床に。カメラの位置から描くと画面いっぱいに写ってしまう）
    const Vector3 listenerOnFloor = { listener.x, 0.05f, listener.z };
    DebugDraw::Arrow(listenerOnFloor, listenerOnFloor + Vector3{ std::sin(cameraYaw), 0.0f, std::cos(cameraYaw) } * 2.0f,
                     DebugDraw::kWhite, 0.0f, false);
    // 壁に遮られている音は、耳の真下からの線を赤で描く（壁に隠れないように）
    for (const Source* source : { &orbitSource, &distanceSource, &flyBySource }) {
        if (source->isEnabled && source->occlusion > 0.0f) {
            DebugDraw::Line(listenerOnFloor, source->position, DebugDraw::kRed, 0.0f, false);
        }
    }

    statusText->SetText(BuildStatusText());
    titleText->SetPosition(Screen::GetAnchoredPosition({ 0.0f, 0.0f }, { 16.0f, 12.0f }));
    helpText->SetPosition(Screen::GetAnchoredPosition({ 0.0f, 0.0f }, { 16.0f, 46.0f }));
    statusText->SetPosition(Screen::GetAnchoredPosition({ 0.0f, 0.0f }, { 16.0f, 98.0f }));
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
    // 左から右へ進み、右端まで行ったら左端へワープ（ワープの動きはドップラーに使われない）
    const float flyByLength = kFlyByHalfLength * 2.0f;
    flyBySource.position = { std::fmod(elapsedTime * kFlyBySpeed, flyByLength) - kFlyByHalfLength, 1.0f, kFlyByZ };
}

void Sound3DTestScene::UpdateOcclusion(Source& source) {
    // 耳から音へレイを飛ばし、音より手前で壁に当たったら遮られている（ゲームでは CollisionManager::Raycast を使う）
    float occlusion = 0.0f;
    if (isWallEnabled) {
        const Vector3& listener = AudioManager::GetInstance()->GetListenerPosition();
        const Vector3 toSource = source.position - listener;
        const float distance = Calculation::Length(toSource);
        const AABB wallBox = { kWallCenter - kWallHalfSize, kWallCenter + kWallHalfSize };
        if (distance > 0.0f && Collision::RayAABB({ listener, toSource * (1.0f / distance) }, wallBox, nullptr, distance)) {
            occlusion = 1.0f;
        }
    }
    source.occlusion = occlusion;
    AudioManager::GetInstance()->SetOcclusion(source.handle, occlusion);
}

void Sound3DTestScene::ToggleSource(Source& source) {
    AudioManager* audio = AudioManager::GetInstance();
    source.isEnabled = !source.isEnabled;
    source.isEnabled ? audio->Resume(source.handle) : audio->Pause(source.handle);
}

void Sound3DTestScene::UpdateInput() {
    Input* input = Framework::GetInstance()->GetInput();
    AudioManager* audio = AudioManager::GetInstance();
    if (input->TriggerKey(DIK_1)) { ToggleSource(orbitSource); }
    if (input->TriggerKey(DIK_2)) { ToggleSource(distanceSource); }
    if (input->TriggerKey(DIK_3)) { panSource.isEnabled = !panSource.isEnabled; }
    if (input->TriggerKey(DIK_4)) { ToggleSource(flyBySource); }
    if (input->TriggerKey(DIK_5)) { isWallEnabled = !isWallEnabled; }
    if (input->TriggerKey(DIK_R)) {
        const int next = (static_cast<int>(audio->GetReverb()) + 1) % static_cast<int>(ReverbPreset::kCount);
        audio->SetReverb(static_cast<ReverbPreset>(next));
    }
    if (input->TriggerKey(DIK_D)) {
        dopplerIndex = (dopplerIndex + 1) % static_cast<int>(std::size(kDopplerScales));
        audio->SetDopplerScale(kDopplerScales[dopplerIndex]);
    }
    if (input->TriggerKey(DIK_M)) {
        isMuffled = !isMuffled;
        audio->SetCategoryMuffle(AudioCategory::kSE, isMuffled ? 1.0f : 0.0f);
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
    if (isWallEnabled) {
        wall->Draw();
    }
    orbitBall->Draw();
    distanceBall->Draw();
    flyByBall->Draw();

    titleText->Draw();
    helpText->Draw();
    statusText->Draw();
}

void Sound3DTestScene::DrawShadowPass() {
    Object3dCommon* object3dCommon = Framework::GetInstance()->GetObject3dCommon();
    object3dCommon->PreDrawShadow();
    const Matrix4x4 lightViewProjection = object3dCommon->GetLightViewProjection();
    floor->DrawShadow(lightViewProjection);
    if (isWallEnabled) {
        wall->DrawShadow(lightViewProjection);
    }
    orbitBall->DrawShadow(lightViewProjection);
    distanceBall->DrawShadow(lightViewProjection);
    flyByBall->DrawShadow(lightViewProjection);
    object3dCommon->PostDrawShadow();
}

std::string Sound3DTestScene::BuildStatusText() const {
    const AudioManager* audio = AudioManager::GetInstance();
    const Vector3& listener = audio->GetListenerPosition();
    auto describe = [&](const Source& source) {
        float left = 0.0f;
        float right = 0.0f;
        if (!source.isEnabled) {
            return std::format("{}: OFF", source.label);
        }
        if (!audio->GetOutputGains(source.handle, left, right)) {
            // 左右に振る音は短い効果音なので、鳴っていない間は次に鳴らすときの振り分けを出す
            return std::format("{}: 次を待っています（振り分け {:+.2f}）", source.label, source.position.x);
        }
        if (!source.is3D) {
            return std::format("{}: 左 {:.2f}  右 {:.2f}", source.label, left, right);
        }
        const float distance = Calculation::Length(source.position - listener);
        return std::format("{}: 左 {:.2f}  右 {:.2f}  距離 {:5.1f}  音程 x{:.2f}{}", source.label, left, right, distance,
                           audio->GetFrequencyRatio(source.handle), source.occlusion > 0.0f ? "  壁の向こう" : "");
    };
    return describe(orbitSource) + "\n" + describe(distanceSource) + "\n" + describe(panSource) + "\n" + describe(flyBySource) +
           std::format("\n壁 {}  反響 {}  ドップラー x{:.0f}  効果音のこもり {}", isWallEnabled ? "ON" : "OFF",
                       AudioManager::GetReverbName(audio->GetReverb()), audio->GetDopplerScale(), isMuffled ? "ON" : "OFF") +
           std::format("\n耳の位置 ({:.1f}, {:.1f}, {:.1f})  カメラに付いていく {}", listener.x, listener.y, listener.z,
                       audio->IsListenerFollowingCamera() ? "ON" : "OFF");
}
