#include "TweenTestScene.h"
#include "Camera.h"
#include "CameraManager.h"
#include "Framework.h"
#include "GameTime.h"
#include "Input.h"
#include "ModelManager.h"
#include "Object3d.h"
#include "Screen.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "Tween.h"
#include <array>
#include <format>
#include <numbers>

namespace {
    const char* kCameraName = "TweenTest";
    constexpr int kUiLayer = 100;
    constexpr float kPi = std::numbers::pi_v<float>;

    // 左: イージングの見本（名前と関数）。箱は kEaseFrom から kEaseDistance だけ右へ、行って戻る
    struct EaseEntry {
        const char* name;
        EaseFunction ease;
    };
    const std::array<EaseEntry, 8> kEases = { {
        { "Linear", Easing::Linear },
        { "OutQuad", Easing::OutQuad },
        { "OutCubic", Easing::OutCubic },
        { "InOutSine", Easing::InOutSine },
        { "InExpo", Easing::InExpo },
        { "OutBack", Easing::OutBack },
        { "OutElastic", Easing::OutElastic },
        { "OutBounce", Easing::OutBounce },
    } };
    const Vector2 kEaseFrom = { 150.0f, 140.0f };
    constexpr float kEaseDistance = 380.0f;
    constexpr float kEaseRowHeight = 52.0f;
    constexpr float kEaseSeconds = 1.2f;
    const Vector2 kEaseBoxSize = { 28.0f, 28.0f };

    // 右上: 順番に動く箱
    const Vector2 kSequenceStart = { 760.0f, 170.0f };
    const Vector2 kSequenceEnd = { 1080.0f, 170.0f };
    const Vector2 kSequenceBoxSize = { 70.0f, 70.0f };

    // 右下: 立方体と、[H] [S] の揺れ・ヒットストップ
    constexpr float kCubeTurnSeconds = 2.0f;
    constexpr float kHitStopSeconds = 0.15f;
    constexpr float kHitShakeStrength = 0.4f;
    constexpr float kHitShakeSeconds = 0.35f;
    constexpr float kShakeStrength = 0.25f;
    constexpr float kShakeSeconds = 0.8f;
}

TweenTestScene::TweenTestScene() = default;
// Object3d・Sprite の中身はヘッダーで見えないので、デストラクタは .cpp に置く
TweenTestScene::~TweenTestScene() = default;

void TweenTestScene::Initialize() {
    SetupCamera();
    InitializeTexts();
    SetupEaseSamples();
    SetupSequence();
    SetupCube();
}

void TweenTestScene::SetupCamera() {
    // 一度空にしてから登録する（シーン読み直しで同じ名前を登録しても assert にならない）
    CameraManager* cameraManager = CameraManager::GetInstance();
    cameraManager->Initialize();
    std::unique_ptr<Camera> newCamera = std::make_unique<Camera>();
    camera = newCamera.get();
    camera->SetTranslate({ 0.0f, 2.0f, -8.0f });
    camera->SetRotate({ 0.15f, 0.0f, 0.0f });
    camera->Update();
    cameraManager->AddCamera(kCameraName, std::move(newCamera));
    cameraManager->SetActiveCamera(kCameraName);
}

void TweenTestScene::InitializeTexts() {
    SpriteCommon* spriteCommon = Framework::GetInstance()->GetSpriteCommon();

    titleText = std::make_unique<Text>();
    titleText->Initialize(spriteCommon, 24.0f, L"Yu Gothic UI", true);
    titleText->SetText("Tween・演出テスト (TWEEN_TEST)");
    titleText->SetLayer(kUiLayer);

    helpText = std::make_unique<Text>();
    helpText->Initialize(spriteCommon, 18.0f);
    helpText->SetText("[H] ヒットストップ＋揺れ  [S] カメラを揺らす  [P] 時間を止める / 動かす（「UI」の文字だけは動き続ける）");
    helpText->SetLayer(kUiLayer);

    statusText = std::make_unique<Text>();
    statusText->Initialize(spriteCommon, 18.0f, L"MS Gothic");
    statusText->SetLayer(kUiLayer);

    // 時間を止めても動き続ける文字（ポーズメニューの UI の動き）
    unscaledText = std::make_unique<Text>();
    unscaledText->Initialize(spriteCommon, 28.0f, L"Yu Gothic UI", true);
    unscaledText->SetText("UI");
    unscaledText->SetAnchorPoint({ 0.5f, 0.5f });
    unscaledText->SetPosition({ 1180.0f, 620.0f });
    unscaledText->SetLayer(kUiLayer);
    Tweens::Scale(unscaledText.get(), 1.6f, 0.5f).SetEase(Easing::InOutSine).SetLoops(-1, TweenLoop::kYoyo).SetUnscaledTime();
}

