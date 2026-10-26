#include "SoundPropagationTestScene.h"
#include "Calculation.h"
#include "Camera.h"
#include "CameraManager.h"
#include "DebugDraw.h"
#include "Framework.h"
#include "GameTime.h"
#include "Input.h"
#include "ModelManager.h"
#include "ModelObject.h"
#include "Object3d.h"
#include "Screen.h"
#include "SpriteCommon.h"
#include <array>
#include <cmath>
#include <format>

namespace {
    const char* kCameraName = "SoundPropagationTest";
    constexpr int kUiLayer = 100;
    const char* kRadioSoundName = "test3d_chime";
    const char* kPatrolSoundName = "test3d_beep";

    // 床（上面が y = 0）の広さと格子
    constexpr float kFloorWidth = 24.0f;
    constexpr float kFloorDepth = 18.0f;
    constexpr float kCellSize = 0.5f;
    // 音の格子は扉を塞がないように、壁を少しだけ太らせる
    constexpr float kSoundGridRadius = 0.1f;
    constexpr float kSoundHeight = 1.0f;

    const Vector4 kFloorColor = { 0.55f, 0.55f, 0.6f, 1.0f };
    const Vector4 kWallColor = { 0.4f, 0.4f, 0.45f, 1.0f };
    const Vector4 kDoorColor = { 0.65f, 0.45f, 0.25f, 1.0f };

    // 耳（左の部屋を回る）
    const std::array<Vector3, 4> kListenerRoute = {
        Vector3{ -9.0f, kSoundHeight, -6.0f }, Vector3{ -2.0f, kSoundHeight, -6.0f },
        Vector3{ -2.0f, kSoundHeight, 6.5f }, Vector3{ -9.0f, kSoundHeight, 6.5f },
    };
    constexpr float kListenerSpeed = 2.5f;
    constexpr float kListenerManualSpeed = 5.0f;

    // 奥の部屋（右下）の音。左の部屋からは 扉 → 右上の部屋 → 通路 と回り込んで届く
    const Vector3 kRadioPosition = { 4.0f, kSoundHeight, -5.0f };
    // 隣の部屋（右上）を行き来する音
    const Vector3 kPatrolFrom = { 3.0f, kSoundHeight, 3.0f };
    const Vector3 kPatrolTo = { 10.0f, kSoundHeight, 7.5f };
    constexpr float kPatrolSpeed = 0.5f; // 行き来の速さ（ラジアン / 秒）

    constexpr float kSoundVolume = 1.0f;
    constexpr float kMinDistance = 2.0f;
    constexpr float kMaxDistance = 35.0f;

    /// <summary>
    /// 動かない壁（Static の箱のコライダー）。音の格子は BlockColliders でこれを読む
    /// </summary>
    class WallBlock : public ModelObject {
    public:
        WallBlock(const Vector3& position, const Vector3& size, const Vector4& color) : size(size) {
            transform.translate = position;
            transform.scale = size;
            SetColor(color);
        }
        void Initialize() override {
            SetModel(ModelManager::GetInstance()->GetCubeModel());
            AddCollider(Collider::MakeBox(size * 0.5f))->SetType(ColliderType::kStatic);
        }

    private:
        Vector3 size;
    };

    const char* GetKindName(SoundPathResult::Kind kind) {
        switch (kind) {
        case SoundPathResult::Kind::kDirect: return "見通せる";
        case SoundPathResult::Kind::kAround: return "回り込み";
        case SoundPathResult::Kind::kThroughWall: return "壁越し";
        default: return "?";
        }
    }
}

SoundPropagationTestScene::SoundPropagationTestScene() = default;
// Object3d の中身はヘッダーで見えないので、デストラクタは .cpp に置く
SoundPropagationTestScene::~SoundPropagationTestScene() = default;

void SoundPropagationTestScene::Initialize() {
    SetupCamera();
    InitializeTexts();
    SpawnWalls();
    BuildGrid();
    propagation.SetGrid(&grid);

    Object3dCommon* object3dCommon = Framework::GetInstance()->GetObject3dCommon();
    auto makeBall = [&](const Vector4& color) {
        std::unique_ptr<Object3d> ball = std::make_unique<Object3d>();
        ball->Initialize(object3dCommon);
        ball->SetModel(ModelManager::GetInstance()->GetSphereModel());
        ball->SetColor(color);
        return ball;
    };
    listenerBall = makeBall({ 0.4f, 1.0f, 0.4f, 1.0f });
    radio.ball = makeBall({ 0.3f, 0.6f, 1.0f, 1.0f });
    patrol.ball = makeBall({ 1.0f, 0.5f, 0.2f, 1.0f });
    listenerPosition = kListenerRoute[0];

    StartSounds();
}

