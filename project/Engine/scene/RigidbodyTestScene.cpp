#include "RigidbodyTestScene.h"
#include "Calculation.h"
#include "Camera.h"
#include "CameraManager.h"
#include "DebugDraw.h"
#include "Framework.h"
#include "GameTime.h"
#include "Input.h"
#include "ModelManager.h"
#include "ModelObject.h"
#include "Screen.h"
#include "SpriteCommon.h"
#include <cmath>
#include <format>
#include <numbers>

namespace {
    const char* kCameraName = "RigidbodyTest";
    constexpr int kUiLayer = 100;
    constexpr float kPi = std::numbers::pi_v<float>;

    const Vector3 kCameraPosition = { 0.0f, 7.0f, -16.0f };
    const Vector3 kCameraRotate = { 0.38f, 0.0f, 0.0f };

    // 積んだ箱（左）
    const Vector3 kTowerBase = { -5.0f, 0.0f, 2.0f };
    constexpr int kTowerHeight = 6;
    constexpr float kTowerBoxSize = 1.0f;
    // ドミノ（真ん中の手前）
    const Vector3 kDominoStart = { -1.5f, 0.0f, -3.0f };
    constexpr int kDominoCount = 10;
    constexpr float kDominoSpacing = 0.7f;
    const Vector3 kDominoSize = { 0.15f, 1.2f, 0.6f };
    constexpr float kDominoPushDelay = 1.5f;
    // 坂（右）
    const Vector3 kRampCenter = { 5.5f, 1.5f, 3.0f };
    const Vector3 kRampSize = { 6.0f, 0.4f, 3.0f };
    constexpr float kRampAngle = 0.4f;
    // 投げる球
    constexpr float kThrowSpeed = 18.0f;
    constexpr float kThrowRadius = 0.45f;
    constexpr float kAutoThrowDelay = 3.0f;
    // 大きく外へ出た物は消す
    constexpr float kFallLimit = -20.0f;

    const Vector4 kFloorColor = { 0.55f, 0.55f, 0.6f, 1.0f };

    /// <summary>
    /// 物理で動く（回る）箱・球・カプセル、または動かない床・坂
    /// </summary>
    class PhysicsShape : public ModelObject {
    public:
        enum class Kind { kBox, kSphere, kCapsule, kStaticBox };

        PhysicsShape(Kind kind, const Vector3& position, const Vector3& size, const Vector4& color, const Vector3& rotate = { 0.0f, 0.0f, 0.0f })
            : kind(kind), size(size) {
            transform.translate = position;
            transform.rotate = rotate;
            transform.scale = size;
            SetColor(color);
        }
        void Initialize() override {
            ModelManager* models = ModelManager::GetInstance();
            switch (kind) {
            case Kind::kStaticBox:
                SetModel(models->GetCubeModel());
                // 回した坂は OBB、回していない床は AABB
                if (transform.rotate.x != 0.0f || transform.rotate.y != 0.0f || transform.rotate.z != 0.0f) {
                    AddCollider(Collider::MakeOrientedBox(size * 0.5f))->SetType(ColliderType::kStatic);
                } else {
                    AddCollider(Collider::MakeBox(size * 0.5f))->SetType(ColliderType::kStatic);
                }
                return;
            case Kind::kBox:
                SetModel(models->GetCubeModel());
                AddCollider(Collider::MakeOrientedBox(size * 0.5f));
                break;
            case Kind::kSphere:
                SetModel(models->GetSphereModel());
                AddCollider(Collider::MakeSphere(size.x * 0.5f));
                break;
            case Kind::kCapsule:
                // 見た目は Debug ビルドの線（カプセルのモデルが無いため）。size.x = 半径、size.y = 両端の球の中心の間
                AddCollider(Collider::MakeCapsule(size.x, size.y));
                transform.scale = { 1.0f, 1.0f, 1.0f };
                break;
            }
            Rigidbody* body = AddRigidbody();
            body->SetUseRotation(true);
            body->SetFriction(0.5f);
        }
        void Draw() override {
            if (kind == Kind::kCapsule) {
                DebugDraw::Capsule(GetCollider()->GetWorldCapsule(), GetColor());
                return;
            }
            ModelObject::Draw();
        }
        void Update() override {
            if (transform.translate.y < kFallLimit) {
                Destroy();
            }
        }

    private:
        Kind kind;
        Vector3 size;
    };
}

void RigidbodyTestScene::Initialize() {
    SetupCamera();
    InitializeTexts();
    SpawnAll();
}