void TweenTestScene::SetupEaseSamples() {
    SpriteCommon* spriteCommon = Framework::GetInstance()->GetSpriteCommon();
    for (size_t i = 0; i < kEases.size(); ++i) {
        const float y = kEaseFrom.y + kEaseRowHeight * static_cast<float>(i);
        EaseSample sample;
        sample.box = std::make_unique<Sprite>();
        sample.box->Initialize(spriteCommon, "white");
        sample.box->SetSize(kEaseBoxSize);
        sample.box->SetAnchorPoint({ 0.5f, 0.5f });
        sample.box->SetPosition({ kEaseFrom.x, y });
        sample.box->SetColor({ 0.3f + 0.08f * static_cast<float>(i), 0.8f, 1.0f - 0.08f * static_cast<float>(i), 1.0f });
        sample.label = std::make_unique<Text>();
        sample.label->Initialize(spriteCommon, 16.0f, L"MS Gothic");
        sample.label->SetText(kEases[i].name);
        sample.label->SetAnchorPoint({ 1.0f, 0.5f });
        sample.label->SetPosition({ kEaseFrom.x - 30.0f, y });
        sample.label->SetLayer(kUiLayer);
        // 右へ行って戻るをずっと繰り返す
        Tweens::Move(sample.box.get(), { kEaseFrom.x + kEaseDistance, y }, kEaseSeconds).SetEase(kEases[i].ease).SetLoops(-1, TweenLoop::kYoyo);
        easeSamples.push_back(std::move(sample));
    }
}

void TweenTestScene::SetupSequence() {
    SpriteCommon* spriteCommon = Framework::GetInstance()->GetSpriteCommon();
    sequenceBox = std::make_unique<Sprite>();
    sequenceBox->Initialize(spriteCommon, "white");
    sequenceBox->SetAnchorPoint({ 0.5f, 0.5f });
    sequenceBox->SetPosition(kSequenceStart);
    sequenceBox->SetSize({ 0.0f, 0.0f });
    sequenceBox->SetColor({ 1.0f, 0.6f, 0.2f, 1.0f });
    sequenceText = std::make_unique<Text>();
    sequenceText->Initialize(spriteCommon, 18.0f);
    sequenceText->SetPosition({ kSequenceStart.x - 40.0f, kSequenceStart.y + 60.0f });
    sequenceText->SetLayer(kUiLayer);

    Sprite* box = sequenceBox.get();
    // 出てくる → 右へ動く（同時に回る）→ 色が変わる → 待つ → 縮んで消える → 元の場所へ戻す、をずっと繰り返す
    TweenSequence& sequence = Tweens::Sequence();
    sequence.AppendCallback([this, box] {
        box->SetPosition(kSequenceStart);
        box->SetRotation(0.0f);
        box->SetColor({ 1.0f, 0.6f, 0.2f, 1.0f });
        sequenceText->SetText("出てくる");
    });
    sequence.Append(Tweens::FromTo<Vector2>([box](const Vector2& size) { box->SetSize(size); }, { 0.0f, 0.0f }, kSequenceBoxSize, 0.4f)
                        .SetEase(Easing::OutBack));
    sequence.AppendCallback([this] { sequenceText->SetText("動きながら回る"); });
    sequence.Append(Tweens::FromTo<Vector2>([box](const Vector2& position) { box->SetPosition(position); }, kSequenceStart, kSequenceEnd, 0.8f)
                        .SetEase(Easing::InOutCubic));
    sequence.Join(Tweens::FromTo<float>([box](const float& rotation) { box->SetRotation(rotation); }, 0.0f, kPi, 0.8f).SetEase(Easing::InOutCubic));
    sequence.AppendCallback([this] { sequenceText->SetText("色が変わる"); });
    sequence.Append(Tweens::FromTo<Vector4>([box](const Vector4& color) { box->SetColor(color); }, { 1.0f, 0.6f, 0.2f, 1.0f },
                                            { 0.3f, 0.5f, 1.0f, 1.0f }, 0.4f));
    sequence.AppendInterval(0.4f);
    sequence.AppendCallback([this] { sequenceText->SetText("消える"); });
    sequence.Append(Tweens::FromTo<Vector2>([box](const Vector2& size) { box->SetSize(size); }, kSequenceBoxSize, { 0.0f, 0.0f }, 0.3f)
                        .SetEase(Easing::InBack));
    sequence.AppendCallback([this] { ++sequenceLoopCount; });
    sequence.AppendInterval(0.3f);
    sequence.SetLoops(-1);
}

