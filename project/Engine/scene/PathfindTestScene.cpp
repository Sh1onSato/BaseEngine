#include "PathfindTestScene.h"
#include "Camera.h"
#include "CameraManager.h"
#include "DebugDraw.h"
#include "Framework.h"
#include "GameTime.h"
#include "Input.h"
#include "ModelManager.h"
#include "ModelObject.h"
#include "PathFollower.h"
#include "Screen.h"
#include "SpriteCommon.h"
#include <array>
#include <chrono>
#include <cmath>
#include <format>
#include <numbers>

namespace {
    const char* kCameraName = "PathfindTest";
    constexpr int kUiLayer = 100;

    // 床（上面が y = 0）の広さと格子
    constexpr float kFloorWidth = 24.0f;
    constexpr float kFloorDepth = 18.0f;
    constexpr float kCellSize = 0.5f;
    constexpr float kAgentRadius = 0.4f;   // 追いかけ役の球の半径（壁からこれだけ離れた所を歩く）

    constexpr float kChaserSpeed = 3.5f;
    constexpr float kRepathInterval = 0.4f;  // 道を探し直す間隔（秒）
    constexpr float kTargetSpeed = 2.5f;     // 自動で動く目標の速さ
    constexpr float kTargetManualSpeed = 5.0f;
    constexpr float kCatchDistance = 0.9f;   // ここまで近づいたら止まる

    const Vector4 kFloorColor = { 0.55f, 0.55f, 0.6f, 1.0f };
    const Vector4 kWallColor = { 0.4f, 0.4f, 0.45f, 1.0f };
    const Vector4 kTargetColor = { 1.0f, 0.85f, 0.2f, 1.0f };
    const std::array<Vector4, 3> kChaserColors = { Vector4{ 1.0f, 0.35f, 0.3f, 1.0f }, Vector4{ 0.35f, 0.6f, 1.0f, 1.0f },
                                                   Vector4{ 0.4f, 0.9f, 0.4f, 1.0f } };

    // 目標が自動で回る点（壁の間を通る周回コース）
    const std::array<Vector3, 6> kTargetRoute = {
        Vector3{ -9.0f, 0.5f, 6.5f }, Vector3{ 9.0f, 0.5f, 6.5f }, Vector3{ 9.0f, 0.5f, -1.0f },
        Vector3{ 2.5f, 0.5f, -6.5f }, Vector3{ -9.0f, 0.5f, -6.5f }, Vector3{ -9.0f, 0.5f, 0.0f },
    };

    /// <summary>
    /// 動かない壁（Static の箱のコライダー）
    /// </summary>
    class WallBlock : public ModelObject {
    public:
        WallBlock(const Vector3& position, const Vector3& size, const Vector3& rotate, const Vector4& color) : size(size) {
            transform.translate = position;
            transform.scale = size;
            transform.rotate = rotate;
            SetColor(color);
        }
        void Initialize() override {
            SetModel(ModelManager::GetInstance()->GetCubeModel());
            const bool isRotated = transform.rotate.y != 0.0f;
            const Vector3 halfSize = size * 0.5f;
            AddCollider(isRotated ? Collider::MakeOrientedBox(halfSize) : Collider::MakeBox(halfSize))->SetType(ColliderType::kStatic);
        }

    private:
        Vector3 size;
    };

