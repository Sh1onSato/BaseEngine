#include "LevelTestScene.h"
#include "Calculation.h"
#include "Camera.h"
#include "CameraManager.h"
#include "Framework.h"
#include "GameTime.h"
#include "Input.h"
#include "LevelData.h"
#include "ModelObject.h"
#include "SpriteCommon.h"
#include <algorithm>
#include <format>

namespace {
    const char* kLevelPath = "Resources/levels/test_level.json";
    const char* kCameraName = "LevelTest";
    const Vector3 kCameraPosition = { 0.0f, 16.0f, -18.0f };
    const Vector3 kCameraRotate = { 0.75f, 0.0f, 0.0f }; // 斜めに見下ろす
    constexpr int kUiLayer = 100;

    const char* kPlayerTag = "Player";
    const char* kCoinTag = "Coin";
    constexpr float kPlayerSpeed = 5.0f;   // 1秒あたりの移動量
    constexpr float kPlayerRadius = 0.5f;
    constexpr float kCoinSpinSpeed = 3.0f; // 1秒あたりの回転（ラジアン）
    const Vector4 kPlayerColor = { 0.2f, 0.4f, 1.0f, 1.0f };
    const Vector4 kCoinColor = { 1.0f, 0.85f, 0.1f, 1.0f };

    /// <summary>
    /// 矢印キーで XZ 平面を動くプレイヤー。JSON に "class" は無く、Blender の名前 "Player" から選ばれる（名前で決める方式の確認）。
    /// 当たり判定は自分で付けるので、JSON の "collider" は使われない。
    /// </summary>
    class LevelTestPlayer : public ModelObject {
    public:
        void Initialize() override {
            SetTag(kPlayerTag);
            SetColor(kPlayerColor);
            Collider* collider = AddCollider(Collider::MakeSphere(kPlayerRadius));
            collider->SetType(ColliderType::kDynamic);
        }
        void OnLevelLoad(const LevelObjectData& /*data*/) override {
            // 重力は無いので、配置された高さを保つ
            height = transform.translate.y;
        }
        void Update() override {
            Input* input = Framework::GetInstance()->GetInput();
            Vector3 direction = { 0.0f, 0.0f, 0.0f };
            if (input->PushKey(DIK_LEFT)) { direction.x -= 1.0f; }
            if (input->PushKey(DIK_RIGHT)) { direction.x += 1.0f; }
            if (input->PushKey(DIK_UP)) { direction.z += 1.0f; }
            if (input->PushKey(DIK_DOWN)) { direction.z -= 1.0f; }
            if (Calculation::Length(direction) > 0.0f) {
                transform.translate += Calculation::Normalize(direction) * (kPlayerSpeed * Time::GetDeltaTime());
            }
            transform.translate.y = height;
        }
        void AddScore(int amount) { score += amount; }
        int GetScore() const { return score; }

    private:
        float height = 0.0f;
        int score = 0;
    };

    /// <summary>
    /// 取ると消えるコイン。JSON の "class": "Coin" で選ばれ、独自項目 "score" を OnLevelLoad で読む。
    /// JSON の "collider"（SPHERE）がそのまま付き、OnLevelLoad でトリガーに変える。
    /// </summary>
    class LevelTestCoin : public ModelObject {
    public:
        void Initialize() override {
            SetTag(kCoinTag);
            SetColor(kCoinColor);
        }
        void OnLevelLoad(const LevelObjectData& data) override {
            if (data.raw["score"].IsNumber()) {
                score = static_cast<int>(data.raw["score"].AsNumber());
            }
            if (Collider* collider = GetCollider()) {
                collider->SetType(ColliderType::kTrigger);
            }
        }
        void Update() override {
            transform.rotate.y += kCoinSpinSpeed * Time::GetDeltaTime();
        }
        void OnCollisionEnter(const CollisionEvent& event) override {
            if (event.other && event.other->HasTag(kPlayerTag)) {
                static_cast<LevelTestPlayer*>(event.other)->AddScore(score);
                Destroy();
            }
        }

    private:
        int score = 1; // "score" が無いときの値
    };
}

void LevelTestScene::Initialize() {
    SetupCamera();
    InitializeTexts();

    spawner.RegisterType<LevelTestPlayer>("Player");
    spawner.RegisterType<LevelTestCoin>("Coin");
    spawnedCount = spawner.LoadAndSpawn(objects, kLevelPath).size();
}

