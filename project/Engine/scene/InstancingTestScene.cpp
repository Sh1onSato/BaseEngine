#include "InstancingTestScene.h"
#include "Camera.h"
#include "CameraManager.h"
#include "Framework.h"
#include "GameTime.h"
#include "Input.h"
#include "ModelManager.h"
#include "Object3d.h"
#include "SpriteCommon.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <format>

namespace {
    const char* kCameraName = "InstancingTest";
    constexpr int kUiLayer = 100;

    // [↑][↓] で切り替える個数
    constexpr std::array<size_t, 7> kCounts = { 100, 500, 1000, 2000, 5000, 10000, 20000 };
    // 1体ずつ描くモードの上限（Object3d は1体ごとに 64KB 単位のバッファを3つ作るため、これ以上はメモリが足りなくなる）
    constexpr size_t kMaxIndividualCount = 2000;

    constexpr float kSpacing = 1.5f;     // 立方体の間隔
    constexpr float kCubeSize = 0.8f;
    constexpr float kSpinSpeed = 1.5f;   // 1秒あたりの回転（ラジアン）
    constexpr float kStatsInterval = 0.5f;
    const Vector4 kFloorColor = { 0.5f, 0.5f, 0.55f, 1.0f };

    // 一辺に並べる数（count 個が入る正方形）
    size_t GetSideCount(size_t count) {
        return static_cast<size_t>(std::ceil(std::sqrt(static_cast<double>(count))));
    }
}

InstancingTestScene::InstancingTestScene() = default;
// Object3d の中身はヘッダーで見えないので、デストラクタは .cpp に置く
InstancingTestScene::~InstancingTestScene() = default;

void InstancingTestScene::Initialize() {
    SetupCamera();
    InitializeTexts();

    cubeModel = ModelManager::GetInstance()->GetCubeModel();
    instancer.Initialize(cubeModel, kCounts.back());

    floor = std::make_unique<Object3d>();
    floor->Initialize(Framework::GetInstance()->GetObject3dCommon());
    floor->SetModel(cubeModel);
    floor->SetColor(kFloorColor);

    ApplyCountLayout();
}

void InstancingTestScene::SetupCamera() {
    // 一度空にしてから登録する（シーン読み直しで同じ名前を登録しても assert にならない）
    CameraManager* cameraManager = CameraManager::GetInstance();
    cameraManager->Initialize();
    std::unique_ptr<Camera> newCamera = std::make_unique<Camera>();
    camera = newCamera.get();
    camera->SetRotate({ 0.8f, 0.0f, 0.0f }); // 斜めに見下ろす
    cameraManager->AddCamera(kCameraName, std::move(newCamera));
    cameraManager->SetActiveCamera(kCameraName);
}

void InstancingTestScene::InitializeTexts() {
    SpriteCommon* spriteCommon = Framework::GetInstance()->GetSpriteCommon();

    titleText = std::make_unique<Text>();
    titleText->Initialize(spriteCommon, 32.0f, L"Yu Gothic UI", true);
    titleText->SetText("インスタンシング テスト (INSTANCING_TEST)");
    titleText->SetPosition({ 20.0f, 16.0f });
    titleText->SetLayer(kUiLayer);

    helpText = std::make_unique<Text>();
    helpText->Initialize(spriteCommon, 20.0f);
    helpText->SetText(
        "[Space] まとめて描く / 1体ずつ描く  [↑][↓] 個数  [P] 回転を止める\n"
        "切り替えても見た目（位置・色・回転・影）が変わらなければOK\n"
        "※影は中央付近（半径55程度）だけに出る（影の範囲の既定値）");
    helpText->SetPosition({ 20.0f, 64.0f });
    helpText->SetLayer(kUiLayer);

    statusText = std::make_unique<Text>();
    statusText->Initialize(spriteCommon, 22.0f, L"MS Gothic");
    statusText->SetPosition({ 20.0f, 150.0f });
    statusText->SetLayer(kUiLayer);
}

void InstancingTestScene::Finalize() {
    // 登録したカメラを消す（次のシーンに残さない）
    CameraManager::GetInstance()->Initialize();
}

void InstancingTestScene::Update() {
    UpdateInput();
    if (!isPaused) {
        elapsedTime += Time::GetDeltaTime();
    }

    const size_t drawCount = GetDrawCount();
    if (useInstancing) {
        instancer.Clear();
        for (size_t i = 0; i < drawCount; ++i) {
            instancer.Add(MakeCubeTransform(i), MakeCubeColor(i));
        }
    } else {
        EnsureIndividualObjects(drawCount);
        for (size_t i = 0; i < drawCount; ++i) {
            const Transform transform = MakeCubeTransform(i);
            Object3d* object = individualObjects[i].get();
            object->SetScale(transform.scale);
            object->SetRotate(transform.rotate);
            object->SetTranslate(transform.translate);
            object->SetColor(MakeCubeColor(i));
            object->Update();
        }
    }

    camera->Update();
    floor->Update();
    UpdateStats();

    statusText->SetText(BuildStatusText());
    titleText->Update();
    helpText->Update();
    statusText->Update();
}

void InstancingTestScene::UpdateInput() {
    Input* input = Framework::GetInstance()->GetInput();
    if (input->TriggerKey(DIK_SPACE)) {
        useInstancing = !useInstancing;
    }
    if (input->TriggerKey(DIK_P)) {
        isPaused = !isPaused;
    }
    if (input->TriggerKey(DIK_UP) && countIndex + 1 < kCounts.size()) {
        ++countIndex;
        ApplyCountLayout();
    }
    if (input->TriggerKey(DIK_DOWN) && countIndex > 0) {
        --countIndex;
        ApplyCountLayout();
    }
}

