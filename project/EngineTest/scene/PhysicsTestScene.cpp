#include "PhysicsTestScene.h"
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
#include <format>
#include <string>
#include <vector>

namespace {
    const char* kCameraName = "PhysicsTest";
    const Vector3 kCameraPosition = { 0.0f, 13.0f, -20.0f };
    const Vector3 kCameraRotate = { 0.55f, 0.0f, 0.0f }; // 斜めに見下ろす
    constexpr int kUiLayer = 100;

    const char* kPlayerTag = "Player";
    constexpr float kPlayerSpeed = 5.0f;        // 1秒あたりの移動量
    constexpr float kJumpSpeed = 6.0f;          // ジャンプした瞬間の上向きの速さ
    constexpr float kPlayerStepHeight = 0.3f;   // 歩いて乗れる段差の高さ（プレイヤーは半径 0.5 の球）
    constexpr float kSlowTimeScale = 0.2f;
    constexpr float kPlatformSpeed = 2.0f;
    constexpr float kPlatformRange = 3.0f;      // 動く床が中心から左右に動く距離
    constexpr float kDebugRayLength = 30.0f;
    constexpr float kShotRaySeconds = 2.0f;     // [J] で撃ったレイを残す秒数
    constexpr float kVelocityArrowScale = 0.3f; // 速度の矢印の長さ（速さ 1 あたり）

    const Vector4 kFloorColor = { 0.55f, 0.55f, 0.6f, 1.0f };
    const Vector4 kWallColor = { 0.4f, 0.4f, 0.45f, 1.0f };
    const Vector4 kRampColor = { 0.6f, 0.5f, 0.4f, 1.0f };
    const Vector4 kPlayerColor = { 0.2f, 0.4f, 1.0f, 1.0f };
    const Vector4 kPlayerGroundedColor = { 0.5f, 0.8f, 1.0f, 1.0f };
    const Vector4 kLightBoxColor = { 0.9f, 0.7f, 0.4f, 1.0f };
    const Vector4 kHeavyBoxColor = { 0.4f, 0.25f, 0.1f, 1.0f };
    const Vector4 kPlatformColor = { 0.3f, 0.7f, 0.4f, 1.0f };

    /// <summary>
    /// 動かない物（床・壁・坂）。コライダーは Static。回転していれば OBB
    /// </summary>
    class StaticBlock : public ModelObject {
    public:
        StaticBlock(const Vector3& position, const Vector3& size, const Vector3& rotate, const Vector4& color)
            : size(size) {
            transform.translate = position;
            transform.scale = size;
            transform.rotate = rotate;
            SetColor(color);
        }
        void Initialize() override {
            SetModel(ModelManager::GetInstance()->GetCubeModel());
            const bool isRotated = transform.rotate.x != 0.0f || transform.rotate.y != 0.0f || transform.rotate.z != 0.0f;
            const Vector3 halfSize = size * 0.5f;
            Collider* collider = AddCollider(isRotated ? Collider::MakeOrientedBox(halfSize) : Collider::MakeBox(halfSize));
            collider->SetType(ColliderType::kStatic);
        }

    private:
        Vector3 size;
    };

    /// <summary>
    /// 物理で動く物（球か箱）。作った後に GetRigidbody() で跳ね返り・質量などを設定する
    /// </summary>
    class PhysicsBody : public ModelObject {
    public:
        PhysicsBody(const Vector3& position, float size, bool isSphere, const Vector4& color)
            : size(size), isSphere(isSphere) {
            transform.translate = position;
            transform.scale = { size, size, size };
            SetColor(color);
        }
        void Initialize() override {
            ModelManager* models = ModelManager::GetInstance();
            SetModel(isSphere ? models->GetSphereModel() : models->GetCubeModel());
            AddCollider(isSphere ? Collider::MakeSphere(size * 0.5f) : Collider::MakeBox({ size * 0.5f, size * 0.5f, size * 0.5f }));
            AddRigidbody();
        }

    private:
        float size = 1.0f;
        bool isSphere = true;
    };