void LevelTestScene::SetupCamera() {
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

void LevelTestScene::InitializeTexts() {
    SpriteCommon* spriteCommon = Framework::GetInstance()->GetSpriteCommon();

    titleText = std::make_unique<Text>();
    titleText->Initialize(spriteCommon, 32.0f, L"Yu Gothic UI", true);
    titleText->SetText("レベルデータ テスト (LEVEL_TEST)");
    titleText->SetPosition({ 20.0f, 16.0f });
    titleText->SetLayer(kUiLayer);

    helpText = std::make_unique<Text>();
    helpText->Initialize(spriteCommon, 20.0f);
    helpText->SetText(
        "[←↑↓→] 移動  [F5] JSON を読み直す  [B] まとめ描き ON / OFF\n"
        "黄=コイン(class) 青=プレイヤー(名前で選択)\n"
        "柱2本=親子 / 白い立方体=モデルが見つからない物\n"
        "ログ（出力ウィンドウ）に LevelSpawner の警告が出る");
    helpText->SetPosition({ 20.0f, 64.0f });
    helpText->SetLayer(kUiLayer);

    statusText = std::make_unique<Text>();
    statusText->Initialize(spriteCommon, 22.0f, L"MS Gothic");
    statusText->SetPosition({ 20.0f, 180.0f });
    statusText->SetLayer(kUiLayer);
}

void LevelTestScene::Finalize() {
    objects.Clear();
    // 登録したカメラを消す（次のシーンに残さない）
    CameraManager::GetInstance()->Initialize();
}

void LevelTestScene::Update() {
    if (Framework::GetInstance()->GetInput()->TriggerKey(DIK_F5)) {
        ReloadLevel();
    }
    // [B] ModelObject の自動まとめ描き ON / OFF（見た目が変わらないことの確認用）
    if (Framework::GetInstance()->GetInput()->TriggerKey(DIK_B)) {
        objects.SetAutoBatching(!objects.IsAutoBatching());
    }

    objects.Update();
    if (camera) {
        camera->Update(); // 画面の縦横比が変わっても追従するよう毎フレーム計算する
    }

    statusText->SetText(BuildStatusText());
    titleText->Update();
    helpText->Update();
    statusText->Update();
}

void LevelTestScene::ReloadLevel() {
    std::string error;
    if (spawner.Reload(objects, &error)) {
        ++reloadCount;
        reloadMessage = std::format("成功（{} 回目）", reloadCount);
        // 作り直した物はまだ数に入っていない分もあるので、Reload 後の生存数で数え直す
        spawnedCount = 0;
        for (GameObject* object : objects.FindAllByType<GameObject>()) {
            if (object->IsFromLevel() && !object->IsDestroyed()) {
                ++spawnedCount;
            }
        }
    } else {
        reloadMessage = "失敗（今の配置のまま）: " + error;
    }
}

void LevelTestScene::Draw() {
    // --- シャドウパス（ライト視点の深度のみ） ---
    DrawShadowPass();
    RebindMainRenderTarget();

    // --- 通常の描画パス ---
    Framework::GetInstance()->GetObject3dCommon()->PreDraw();
    objects.Draw();

    titleText->Draw();
    helpText->Draw();
    statusText->Draw();
}

void LevelTestScene::DrawShadowPass() {
    Object3dCommon* object3dCommon = Framework::GetInstance()->GetObject3dCommon();
    object3dCommon->PreDrawShadow();
    // 有効な物すべての影（ModelObject は同じモデルどうしまとめて描かれる）
    objects.DrawShadow(object3dCommon->GetLightViewProjection());
    object3dCommon->PostDrawShadow();
}

void LevelTestScene::RebindMainRenderTarget() {
    // PostDrawShadow() はビューポート/シザーしか戻さないので、描画先（RTV/DSV）はここで張り直す
    // （内部解像度を下げているときは小さい描画先になるので、バックバッファを直接指定しない）
    Framework::GetInstance()->GetDxCommon()->BindMainRenderTarget();
}

std::string LevelTestScene::BuildStatusText() const {
    const LevelTestPlayer* player = objects.FindByType<LevelTestPlayer>();
    const GameObject* hidden = objects.Find("Hidden");
    const GameObject* camera3d = objects.Find("Camera");
    return std::format(
        "配置から作った物: {}\n"
        "残りのコイン: {}\n"
        "スコア: {}\n"
        "disabled の物(Hidden): {}\n"
        "CAMERA(Camera): {}\n"
        "読み直し: {}\n"
        "まとめ描き: {}",
        spawnedCount,
        objects.FindAllByTag(kCoinTag).size(),
        player ? player->GetScore() : 0,
        hidden ? "作られた(NG)" : "作られていない(OK)",
        camera3d ? "作られた(NG)" : "作られていない(OK)",
        reloadMessage,
        objects.IsAutoBatching()
            ? std::format("ON（{} 個を {} 回の描画命令で）", objects.GetModelBatcher().GetLastObjectCount(),
                          objects.GetModelBatcher().GetLastDrawCallCount())
            : std::string("OFF（1個ずつ描く）"));
}