void InstancingTestScene::ApplyCountLayout() {
    const float width = static_cast<float>(GetSideCount(kCounts[countIndex])) * kSpacing;
    // 格子全体が画面に入る高さ・距離（回転 0.8 ラジアンで見下ろす前提の目安）
    camera->SetTranslate({ 0.0f, width * 0.85f + 4.0f, -width * 0.75f - 4.0f });
    camera->Update();
    // 床（上面が y = 0）
    floor->SetScale({ width + 4.0f, 0.2f, width + 4.0f });
    floor->SetTranslate({ 0.0f, -0.1f, 0.0f });
}

Transform InstancingTestScene::MakeCubeTransform(size_t index) const {
    const size_t side = GetSideCount(kCounts[countIndex]);
    const float offset = (static_cast<float>(side) - 1.0f) * kSpacing * 0.5f;
    const float x = static_cast<float>(index % side) * kSpacing - offset;
    const float z = static_cast<float>(index / side) * kSpacing - offset;
    // 1個ずつ回転の位相をずらす（全部同じ向きだと回転の違いに気づきにくいため）
    const float angle = elapsedTime * kSpinSpeed + static_cast<float>(index) * 0.1f;
    Transform transform;
    transform.scale = { kCubeSize, kCubeSize, kCubeSize };
    transform.rotate = { angle * 0.5f, angle, 0.0f };
    transform.translate = { x, kCubeSize, z };
    return transform;
}

Vector4 InstancingTestScene::MakeCubeColor(size_t index) const {
    // 格子の位置で色を変える（左手前=赤寄り、右奥=青寄り）
    const float side = static_cast<float>(GetSideCount(kCounts[countIndex]));
    const float u = static_cast<float>(index % static_cast<size_t>(side)) / side;
    const float v = static_cast<float>(index / static_cast<size_t>(side)) / side;
    return { 1.0f - u * 0.7f, 0.35f + 0.5f * v * (1.0f - u), 0.3f + u * 0.7f, 1.0f };
}

void InstancingTestScene::EnsureIndividualObjects(size_t count) {
    Object3dCommon* object3dCommon = Framework::GetInstance()->GetObject3dCommon();
    while (individualObjects.size() < count) {
        auto object = std::make_unique<Object3d>();
        object->Initialize(object3dCommon);
        object->SetModel(cubeModel);
        individualObjects.push_back(std::move(object));
    }
}

size_t InstancingTestScene::GetDrawCount() const {
    const size_t count = kCounts[countIndex];
    return useInstancing ? count : (std::min)(count, kMaxIndividualCount);
}

void InstancingTestScene::Draw() {
    const auto start = std::chrono::steady_clock::now();

    // --- シャドウパス（ライト視点の深度のみ） ---
    DrawShadowPass();
    RebindMainRenderTarget();

    // --- 通常の描画パス ---
    Framework::GetInstance()->GetObject3dCommon()->PreDraw();
    floor->Draw();
    const size_t drawCount = GetDrawCount();
    if (useInstancing) {
        instancer.Draw();
        lastDrawCallCount = 2; // 影1回 + 通常1回
    } else {
        for (size_t i = 0; i < drawCount; ++i) {
            individualObjects[i]->Draw();
        }
        lastDrawCallCount = drawCount * 2;
    }

    const auto end = std::chrono::steady_clock::now();
    drawTimeSumMs += std::chrono::duration<double, std::milli>(end - start).count();

    titleText->Draw();
    helpText->Draw();
    statusText->Draw();
}

void InstancingTestScene::DrawShadowPass() {
    Object3dCommon* object3dCommon = Framework::GetInstance()->GetObject3dCommon();
    object3dCommon->PreDrawShadow();
    const Matrix4x4 lightViewProjection = object3dCommon->GetLightViewProjection();
    floor->DrawShadow(lightViewProjection);
    if (useInstancing) {
        instancer.DrawShadow();
    } else {
        const size_t drawCount = GetDrawCount();
        for (size_t i = 0; i < drawCount; ++i) {
            individualObjects[i]->DrawShadow(lightViewProjection);
        }
    }
    object3dCommon->PostDrawShadow();
}

void InstancingTestScene::RebindMainRenderTarget() {
    // PostDrawShadow() はビューポート/シザーしか戻さないので、描画先（RTV/DSV）はここで張り直す
    // （内部解像度を下げているときは小さい描画先になるので、バックバッファを直接指定しない）
    Framework::GetInstance()->GetDxCommon()->BindMainRenderTarget();
}

void InstancingTestScene::UpdateStats() {
    statsTimer += Time::GetUnscaledDeltaTime();
    ++statsFrameCount;
    if (statsTimer < kStatsInterval) {
        return;
    }
    fps = static_cast<float>(statsFrameCount) / statsTimer;
    drawTimeMs = drawTimeSumMs / statsFrameCount;
    statsTimer = 0.0f;
    statsFrameCount = 0;
    drawTimeSumMs = 0.0;
}

std::string InstancingTestScene::BuildStatusText() const {
    const size_t count = kCounts[countIndex];
    const size_t drawCount = GetDrawCount();
    std::string limitNote = drawCount < count ? std::format("（1体ずつは {} 個まで）", kMaxIndividualCount) : "";
    return std::format(
        "描き方: {}\n"
        "個数: {} {}\n"
        "描画命令の数(影+通常): {}\n"
        "描画命令を積む時間(CPU): {:.2f} ms\n"
        "FPS: {:.1f}",
        useInstancing ? "まとめて描く (ModelInstancer)" : "1体ずつ描く (Object3d)",
        drawCount, limitNote,
        lastDrawCallCount,
        drawTimeMs,
        fps);
}