void RigidbodyTestScene::SetupCamera() {
    // 一度空にしてから登録する（シーン読み直しで同じ名前を登録しても assert にならない）
    CameraManager* cameraManager = CameraManager::GetInstance();
    cameraManager->Initialize();
    std::unique_ptr<Camera> newCamera = std::make_unique<Camera>();
    camera = newCamera.get();
    camera->SetTranslate(kCameraPosition);
    camera->SetRotate(kCameraRotate);
    camera->Update();
    cameraManager->AddCamera(kCameraName, std::move(newCamera));
    cameraManager->SetActiveCamera(kCameraName);
}

void RigidbodyTestScene::InitializeTexts() {
    SpriteCommon* spriteCommon = Framework::GetInstance()->GetSpriteCommon();

    titleText = std::make_unique<Text>();
    titleText->Initialize(spriteCommon, 24.0f, L"Yu Gothic UI", true);
    titleText->SetText("回転する剛体テスト (RIGIDBODY_TEST)");
    titleText->SetLayer(kUiLayer);

    helpText = std::make_unique<Text>();
    helpText->Initialize(spriteCommon, 18.0f);
    helpText->SetText("[Space] 球を投げる  [R] 並べ直す（3 秒後に左の箱へ自動で1回投げる）  [F7] 当たり判定の形");
    helpText->SetLayer(kUiLayer);

    statusText = std::make_unique<Text>();
    statusText->Initialize(spriteCommon, 18.0f, L"MS Gothic");
    statusText->SetLayer(kUiLayer);
}

void RigidbodyTestScene::SpawnAll() {
    objects.Clear();
    elapsedTime = 0.0f;
    hasPushedDomino = false;
    hasThrownAuto = false;
    objects.Create<PhysicsShape>(PhysicsShape::Kind::kStaticBox, Vector3{ 0.0f, -0.5f, 0.0f }, Vector3{ 30.0f, 1.0f, 16.0f }, kFloorColor);
    SpawnTower();
    SpawnDominoes();
    SpawnRamp();
    // 斜めに落とす箱（角から着地して倒れる）とカプセル（倒れて転がる）
    objects.Create<PhysicsShape>(PhysicsShape::Kind::kBox, Vector3{ 1.0f, 4.0f, 3.0f }, Vector3{ 1.2f, 1.2f, 1.2f },
                                 Vector4{ 0.9f, 0.6f, 0.2f, 1.0f }, Vector3{ 0.5f, 0.3f, 0.7f });
    objects.Create<PhysicsShape>(PhysicsShape::Kind::kCapsule, Vector3{ 2.5f, 3.0f, 0.5f }, Vector3{ 0.35f, 1.4f, 0.0f },
                                 Vector4{ 0.3f, 1.0f, 0.5f, 1.0f }, Vector3{ 0.0f, 0.0f, 0.4f });
}

void RigidbodyTestScene::SpawnTower() {
    for (int i = 0; i < kTowerHeight; ++i) {
        const float y = kTowerBoxSize * (0.5f + static_cast<float>(i));
        const float shade = 0.5f + 0.08f * static_cast<float>(i);
        objects.Create<PhysicsShape>(PhysicsShape::Kind::kBox, kTowerBase + Vector3{ 0.0f, y, 0.0f },
                                     Vector3{ kTowerBoxSize, kTowerBoxSize, kTowerBoxSize }, Vector4{ shade, 0.4f, 0.4f, 1.0f });
    }
}

void RigidbodyTestScene::SpawnDominoes() {
    for (int i = 0; i < kDominoCount; ++i) {
        const Vector3 position = kDominoStart + Vector3{ kDominoSpacing * static_cast<float>(i), kDominoSize.y * 0.5f, 0.0f };
        PhysicsShape* domino = objects.Create<PhysicsShape>(PhysicsShape::Kind::kBox, position, kDominoSize, Vector4{ 0.9f, 0.9f, 0.95f, 1.0f });
        if (i == 0) {
            domino->SetName("FirstDomino");
        }
    }
}

void RigidbodyTestScene::SpawnRamp() {
    // 右が高い坂と、その上に置いた球（転がり落ちる）
    objects.Create<PhysicsShape>(PhysicsShape::Kind::kStaticBox, kRampCenter, kRampSize, Vector4{ 0.5f, 0.6f, 0.75f, 1.0f },
                                 Vector3{ 0.0f, 0.0f, kRampAngle });
    const Vector3 top = kRampCenter + Vector3{ std::cos(kRampAngle) * 2.4f, std::sin(kRampAngle) * 2.4f + 0.75f, 0.0f };
    objects.Create<PhysicsShape>(PhysicsShape::Kind::kSphere, top, Vector3{ 0.8f, 0.8f, 0.8f }, Vector4{ 0.3f, 0.6f, 1.0f, 1.0f });
}