    /// <summary>
    /// 追いかけられる目標。自動でコースを回るか、[←↑↓→] で動かす
    /// </summary>
    class PathTarget : public ModelObject {
    public:
        explicit PathTarget(const Vector3& position) {
            transform.translate = position;
            transform.scale = { 0.8f, 0.8f, 0.8f };
            SetColor(kTargetColor);
        }
        void Initialize() override {
            SetModel(ModelManager::GetInstance()->GetSphereModel());
            SetName("Target");
            // 押し合わないよう Trigger（追いかけ役とぶつかっても動かない）
            AddCollider(Collider::MakeSphere(0.4f))->SetType(ColliderType::kTrigger);
        }
        void Update() override {
            if (isManual) {
                Input* input = Framework::GetInstance()->GetInput();
                Vector3 move = { 0.0f, 0.0f, 0.0f };
                if (input->PushKey(DIK_LEFT)) { move.x -= 1.0f; }
                if (input->PushKey(DIK_RIGHT)) { move.x += 1.0f; }
                if (input->PushKey(DIK_UP)) { move.z += 1.0f; }
                if (input->PushKey(DIK_DOWN)) { move.z -= 1.0f; }
                transform.translate = transform.translate + move * (kTargetManualSpeed * Time::GetDeltaTime());
                return;
            }
            // 次の点へ一定の速さで進み、着いたらその次へ
            const Vector3& next = kTargetRoute[routeIndex];
            const Vector3 toNext = next - transform.translate;
            const float distance = Calculation::Length(toNext);
            const float step = kTargetSpeed * Time::GetDeltaTime();
            if (distance <= step) {
                transform.translate = next;
                routeIndex = (routeIndex + 1) % kTargetRoute.size();
            } else {
                transform.translate = transform.translate + toNext * (step / distance);
            }
        }
        void SetManual(bool manual) { isManual = manual; }
        bool IsManual() const { return isManual; }

    private:
        size_t routeIndex = 0;
        bool isManual = false;
    };

    /// <summary>
    /// 目標へ道を探して進む追いかけ役（Rigidbody の球）。時々道を探し直す
    /// </summary>
    class Chaser : public ModelObject {
    public:
        Chaser(PathfindTestScene* scene, const Vector3& position, const Vector4& color, float repathOffset)
            : scene(scene), repathTimer(repathOffset), pathColor(color) {
            transform.translate = position;
            transform.scale = { kAgentRadius * 2.0f, kAgentRadius * 2.0f, kAgentRadius * 2.0f };
            SetColor(color);
        }
        void Initialize() override {
            SetModel(ModelManager::GetInstance()->GetSphereModel());
            SetName("Chaser");
            AddCollider(Collider::MakeSphere(kAgentRadius));
            Rigidbody* body = AddRigidbody();
            // 速さはこちらで直接決めるので摩擦は要らない（壁に擦って止まらないように）
            body->SetFriction(0.0f);
        }
        void Update() override {
            PathTarget* target = FindTarget();
            if (!target) {
                return;
            }
            // 一定間隔で道を探し直す（追いかけ役ごとに時間をずらして、同じフレームに重ならないようにしてある）
            repathTimer -= Time::GetDeltaTime();
            if (repathTimer <= 0.0f) {
                repathTimer = kRepathInterval;
                std::vector<Vector3> path;
                if (scene->GetGrid().FindPath(transform.translate, target->transform.translate, path, scene->GetPathOptions())) {
                    follower.SetPath(path, NavPlane::kXZ);
                } else {
                    follower.Clear();
                }
            }
            Rigidbody* body = GetRigidbody();
            const float distanceToTarget = Calculation::Length(target->transform.translate - transform.translate);
            const Vector3 direction = distanceToTarget > kCatchDistance ? follower.Update(transform.translate) : Vector3{ 0.0f, 0.0f, 0.0f };
            body->velocity.x = direction.x * kChaserSpeed;
            body->velocity.z = direction.z * kChaserSpeed;
        }
        const PathFollower& GetFollower() const { return follower; }
        const Vector4& GetPathColor() const { return pathColor; }

    private:
        PathTarget* FindTarget() const {
            // 目標は1つだけなので名前で探す
            return dynamic_cast<PathTarget*>(GetManager() ? GetManager()->Find("Target") : nullptr);
        }

        PathfindTestScene* scene = nullptr;
        PathFollower follower;
        float repathTimer = 0.0f;
        Vector4 pathColor;
    };
}

void PathfindTestScene::Initialize() {
    SetupCamera();
    InitializeTexts();
    SpawnAll();
}