void SoundPropagationTestScene::SetupCamera() {
    // 一度空にしてから登録する（シーン読み直しで同じ名前を登録しても assert にならない）
    CameraManager* cameraManager = CameraManager::GetInstance();
    cameraManager->Initialize();
    std::unique_ptr<Camera> newCamera = std::make_unique<Camera>();
    camera = newCamera.get();
    camera->SetTranslate({ 0.0f, 22.0f, -15.0f });
    camera->SetRotate({ 0.95f, 0.0f, 0.0f }); // ほぼ真上から見下ろす
    camera->Update();
    cameraManager->AddCamera(kCameraName, std::move(newCamera));
    cameraManager->SetActiveCamera(kCameraName);
}

void SoundPropagationTestScene::InitializeTexts() {
    SpriteCommon* spriteCommon = Framework::GetInstance()->GetSpriteCommon();

    titleText = std::make_unique<Text>();
    titleText->Initialize(spriteCommon, 24.0f, L"Yu Gothic UI", true);
    titleText->SetText("音の回り込みテスト (SOUND_PROPAGATION_TEST)　耳 = 緑の球（画面の右が右耳）");
    titleText->SetLayer(kUiLayer);

    helpText = std::make_unique<Text>();
    helpText->Initialize(spriteCommon, 18.0f);
    helpText->SetText("[1] 奥の部屋の音  [2] 隣の部屋を歩く音 の ON/OFF  [O] 扉の開け閉め  [P] 回り込み ON/OFF（OFF は真っすぐのレイだけ）\n"
                      "[Space] 耳を手で動かす（[←↑↓→]）/ 自動  [G] 格子   道: 緑 = 見通せる  黄 = 回り込み  赤 = 壁越し  白い × = 聞こえる位置");
    helpText->SetLayer(kUiLayer);

    statusText = std::make_unique<Text>();
    statusText->Initialize(spriteCommon, 18.0f, L"MS Gothic");
    statusText->SetLayer(kUiLayer);
}

void SoundPropagationTestScene::SpawnWalls() {
    objects.Clear();
    objects.Create<WallBlock>(Vector3{ 0.0f, -0.5f, 0.0f }, Vector3{ kFloorWidth, 1.0f, kFloorDepth }, kFloorColor)->SetName("Floor");
    // 外周の壁
    objects.Create<WallBlock>(Vector3{ 0.0f, 0.75f, 9.25f }, Vector3{ 24.0f, 1.5f, 0.5f }, kWallColor);
    objects.Create<WallBlock>(Vector3{ 0.0f, 0.75f, -9.25f }, Vector3{ 24.0f, 1.5f, 0.5f }, kWallColor);
    objects.Create<WallBlock>(Vector3{ 12.25f, 0.75f, 0.0f }, Vector3{ 0.5f, 1.5f, 18.0f }, kWallColor);
    objects.Create<WallBlock>(Vector3{ -12.25f, 0.75f, 0.0f }, Vector3{ 0.5f, 1.5f, 18.0f }, kWallColor);
    // 真ん中の壁（z = 4〜7 が扉）
    objects.Create<WallBlock>(Vector3{ 0.0f, 0.75f, -2.5f }, Vector3{ 0.6f, 1.5f, 13.0f }, kWallColor);
    objects.Create<WallBlock>(Vector3{ 0.0f, 0.75f, 8.0f }, Vector3{ 0.6f, 1.5f, 2.0f }, kWallColor);
    door = objects.Create<WallBlock>(Vector3{ 0.0f, 0.75f, 5.5f }, Vector3{ 0.4f, 1.5f, 3.0f }, kDoorColor);
    door->SetName("Door");
    door->SetActive(!isDoorOpen);
    // 右の部屋を上下に分ける壁（x = 8〜10 が通路）
    objects.Create<WallBlock>(Vector3{ 4.0f, 0.75f, 0.0f }, Vector3{ 8.0f, 1.5f, 0.6f }, kWallColor);
    objects.Create<WallBlock>(Vector3{ 11.0f, 0.75f, 0.0f }, Vector3{ 2.0f, 1.5f, 0.6f }, kWallColor);
}