    /// <summary>
    /// 矢印キーで動き、[Space] でジャンプする球。地面に乗っている間は色が明るくなる
    /// </summary>
    class PhysicsPlayer : public PhysicsBody {
    public:
        explicit PhysicsPlayer(const Vector3& position) : PhysicsBody(position, 1.0f, true, kPlayerColor) {}
        void Initialize() override {
            PhysicsBody::Initialize();
            SetTag(kPlayerTag);
            SetName(kPlayerTag);
            // 横の速さはキーで直接決めるので摩擦は要らない。摩擦があると、壁に向かってキーを押している間
            // 「押し付け × 摩擦」が重力に勝って壁に張り付き、落ちなくなる
            GetRigidbody()->SetFriction(0.0f);
            // 段差の角で斜め上に押されて、ジャンプせずに浮き上がらないようにする
            GetRigidbody()->SetStepHeight(kPlayerStepHeight);
        }
        void Update() override {
            Input* input = Framework::GetInstance()->GetInput();
            Rigidbody* body = GetRigidbody();
            float moveX = 0.0f;
            float moveZ = 0.0f;
            if (input->PushKey(DIK_LEFT)) { moveX -= 1.0f; }
            if (input->PushKey(DIK_RIGHT)) { moveX += 1.0f; }
            if (input->PushKey(DIK_UP)) { moveZ += 1.0f; }
            if (input->PushKey(DIK_DOWN)) { moveZ -= 1.0f; }
            // 横の速さはキーで直接決める（縦は重力とジャンプに任せる）
            body->velocity.x = moveX * kPlayerSpeed;
            body->velocity.z = moveZ * kPlayerSpeed;
            if (input->TriggerKey(DIK_SPACE) && body->IsGrounded()) {
                body->velocity.y = kJumpSpeed;
            }
            SetColor(body->IsGrounded() ? kPlayerGroundedColor : kPlayerColor);
            // 当たっている物の記録は、このフレームの当たり判定の通知（Update の後に来る）で入れ直す
            contacts.clear();
        }
        void OnCollisionEnter(const CollisionEvent& event) override { RecordContact(event); }
        void OnCollisionStay(const CollisionEvent& event) override { RecordContact(event); }

        // このフレームに当たっている物（名前・押し戻しの向き・めり込み）を文字にする（原因調査の表示用）
        std::string BuildContactText() const {
            if (contacts.empty()) {
                return "当たっている物: なし";
            }
            // 行が増えすぎて画面をふさがないよう、表示は数件まで
            constexpr size_t kMaxShown = 3;
            std::string text = "当たっている物:";
            for (size_t i = 0; i < contacts.size() && i < kMaxShown; ++i) {
                const ContactInfo& contact = contacts[i];
                text += std::format("\n  {} ({:.2f}, {:.2f}, {:.2f}) {:.3f}",
                                    contact.name, contact.normal.x, contact.normal.y, contact.normal.z, contact.depth);
            }
            if (contacts.size() > kMaxShown) {
                text += std::format("\n  ほか {} 件", contacts.size() - kMaxShown);
            }
            return text;
        }

    private:
        struct ContactInfo {
            std::string name;
            Vector3 normal;
            float depth = 0.0f;
        };
        void RecordContact(const CollisionEvent& event) {
            contacts.push_back({ event.other ? event.other->GetName() : "(消えた物)", event.normal, event.depth });
        }
        std::vector<ContactInfo> contacts;
    };

    /// <summary>
    /// 左右に行き来する動く床（キネマティック。速度どおりに動き、乗った物を押す・摩擦で運ぶ）
    /// </summary>
    class MovingPlatform : public PhysicsBody {
    public:
        explicit MovingPlatform(const Vector3& position)
            : PhysicsBody(position, 1.0f, false, kPlatformColor), centerX(position.x) {}
        void Initialize() override {
            SetModel(ModelManager::GetInstance()->GetCubeModel());
            transform.scale = { 3.0f, 0.4f, 3.0f };
            AddCollider(Collider::MakeBox({ 1.5f, 0.2f, 1.5f }));
            Rigidbody* body = AddRigidbody();
            body->SetKinematic(true);
            body->velocity = { kPlatformSpeed, 0.0f, 0.0f };
        }
        void Update() override {
            Rigidbody* body = GetRigidbody();
            // 端まで来たら折り返す（位置は物理が velocity で動かす）
            if (transform.translate.x > centerX + kPlatformRange) { body->velocity.x = -kPlatformSpeed; }
            if (transform.translate.x < centerX - kPlatformRange) { body->velocity.x = kPlatformSpeed; }
        }