void PathfindTestScene::SetupCamera() {
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

void PathfindTestScene::InitializeTexts() {
    SpriteCommon* spriteCommon = Framework::GetInstance()->GetSpriteCommon();

    titleText = std::make_unique<Text>();
    titleText->Initialize(spriteCommon, 24.0f, L"Yu Gothic UI", true);
    titleText->SetText("経路探索テスト (PATHFIND_TEST)");
    titleText->SetLayer(kUiLayer);

    helpText = std::make_unique<Text>();
    helpText->Initialize(spriteCommon, 18.0f);
    helpText->SetText("[Space] 目標を手で動かす（[←↑↓→]）/ 自動  [G] 格子  [P] 道  [S] 道をなめらかに ON/OFF  [D] 斜め ON/OFF  [R] 並べ直す");
    helpText->SetLayer(kUiLayer);

    statusText = std::make_unique<Text>();
    statusText->Initialize(spriteCommon, 18.0f, L"MS Gothic");
    statusText->SetLayer(kUiLayer);
}

void PathfindTestScene::SpawnAll() {
    objects.Clear();
    const Vector3 noRotate = { 0.0f, 0.0f, 0.0f };
    objects.Create<WallBlock>(Vector3{ 0.0f, -0.5f, 0.0f }, Vector3{ kFloorWidth, 1.0f, kFloorDepth }, noRotate, kFloorColor)->SetName("Floor");
    // 外周の壁
    objects.Create<WallBlock>(Vector3{ 0.0f, 0.75f, 9.25f }, Vector3{ 24.0f, 1.5f, 0.5f }, noRotate, kWallColor);
    objects.Create<WallBlock>(Vector3{ 0.0f, 0.75f, -9.25f }, Vector3{ 24.0f, 1.5f, 0.5f }, noRotate, kWallColor);
    objects.Create<WallBlock>(Vector3{ 12.25f, 0.75f, 0.0f }, Vector3{ 0.5f, 1.5f, 18.0f }, noRotate, kWallColor);
    objects.Create<WallBlock>(Vector3{ -12.25f, 0.75f, 0.0f }, Vector3{ 0.5f, 1.5f, 18.0f }, noRotate, kWallColor);
    // 中の壁（回り込まないと通れない形と、斜めの壁）
    objects.Create<WallBlock>(Vector3{ -4.0f, 0.75f, 3.0f }, Vector3{ 12.0f, 1.5f, 0.6f }, noRotate, kWallColor);
    objects.Create<WallBlock>(Vector3{ 4.0f, 0.75f, -3.0f }, Vector3{ 12.0f, 1.5f, 0.6f }, noRotate, kWallColor);
    objects.Create<WallBlock>(Vector3{ -6.0f, 0.75f, -5.0f }, Vector3{ 0.6f, 1.5f, 6.0f }, noRotate, kWallColor);
    objects.Create<WallBlock>(Vector3{ 6.0f, 0.75f, 5.5f }, Vector3{ 0.6f, 1.5f, 5.0f }, noRotate, kWallColor);
    objects.Create<WallBlock>(Vector3{ 0.0f, 0.75f, 0.0f }, Vector3{ 3.0f, 1.5f, 0.6f }, Vector3{ 0.0f, std::numbers::pi_v<float> * 0.25f, 0.0f }, kWallColor);

    objects.Create<PathTarget>(kTargetRoute[0]);
    const Vector3 chaserStarts[3] = { { 10.0f, 0.5f, -7.5f }, { -10.0f, 0.5f, -1.5f }, { 3.0f, 0.5f, 1.0f } };
    for (int i = 0; i < 3; ++i) {
        objects.Create<Chaser>(this, chaserStarts[i], kChaserColors[i], kRepathInterval * static_cast<float>(i) / 3.0f);
    }
    // 壁ができたので、そのコライダーから歩けない格子を作る
    BuildGrid();
}

void PathfindTestScene::BuildGrid() {
    const auto start = std::chrono::steady_clock::now();
    grid.Initialize({ -kFloorWidth * 0.5f, 0.0f, -kFloorDepth * 0.5f },
                    static_cast<int>(kFloorWidth / kCellSize), static_cast<int>(kFloorDepth / kCellSize), kCellSize, NavPlane::kXZ);
    grid.BlockColliders(objects, kAgentRadius);
    const auto end = std::chrono::steady_clock::now();
    lastBuildMilliseconds = std::chrono::duration<float, std::milli>(end - start).count();
}

void PathfindTestScene::Finalize() {
    objects.Clear();
    // 登録したカメラを消す（次のシーンに残さない）
    CameraManager::GetInstance()->Initialize();
}

void PathfindTestScene::Update() {
    UpdateInput();
    objects.Update();
    camera->Update();

    if (isGridVisible) {
        grid.DrawDebug();
    }
    if (isPathVisible) {
        for (Chaser* chaser : objects.FindAllByType<Chaser>()) {
            NavGrid::DrawPath(chaser->GetFollower().GetPath(), NavPlane::kXZ, chaser->GetPathColor());
        }
    }

    statusText->SetText(BuildStatusText());
    titleText->SetPosition(Screen::GetAnchoredPosition({ 0.0f, 0.0f }, { 16.0f, 12.0f }));
    helpText->SetPosition(Screen::GetAnchoredPosition({ 0.0f, 0.0f }, { 16.0f, 46.0f }));
    statusText->SetPosition(Screen::GetAnchoredPosition({ 1.0f, 0.0f }, { -16.0f, 76.0f }));
    statusText->SetAnchorPoint({ 1.0f, 0.0f });
    titleText->Update();
    helpText->Update();
    statusText->Update();
}

void PathfindTestScene::UpdateInput() {
    Input* input = Framework::GetInstance()->GetInput();
    if (input->TriggerKey(DIK_SPACE)) {
        if (PathTarget* target = objects.FindByType<PathTarget>()) {
            target->SetManual(!target->IsManual());
        }
    }
    if (input->TriggerKey(DIK_G)) { isGridVisible = !isGridVisible; }
    if (input->TriggerKey(DIK_P)) { isPathVisible = !isPathVisible; }
    if (input->TriggerKey(DIK_S)) { pathOptions.smooth = !pathOptions.smooth; }
    if (input->TriggerKey(DIK_D)) { pathOptions.allowDiagonal = !pathOptions.allowDiagonal; }
    if (input->TriggerKey(DIK_R)) { SpawnAll(); }
}

void PathfindTestScene::Draw() {
    // --- シャドウパス（ライト視点の深度のみ） ---
    DrawShadowPass();
    // シャドウパスで外れた描画先を戻す（内部解像度・画面の効果を使っているときは別の描画先なので、バックバッファを直接指定しない）
    Framework::GetInstance()->GetDxCommon()->BindMainRenderTarget();

    // --- 通常の描画パス ---
    Framework::GetInstance()->GetObject3dCommon()->PreDraw();
    objects.Draw();

    titleText->Draw();
    helpText->Draw();
    statusText->Draw();
}

void PathfindTestScene::DrawShadowPass() {
    Object3dCommon* object3dCommon = Framework::GetInstance()->GetObject3dCommon();
    object3dCommon->PreDrawShadow();
    objects.DrawShadow(object3dCommon->GetLightViewProjection());
    object3dCommon->PostDrawShadow();
}

std::string PathfindTestScene::BuildStatusText() const {
    int blocked = 0;
    for (int y = 0; y < grid.GetHeight(); ++y) {
        for (int x = 0; x < grid.GetWidth(); ++x) {
            blocked += grid.IsWalkable(x, y) ? 0 : 1;
        }
    }
    const PathTarget* target = objects.FindByType<PathTarget>();
    return std::format(
        "格子 {}x{}（1マス {:.1f}）歩けない {} マス\n"
        "格子を作る時間 {:.2f} ms\n"
        "斜め {}  なめらか {}  目標 {}",
        grid.GetWidth(), grid.GetHeight(), grid.GetCellSize(), blocked, lastBuildMilliseconds,
        pathOptions.allowDiagonal ? "ON" : "OFF", pathOptions.smooth ? "ON" : "OFF",
        (target && target->IsManual()) ? "手動" : "自動");
}