void RigidbodyTestScene::ThrowBall() {
    // カメラから左の箱の真ん中あたりへ投げる
    const Vector3 start = kCameraPosition + Vector3{ 0.0f, -2.0f, 2.0f };
    const Vector3 target = kTowerBase + Vector3{ 0.0f, kTowerBoxSize * 2.5f, 0.0f };
    PhysicsShape* ball = objects.Create<PhysicsShape>(PhysicsShape::Kind::kSphere, start, Vector3{ kThrowRadius * 2.0f, kThrowRadius * 2.0f, kThrowRadius * 2.0f },
                                                      Vector4{ 1.0f, 0.85f, 0.2f, 1.0f });
    // Create の中で Initialize（AddRigidbody）まで済んでいる
    Rigidbody* body = ball->GetRigidbody();
    body->SetMass(3.0f);
    body->velocity = Calculation::Normalize(target - start) * kThrowSpeed;
}

void RigidbodyTestScene::Finalize() {
    objects.Clear();
    // 登録したカメラを消す（次のシーンに残さない）
    CameraManager::GetInstance()->Initialize();
}

void RigidbodyTestScene::Update() {
    UpdateInput();
    elapsedTime += Time::GetDeltaTime();
    // 最初のドミノを上の方で押して倒す（ずれた所を押すので回る）
    if (!hasPushedDomino && elapsedTime >= kDominoPushDelay) {
        hasPushedDomino = true;
        if (GameObject* first = objects.Find("FirstDomino")) {
            const Vector3 top = first->transform.translate + Vector3{ 0.0f, kDominoSize.y * 0.4f, 0.0f };
            first->GetRigidbody()->AddImpulseAtPosition({ 0.6f, 0.0f, 0.0f }, top);
        }
    }
    if (!hasThrownAuto && elapsedTime >= kAutoThrowDelay) {
        hasThrownAuto = true;
        ThrowBall();
    }
    objects.Update();
    camera->Update();

    statusText->SetText(BuildStatusText());
    titleText->SetPosition(Screen::GetAnchoredPosition({ 0.0f, 0.0f }, { 16.0f, 12.0f }));
    helpText->SetPosition(Screen::GetAnchoredPosition({ 0.0f, 0.0f }, { 16.0f, 46.0f }));
    statusText->SetPosition(Screen::GetAnchoredPosition({ 0.0f, 0.0f }, { 16.0f, 76.0f }));
    titleText->Update();
    helpText->Update();
    statusText->Update();
}

void RigidbodyTestScene::UpdateInput() {
    Input* input = Framework::GetInstance()->GetInput();
    if (input->TriggerKey(DIK_SPACE)) {
        ThrowBall();
    }
    if (input->TriggerKey(DIK_R)) {
        SpawnAll();
    }
}

void RigidbodyTestScene::Draw() {
    // --- シャドウパス（ライト視点の深度のみ） ---
    DrawShadowPass();
    Framework::GetInstance()->GetDxCommon()->BindMainRenderTarget();

    // --- 通常の描画パス ---
    Framework::GetInstance()->GetObject3dCommon()->PreDraw();
    objects.Draw();

    titleText->Draw();
    helpText->Draw();
    statusText->Draw();
}

void RigidbodyTestScene::DrawShadowPass() {
    Object3dCommon* object3dCommon = Framework::GetInstance()->GetObject3dCommon();
    object3dCommon->PreDrawShadow();
    objects.DrawShadow(object3dCommon->GetLightViewProjection());
    object3dCommon->PostDrawShadow();
}

std::string RigidbodyTestScene::BuildStatusText() const {
    // 動いている物の数と、一番速く回っている物の回る速さ（止まりきるかの確認用）
    int moving = 0;
    float maxAngularSpeed = 0.0f;
    float maxSpeed = 0.0f;
    for (const PhysicsShape* shape : objects.FindAllByType<PhysicsShape>()) {
        const Rigidbody* body = shape->GetRigidbody();
        if (!body) {
            continue;
        }
        const float speed = Calculation::Length(body->velocity);
        const float angularSpeed = Calculation::Length(body->angularVelocity);
        maxSpeed = (std::max)(maxSpeed, speed);
        maxAngularSpeed = (std::max)(maxAngularSpeed, angularSpeed);
        moving += (speed > 0.05f || angularSpeed > 0.05f) ? 1 : 0;
    }
    return std::format("経過 {:.1f} 秒  動いている物 {} 個  一番速い {:.2f}  一番速く回る {:.2f} rad/s  物理 {} 回/フレーム", elapsedTime, moving,
                       maxSpeed, maxAngularSpeed, objects.GetLastPhysicsStepCount());
}