    private:
        float centerX = 0.0f;
    };
}

void PhysicsTestScene::Initialize() {
    SetupCamera();
    InitializeTexts();
    SpawnAll();
}

void PhysicsTestScene::SetupCamera() {
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

void PhysicsTestScene::InitializeTexts() {
    SpriteCommon* spriteCommon = Framework::GetInstance()->GetSpriteCommon();

    titleText = std::make_unique<Text>();
    titleText->Initialize(spriteCommon, 24.0f, L"Yu Gothic UI", true);
    titleText->SetText("物理テスト (PHYSICS_TEST)");
    titleText->SetLayer(kUiLayer);

    helpText = std::make_unique<Text>();
    helpText->Initialize(spriteCommon, 18.0f);
    helpText->SetText("[←↑↓→] 移動  [Space] ジャンプ  [R] 並べ直す  [T] スロー  [I] 補間  [H] 説明  [G] 線  [J] レイ  [M] 黒帯");
    helpText->SetLayer(kUiLayer);

    detailText = std::make_unique<Text>();
    detailText->Initialize(spriteCommon, 18.0f);
    detailText->SetText(
        "左奥: 跳ね返り 0 / 0.5 / 0.9 のボール\n"
        "中央: 軽い箱(質量1) と 重い箱(質量10)\n"
        "右: 坂を滑るボール。坂を登ると奥の台に乗れる\n"
        "手前: 動く床と、その上の箱\n"
        "左手前: 段差 0.25（歩いて乗れる）/ 0.45（ジャンプで乗る）\n"
        "補間はスロー中に ON/OFF するとカクつきの差が分かる");
    detailText->SetAnchorPoint({ 0.0f, 1.0f });
    detailText->SetLayer(kUiLayer);

    statusText = std::make_unique<Text>();
    statusText->Initialize(spriteCommon, 18.0f, L"MS Gothic");
    statusText->SetAnchorPoint({ 1.0f, 0.0f });
    statusText->SetLayer(kUiLayer);

    LayoutTexts();
}

void PhysicsTestScene::LayoutTexts() {
    // 画面いっぱいモードではゲーム座標の幅が変わるので、端に付ける文字は毎フレーム置き直す
    titleText->SetPosition(Screen::GetAnchoredPosition({ 0.0f, 0.0f }, { 16.0f, 12.0f }));
    helpText->SetPosition(Screen::GetAnchoredPosition({ 0.0f, 0.0f }, { 16.0f, 46.0f }));
    detailText->SetPosition(Screen::GetAnchoredPosition({ 0.0f, 1.0f }, { 16.0f, -12.0f }));
    statusText->SetPosition(Screen::GetAnchoredPosition({ 1.0f, 0.0f }, { -16.0f, 12.0f }));
}

void PhysicsTestScene::SpawnAll() {
    objects.Clear();
    const Vector3 noRotate = { 0.0f, 0.0f, 0.0f };

    // 床（上面が y = 0）と囲いの壁（名前は「当たっている物」の表示に出る）
    objects.Create<StaticBlock>(Vector3{ 0.0f, -0.5f, 0.0f }, Vector3{ 24.0f, 1.0f, 18.0f }, noRotate, kFloorColor)->SetName("Floor");
    objects.Create<StaticBlock>(Vector3{ 0.0f, 1.0f, 9.5f }, Vector3{ 24.0f, 2.0f, 1.0f }, noRotate, kWallColor)->SetName("Wall(back)");
    objects.Create<StaticBlock>(Vector3{ 0.0f, 1.0f, -9.5f }, Vector3{ 24.0f, 2.0f, 1.0f }, noRotate, kWallColor)->SetName("Wall(front)");
    objects.Create<StaticBlock>(Vector3{ 12.5f, 1.0f, 0.0f }, Vector3{ 1.0f, 2.0f, 18.0f }, noRotate, kWallColor)->SetName("Wall(right)");
    objects.Create<StaticBlock>(Vector3{ -12.5f, 1.0f, 0.0f }, Vector3{ 1.0f, 2.0f, 18.0f }, noRotate, kWallColor)->SetName("Wall(left)");

    // 跳ね返り係数 0 / 0.5 / 0.9 のボール（同じ高さから落とす）
    const float restitutions[] = { 0.0f, 0.5f, 0.9f };
    for (int i = 0; i < 3; ++i) {
        const float value = restitutions[i];
        PhysicsBody* ball = objects.Create<PhysicsBody>(Vector3{ -9.0f + i * 2.0f, 6.0f, 6.0f }, 1.0f, true,
                                                         Vector4{ 1.0f, 0.3f + value * 0.7f, 0.3f, 1.0f });
        ball->SetName(std::format("Ball e={:.1f}", value));
        ball->GetRigidbody()->SetRestitution(value);
    }

    // 軽い箱と重い箱（プレイヤーで押し比べる）
    PhysicsBody* lightBox = objects.Create<PhysicsBody>(Vector3{ -1.5f, 0.6f, 2.0f }, 1.2f, false, kLightBoxColor);
    lightBox->SetName("Light box (mass 1)");
    PhysicsBody* heavyBox = objects.Create<PhysicsBody>(Vector3{ 1.5f, 0.6f, 2.0f }, 1.2f, false, kHeavyBoxColor);
    heavyBox->SetName("Heavy box (mass 10)");
    heavyBox->GetRigidbody()->SetMass(10.0f);

    // 坂（X 軸まわりに傾けた厚い箱。奥が高い）と、坂の上の台。その上から落とす摩擦の小さいボール。
    // 坂の下に隙間を作らないこと（床と坂の間の狭い隙間に入ると、動かない物2つに挟まれて押し戻し先が無くなり、
    // 薄い坂を突き抜けて上に飛び出す。collisionLog.txt 参照）。厚い箱の下側は床の下に埋めてある
    objects.Create<StaticBlock>(Vector3{ 8.0f, -0.34f, 4.0f }, Vector3{ 4.0f, 4.0f, 9.0f }, Vector3{ -0.35f, 0.0f, 0.0f }, kRampColor)->SetName("Ramp");
    // 台の上面は坂の上端の高さ（-0.34 + 2cos0.35 + 4.5sin0.35 ≒ 3.08）にそろえる（ずれると頂上に小さな段差ができる）
    objects.Create<StaticBlock>(Vector3{ 8.0f, 1.54f, 8.1f }, Vector3{ 4.0f, 3.08f, 1.8f }, noRotate, kRampColor)->SetName("Plateau");
    PhysicsBody* slider = objects.Create<PhysicsBody>(Vector3{ 8.0f, 4.5f, 4.0f }, 0.8f, true, Vector4{ 0.9f, 0.9f, 0.3f, 1.0f });
    slider->SetName("Slider");
    slider->GetRigidbody()->SetFriction(0.05f);

    // 段差（プレイヤーの乗れる高さ 0.3 より低い物と高い物）
    objects.Create<StaticBlock>(Vector3{ -7.0f, 0.125f, -3.0f }, Vector3{ 3.0f, 0.25f, 2.0f }, noRotate, kRampColor)->SetName("Step(0.25)");
    objects.Create<StaticBlock>(Vector3{ -7.0f, 0.225f, -6.5f }, Vector3{ 3.0f, 0.45f, 2.0f }, noRotate, kRampColor)->SetName("Step(0.45)");

    // 動く床と、その上に乗せた箱
    objects.Create<MovingPlatform>(Vector3{ 0.0f, 0.4f, -5.0f });
    PhysicsBody* rider = objects.Create<PhysicsBody>(Vector3{ 0.0f, 1.5f, -5.0f }, 0.8f, false, Vector4{ 0.8f, 0.4f, 0.8f, 1.0f });
    rider->SetName("Rider");

    objects.Create<PhysicsPlayer>(Vector3{ 0.0f, 1.0f, -1.0f });

    SetInterpolationForAll(isInterpolationEnabled);
}

void PhysicsTestScene::Finalize() {
    objects.Clear();
    Time::SetTimeScale(1.0f); // スローのまま次のシーンへ行かないように
    Screen::SetScaleMode(Screen::ScaleMode::kLetterbox); // [M] で変えた表示モードを戻す（MyGame の既定）
    // 登録したカメラを消す（次のシーンに残さない）
    CameraManager::GetInstance()->Initialize();
}

void PhysicsTestScene::Update() {
    UpdateInput();
    objects.Update();
    camera->Update();
    if (isDebugLineVisible) {
        AddDebugLines();
    }

    statusText->SetText(BuildStatusText());
    LayoutTexts();
    titleText->Update();
    helpText->Update();
    detailText->Update();
    statusText->Update();
}

void PhysicsTestScene::UpdateInput() {
    Input* input = Framework::GetInstance()->GetInput();
    if (input->TriggerKey(DIK_R)) {
        SpawnAll();
    }
    if (input->TriggerKey(DIK_T)) {
        isSlowMotion = !isSlowMotion;
        Time::SetTimeScale(isSlowMotion ? kSlowTimeScale : 1.0f);
    }
    if (input->TriggerKey(DIK_I)) {
        isInterpolationEnabled = !isInterpolationEnabled;
        SetInterpolationForAll(isInterpolationEnabled);
    }
    if (input->TriggerKey(DIK_H)) {
        isDetailVisible = !isDetailVisible;
    }
    if (input->TriggerKey(DIK_G)) {
        isDebugLineVisible = !isDebugLineVisible;
    }
    // [M] 黒帯 / 画面いっぱい（2D の線がゲームの絵の端に沿うかの確認用）
    if (input->TriggerKey(DIK_M)) {
        Screen::SetScaleMode(Screen::GetScaleMode() == Screen::ScaleMode::kLetterbox ? Screen::ScaleMode::kExpand
                                                                                     : Screen::ScaleMode::kLetterbox);
    }
    // [J] プレイヤーから右へレイを撃ち、2秒間残す（duration の確認）
    if (input->TriggerKey(DIK_J)) {
        if (const PhysicsPlayer* player = objects.FindByType<PhysicsPlayer>()) {
            const Ray ray = { player->transform.translate + Vector3{ 0.6f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
            RaycastHit hit{};
            if (objects.GetCollisionManager().Raycast(ray, kDebugRayLength, 0xFFFFFFFF, &hit)) {
                DebugDraw::Line(ray.origin, hit.hitPoint, DebugDraw::kMagenta, kShotRaySeconds);
                DebugDraw::Arrow(hit.hitPoint, hit.hitPoint + hit.normal, DebugDraw::kCyan, kShotRaySeconds);
            } else {
                DebugDraw::Ray(ray, kDebugRayLength, DebugDraw::kMagenta, kShotRaySeconds);
            }
        }
    }
}

void PhysicsTestScene::AddDebugLines() {
    // 床の上面（y = 0）より少し上に格子と原点の軸
    DebugDraw::Grid({ 0.0f, 0.01f, 0.0f }, 24.0f, 24);
    DebugDraw::Axes(Calculation::MakeIdentity4x4(), 2.0f);

    if (const PhysicsPlayer* player = objects.FindByType<PhysicsPlayer>()) {
        const Vector3& position = player->transform.translate;
        const Rigidbody* body = player->GetRigidbody();
        // 当たり判定の球（物に隠れる）と、速度・地面の向きの矢印（隠れない）
        DebugDraw::Sphere({ position, 0.5f }, DebugDraw::kGreen);
        DebugDraw::Arrow(position, position + body->velocity * kVelocityArrowScale, DebugDraw::kYellow, 0.0f, false);
        if (body->IsGrounded()) {
            DebugDraw::Arrow(position, position + body->GetGroundNormal(), DebugDraw::kCyan, 0.0f, false);
        }
        // 足元から真下へのレイ。当たった所に十字
        const Ray down = { position - Vector3{ 0.0f, 0.51f, 0.0f }, { 0.0f, -1.0f, 0.0f } };
        RaycastHit hit{};
        if (objects.GetCollisionManager().Raycast(down, kDebugRayLength, 0xFFFFFFFF, &hit)) {
            DebugDraw::Line(down.origin, hit.hitPoint, DebugDraw::kRed, 0.0f, false);
            DebugDraw::Cross(hit.hitPoint, 0.4f, DebugDraw::kRed, 0.0f, false);
        }
    }

    // 2D: ゲーム画面の枠（画面に固定）。黒帯や画面いっぱいモードでも、ゲームの絵の端に沿うこと
    const Vector2 gameSize = Screen::GetGameSize();
    DebugDraw::Rect2D({ { 4.0f, 4.0f }, { gameSize.x - 4.0f, gameSize.y - 4.0f } }, DebugDraw::kMagenta, 0.0f, false);
    DebugDraw::Circle2D({ { gameSize.x * 0.5f, gameSize.y * 0.5f }, 12.0f }, DebugDraw::kWhite, 0.0f, false);
}

void PhysicsTestScene::SetInterpolationForAll(bool enable) {
    for (GameObject* object : objects.FindAllByType<GameObject>()) {
        if (Rigidbody* body = object->GetRigidbody()) {
            body->SetInterpolate(enable);
        }
    }
}

void PhysicsTestScene::Draw() {
    // --- シャドウパス（ライト視点の深度のみ）。通常の Draw と同じく補間した位置で描く ---
    objects.BeginRenderInterpolation();
    DrawShadowPass();
    objects.EndRenderInterpolation();
    RebindMainRenderTarget();

    // --- 通常の描画パス ---
    Framework::GetInstance()->GetObject3dCommon()->PreDraw();
    objects.Draw();

    titleText->Draw();
    helpText->Draw();
    if (isDetailVisible) {
        detailText->Draw();
    }
    statusText->Draw();
}

void PhysicsTestScene::DrawShadowPass() {
    Object3dCommon* object3dCommon = Framework::GetInstance()->GetObject3dCommon();
    object3dCommon->PreDrawShadow();
    // 有効な物すべての影（ModelObject は同じモデルどうしまとめて描かれる）
    objects.DrawShadow(object3dCommon->GetLightViewProjection());
    object3dCommon->PostDrawShadow();
}

void PhysicsTestScene::RebindMainRenderTarget() {
    // PostDrawShadow() はビューポート/シザーしか戻さないので、描画先（RTV/DSV）はここで張り直す
    // （内部解像度を下げているときは小さい描画先になるので、バックバッファを直接指定しない）
    Framework::GetInstance()->GetDxCommon()->BindMainRenderTarget();
}

std::string PhysicsTestScene::BuildStatusText() const {
    const PhysicsPlayer* player = objects.FindByType<PhysicsPlayer>();
    const Rigidbody* body = player ? player->GetRigidbody() : nullptr;
    if (!body) {
        return "";
    }
    const Vector3& position = player->transform.translate;
    const Vector3& velocity = body->velocity;
    const Vector3& ground = body->GetGroundNormal();
    return std::format(
        "接地 {}  面 ({:.2f}, {:.2f}, {:.2f})\n"
        "位置 ({:.2f}, {:.2f}, {:.2f})\n"
        "速度 ({:.2f}, {:.2f}, {:.2f})\n"
        "物理 {}回/フレーム  スロー {}  補間 {}\n"
        "{}",
        body->IsGrounded() ? "○" : "×", ground.x, ground.y, ground.z,
        position.x, position.y, position.z,
        velocity.x, velocity.y, velocity.z,
        objects.GetLastPhysicsStepCount(),
        isSlowMotion ? "ON" : "OFF", isInterpolationEnabled ? "ON" : "OFF",
        player->BuildContactText());
}