void SoundPropagationTestScene::BuildGrid() {
    grid.Initialize({ -kFloorWidth * 0.5f, 0.0f, -kFloorDepth * 0.5f },
                    static_cast<int>(kFloorWidth / kCellSize), static_cast<int>(kFloorDepth / kCellSize), kCellSize, NavPlane::kXZ);
    grid.BlockColliders(objects, kSoundGridRadius);
    // 格子が変わったので、音の道も作り直す
    propagation.Invalidate();
}

void SoundPropagationTestScene::StartSounds() {
    AudioManager* audio = AudioManager::GetInstance();
    audio->Load(kRadioSoundName, "Resources/sounds/test3d_chime.wav");
    audio->Load(kPatrolSoundName, "Resources/sounds/test3d_beep.wav");
    // 耳はカメラ（上空）ではなく緑の球にする
    UpdateListener();

    radio.label = "[1] 奥の部屋の音  ";
    patrol.label = "[2] 隣の部屋の足音";
    UpdateSources();
    radio.handle = audio->Play3D(kRadioSoundName, radio.position, true, kSoundVolume, kMinDistance, kMaxDistance);
    patrol.handle = audio->Play3D(kPatrolSoundName, patrol.position, true, kSoundVolume, kMinDistance, kMaxDistance);
}

void SoundPropagationTestScene::Finalize() {
    AudioManager* audio = AudioManager::GetInstance();
    audio->Stop(radio.handle);
    audio->Stop(patrol.handle);
    audio->SetListenerFollowCamera(true);
    objects.Clear();
    // 登録したカメラを消す（次のシーンに残さない）
    CameraManager::GetInstance()->Initialize();
}

void SoundPropagationTestScene::Update() {
    UpdateInput();
    elapsedTime += Time::GetDeltaTime();
    objects.Update();
    camera->Update();

    UpdateListener();
    UpdateSources();
    // 耳が別のマスに動いた・扉を開け閉めしたときだけ、道を作り直す
    propagation.Update();

    AudioManager* audio = AudioManager::GetInstance();
    for (Source* source : { &radio, &patrol }) {
        if (isPropagationEnabled) {
            source->path = propagation.Apply(source->handle, source->position);
        } else {
            source->path = EvaluateStraight(source->position);
            audio->SetSoundPosition(source->handle, source->position);
            audio->SetOcclusion(source->handle, source->path.occlusion);
        }
        if (source->isEnabled) {
            if (isPropagationEnabled) {
                propagation.DrawDebug(source->position);
            } else {
                DebugDraw::Line(listenerPosition, source->position,
                                source->path.occlusion > 0.0f ? DebugDraw::kRed : DebugDraw::kGreen, 0.0f, false);
            }
        }
        source->ball->SetTranslate(source->position);
        source->ball->Update();
    }
    listenerBall->SetTranslate(listenerPosition);
    listenerBall->Update();
    if (isGridVisible) {
        grid.DrawDebug();
    }

    statusText->SetText(BuildStatusText());
    titleText->SetPosition(Screen::GetAnchoredPosition({ 0.0f, 0.0f }, { 16.0f, 12.0f }));
    helpText->SetPosition(Screen::GetAnchoredPosition({ 0.0f, 0.0f }, { 16.0f, 46.0f }));
    statusText->SetPosition(Screen::GetAnchoredPosition({ 0.0f, 0.0f }, { 16.0f, 98.0f }));
    titleText->Update();
    helpText->Update();
    statusText->Update();
}

void SoundPropagationTestScene::UpdateListener() {
    if (isListenerManual) {
        Input* input = Framework::GetInstance()->GetInput();
        Vector3 move = { 0.0f, 0.0f, 0.0f };
        if (input->PushKey(DIK_LEFT)) { move.x -= 1.0f; }
        if (input->PushKey(DIK_RIGHT)) { move.x += 1.0f; }
        if (input->PushKey(DIK_UP)) { move.z += 1.0f; }
        if (input->PushKey(DIK_DOWN)) { move.z -= 1.0f; }
        listenerPosition = listenerPosition + move * (kListenerManualSpeed * Time::GetDeltaTime());
    } else {
        // 次の点へ一定の速さで進み、着いたらその次へ
        const Vector3& next = kListenerRoute[listenerRouteIndex];
        const Vector3 toNext = next - listenerPosition;
        const float distance = Calculation::Length(toNext);
        const float step = kListenerSpeed * Time::GetDeltaTime();
        if (distance <= step) {
            listenerPosition = next;
            listenerRouteIndex = (listenerRouteIndex + 1) % kListenerRoute.size();
        } else {
            listenerPosition = listenerPosition + toNext * (step / distance);
        }
    }
    // 上から見下ろしているので、画面の上（+z）を前、上（+y）を頭の上にする（画面の右 = 右耳）
    AudioManager::GetInstance()->SetListener(listenerPosition, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f });
}