void TweenTestScene::SetupCube() {
    Object3dCommon* object3dCommon = Framework::GetInstance()->GetObject3dCommon();
    ModelManager* models = ModelManager::GetInstance();
    floor = std::make_unique<Object3d>();
    floor->Initialize(object3dCommon);
    floor->SetModel(models->GetCubeModel());
    floor->SetScale({ 20.0f, 0.2f, 10.0f });
    floor->SetTranslate({ 0.0f, -0.1f, 3.0f });
    floor->SetColor({ 0.5f, 0.5f, 0.55f, 1.0f });
    cube = std::make_unique<Object3d>();
    cube->Initialize(object3dCommon);
    cube->SetModel(models->GetCubeModel());
    cube->SetTranslate({ 3.2f, 1.0f, 2.0f });
    cube->SetColor({ 0.9f, 0.3f, 0.3f, 1.0f });
    // 1周回るのをずっと繰り返す・上下に弾む
    Tweens::Rotate(cube.get(), { 0.0f, kPi * 2.0f, 0.0f }, kCubeTurnSeconds).SetLoops(-1);
    Tweens::Move(cube.get(), { 3.2f, 2.0f, 2.0f }, 0.6f).SetEase(Easing::OutQuad).SetLoops(-1, TweenLoop::kYoyo);
}

void TweenTestScene::Finalize() {
    // 時間を止めたままにしない（Tween は SceneManager が止める）
    Time::SetTimeScale(1.0f);
    // 登録したカメラを消す（次のシーンに残さない）
    CameraManager::GetInstance()->Initialize();
}

void TweenTestScene::Update() {
    UpdateInput();
    camera->Update();
    floor->Update();
    cube->Update();
    for (EaseSample& sample : easeSamples) {
        sample.box->Update();
        sample.label->Update();
    }
    sequenceBox->Update();
    sequenceText->Update();
    unscaledText->Update();

    statusText->SetText(BuildStatusText());
    titleText->SetPosition(Screen::GetAnchoredPosition({ 0.0f, 0.0f }, { 16.0f, 12.0f }));
    helpText->SetPosition(Screen::GetAnchoredPosition({ 0.0f, 0.0f }, { 16.0f, 46.0f }));
    statusText->SetPosition(Screen::GetAnchoredPosition({ 0.0f, 0.0f }, { 16.0f, 80.0f }));
    titleText->Update();
    helpText->Update();
    statusText->Update();
}

void TweenTestScene::UpdateInput() {
    Input* input = Framework::GetInstance()->GetInput();
    if (input->TriggerKey(DIK_H)) {
        HitStop();
    }
    if (input->TriggerKey(DIK_S)) {
        camera->Shake(kShakeStrength, kShakeSeconds);
    }
    if (input->TriggerKey(DIK_P)) {
        isTimeStopped = !isTimeStopped;
        Time::SetTimeScale(isTimeStopped ? 0.0f : 1.0f);
    }
}

void TweenTestScene::HitStop() {
    // 当たった瞬間: ゲームの時間を一瞬止めて、カメラを強く短く揺らす
    ++hitStopCount;
    Time::HitStop(kHitStopSeconds);
    camera->Shake(kHitShakeStrength, kHitShakeSeconds, 30.0f);
    // 立方体を一瞬白くして戻す（止まっている間も色は戻したいので、時間を止めても動く Tween にする）
    cube->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
    Tweens::Color(cube.get(), { 0.9f, 0.3f, 0.3f, 1.0f }, 0.3f).SetUnscaledTime();
}

void TweenTestScene::Draw() {
    // --- シャドウパス（ライト視点の深度のみ） ---
    DrawShadowPass();
    Framework::GetInstance()->GetDxCommon()->BindMainRenderTarget();

    // --- 通常の描画パス ---
    Framework::GetInstance()->GetObject3dCommon()->PreDraw();
    floor->Draw();
    cube->Draw();

    for (EaseSample& sample : easeSamples) {
        sample.box->Draw();
        sample.label->Draw();
    }
    sequenceBox->Draw();
    sequenceText->Draw();
    unscaledText->Draw();
    titleText->Draw();
    helpText->Draw();
    statusText->Draw();
}

void TweenTestScene::DrawShadowPass() {
    Object3dCommon* object3dCommon = Framework::GetInstance()->GetObject3dCommon();
    object3dCommon->PreDrawShadow();
    const Matrix4x4 lightViewProjection = object3dCommon->GetLightViewProjection();
    floor->DrawShadow(lightViewProjection);
    cube->DrawShadow(lightViewProjection);
    object3dCommon->PostDrawShadow();
}

std::string TweenTestScene::BuildStatusText() const {
    return std::format("動いている Tween {} 本  順番の動き {} 周  ヒットストップ {} 回{}{}{}", Tweens::GetActiveCount(), sequenceLoopCount, hitStopCount,
                       Time::IsHitStopping() ? "  【止まっています】" : "", camera->IsShaking() ? "  【揺れています】" : "",
                       isTimeStopped ? "  【時間を止めています】" : "");
}