void SoundPropagationTestScene::UpdateSources() {
    radio.position = kRadioPosition;
    const float wave = 0.5f - 0.5f * std::cos(elapsedTime * kPatrolSpeed);
    patrol.position = kPatrolFrom + (kPatrolTo - kPatrolFrom) * wave;
}

SoundPathResult SoundPropagationTestScene::EvaluateStraight(const Vector3& sourcePosition) const {
    SoundPathResult result;
    result.apparentPosition = sourcePosition;
    result.directDistance = Calculation::Length(sourcePosition - listenerPosition);
    result.pathDistance = result.directDistance;
    int fromX = 0;
    int fromY = 0;
    int toX = 0;
    int toY = 0;
    grid.WorldToCell(listenerPosition, fromX, fromY);
    grid.WorldToCell(sourcePosition, toX, toY);
    if (!grid.HasLineOfSight(fromX, fromY, toX, toY)) {
        result.kind = SoundPathResult::Kind::kThroughWall;
        result.occlusion = 1.0f;
    }
    return result;
}

void SoundPropagationTestScene::ToggleSource(Source& source) {
    AudioManager* audio = AudioManager::GetInstance();
    source.isEnabled = !source.isEnabled;
    source.isEnabled ? audio->Resume(source.handle) : audio->Pause(source.handle);
}

void SoundPropagationTestScene::UpdateInput() {
    Input* input = Framework::GetInstance()->GetInput();
    if (input->TriggerKey(DIK_1)) { ToggleSource(radio); }
    if (input->TriggerKey(DIK_2)) { ToggleSource(patrol); }
    if (input->TriggerKey(DIK_O)) {
        isDoorOpen = !isDoorOpen;
        door->SetActive(!isDoorOpen);
        BuildGrid();
    }
    if (input->TriggerKey(DIK_P)) { isPropagationEnabled = !isPropagationEnabled; }
    if (input->TriggerKey(DIK_G)) { isGridVisible = !isGridVisible; }
    if (input->TriggerKey(DIK_SPACE)) { isListenerManual = !isListenerManual; }
}

void SoundPropagationTestScene::Draw() {
    // --- シャドウパス（ライト視点の深度のみ） ---
    DrawShadowPass();
    Framework::GetInstance()->GetDxCommon()->BindMainRenderTarget();

    // --- 通常の描画パス ---
    Framework::GetInstance()->GetObject3dCommon()->PreDraw();
    objects.Draw();
    listenerBall->Draw();
    radio.ball->Draw();
    patrol.ball->Draw();

    titleText->Draw();
    helpText->Draw();
    statusText->Draw();
}

void SoundPropagationTestScene::DrawShadowPass() {
    Object3dCommon* object3dCommon = Framework::GetInstance()->GetObject3dCommon();
    object3dCommon->PreDrawShadow();
    const Matrix4x4 lightViewProjection = object3dCommon->GetLightViewProjection();
    objects.DrawShadow(lightViewProjection);
    listenerBall->DrawShadow(lightViewProjection);
    radio.ball->DrawShadow(lightViewProjection);
    patrol.ball->DrawShadow(lightViewProjection);
    object3dCommon->PostDrawShadow();
}

std::string SoundPropagationTestScene::BuildStatusText() const {
    const AudioManager* audio = AudioManager::GetInstance();
    auto describe = [&](const Source& source) {
        if (!source.isEnabled) {
            return std::format("{}: OFF", source.label);
        }
        float left = 0.0f;
        float right = 0.0f;
        audio->GetOutputGains(source.handle, left, right);
        return std::format("{}: {}  道 {:5.1f}（真っすぐ {:5.1f}）こもり {:.2f}  左 {:.2f}  右 {:.2f}", source.label,
                           GetKindName(source.path.kind), source.path.pathDistance, source.path.directDistance,
                           source.path.occlusion, left, right);
    };
    return describe(radio) + "\n" + describe(patrol) +
           std::format("\n扉 {}  回り込み {}  耳 {}  道を作り直す時間 {:.2f} ms", isDoorOpen ? "開" : "閉",
                       isPropagationEnabled ? "ON" : "OFF（真っすぐのレイだけ）", isListenerManual ? "手動" : "自動",
                       propagation.GetLastBuildMilliseconds());
}
