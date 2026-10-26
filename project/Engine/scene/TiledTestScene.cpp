#include "TiledTestScene.h"
#include "Framework.h"
#include "GameTime.h"
#include "Input.h"
#include "LevelData.h"
#include "Screen.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include <algorithm>
#include <format>

namespace {
    const char* kMapPath = "Resources/maps/test_map.tmj";
    const char* kPlayerTag = "Player";
    constexpr int kUiLayer = 100;
    // Tiled のレイヤーの順番（0 背景 / 1 地面 / 2 物 / 3 前景）。プレイヤーは物のレイヤーに描く
    constexpr int kObjectLayer = 2;
    const Vector3 kGravity = { 0.0f, 1500.0f, 0.0f }; // 2D は下が +y（ピクセル / 秒²）
    constexpr float kPlayerSpeed = 260.0f;
    constexpr float kJumpSpeed = 620.0f;
    constexpr float kFallResetMargin = 200.0f;          // マップの下からこれだけ落ちたら最初の位置に戻す
    constexpr float kCoinRadius = 12.0f;
    const Vector4 kPlayerColor = { 0.2f, 0.45f, 1.0f, 1.0f };

    /// <summary>
    /// 矢印キーで動き、[Space] でジャンプするプレイヤー（Tiled の Class "Player" の四角）。大きさは Tiled の四角の大きさ
    /// </summary>
    class TiledTestPlayer : public GameObject {
    public:
        void Initialize() override {
            SetTag(kPlayerTag);
        }
        void OnLevelLoad(const LevelObjectData& data) override {
            startPosition = transform.translate;
            const Vector2 size = { static_cast<float>(data.raw["width"].AsNumber()), static_cast<float>(data.raw["height"].AsNumber()) };
            sprite = std::make_unique<Sprite>();
            sprite->Initialize(Framework::GetInstance()->GetSpriteCommon(), "white");
            sprite->SetSize(size);
            sprite->SetAnchorPoint({ 0.5f, 0.5f });
            sprite->SetColor(kPlayerColor);
            sprite->SetUseCamera(true);
            sprite->SetLayer(kObjectLayer);
            AddCollider(Collider::MakeRect(size));
            Rigidbody* body = AddRigidbody();
            // 横の速さはキーで直接決めるので摩擦は要らない（壁に押し付けても張り付かないように）
            body->SetFriction(0.0f);
        }
        void Update() override {
            Input* input = Framework::GetInstance()->GetInput();
            Rigidbody* body = GetRigidbody();
            if (!body) {
                return;
            }
            float move = 0.0f;
            if (input->PushKey(DIK_LEFT)) { move -= 1.0f; }
            if (input->PushKey(DIK_RIGHT)) { move += 1.0f; }
            body->velocity.x = move * kPlayerSpeed;
            if ((input->TriggerKey(DIK_SPACE) || input->TriggerKey(DIK_UP)) && body->IsGrounded()) {
                body->velocity.y = -kJumpSpeed;
            }
            // 穴に落ちたら最初の位置へ
            if (transform.translate.y > mapBottom + kFallResetMargin) {
                transform.translate = startPosition;
                body->velocity = { 0.0f, 0.0f, 0.0f };
            }
        }
        void Draw() override {
            if (sprite) {
                sprite->SetPosition({ transform.translate.x, transform.translate.y });
                sprite->Update();
                sprite->Draw();
            }
        }
        void AddScore(int value) { score += value; }
        int GetScore() const { return score; }
        void SetMapBottom(float bottom) { mapBottom = bottom; }

    private:
        std::unique_ptr<Sprite> sprite;
        Vector3 startPosition = { 0.0f, 0.0f, 0.0f };
        float mapBottom = 0.0f;
        int score = 0;
    };

    /// <summary>
    /// 取ると消えるコイン（タイルセットでタイルに Class "Coin" を付けた物、または Class が "Coin" の物）。
    /// 見た目は TiledTileObject（タイルの絵）。"score" はタイルセットの値を引き継ぎ、物に書けば上書きできる
    /// </summary>
    class TiledTestCoin : public TiledTileObject {
    public:
        void OnLevelLoad(const LevelObjectData& data) override {
            TiledTileObject::OnLevelLoad(data);
            if (data.raw["score"].IsNumber()) {
                score = static_cast<int>(data.raw["score"].AsNumber());
            }
            AddCollider(Collider::MakeCircle(kCoinRadius))->SetType(ColliderType::kTrigger);
        }
        void OnCollisionEnter(const CollisionEvent& event) override {
            if (event.other && event.other->HasTag(kPlayerTag)) {
                static_cast<TiledTestPlayer*>(event.other)->AddScore(score);
                Destroy();
            }
        }

    private:
        int score = 1;
    };

    /// <summary>
    /// ゴール（Class "Goal" の四角。"collider": "trigger" で当たり判定が付く）。プレイヤーが入ったら到着
    /// </summary>
    class TiledTestGoal : public GameObject {
    public:
        void OnCollisionEnter(const CollisionEvent& event) override {
            if (event.other && event.other->HasTag(kPlayerTag)) {
                isReached = true;
            }
        }
        bool IsReached() const { return isReached; }

    private:
        bool isReached = false;
    };
}

void TiledTestScene::Initialize() {
    InitializeTexts();

    objects.SetGravity(kGravity);
    spawner.RegisterType<TiledTestPlayer>("Player");
    spawner.RegisterType<TiledTestCoin>("Coin");
    spawner.RegisterType<TiledTestGoal>("Goal");
    spawner.LoadAndSpawn(objects, kMapPath);
    tileMap.Initialize(spawner.GetMap());

    Framework::GetInstance()->GetSpriteCommon()->SetCamera2D(&camera);
    UpdateCamera();
}

void TiledTestScene::InitializeTexts() {
    SpriteCommon* spriteCommon = Framework::GetInstance()->GetSpriteCommon();

    titleText = std::make_unique<Text>();
    titleText->Initialize(spriteCommon, 32.0f, L"Yu Gothic UI", true);
    titleText->SetText("Tiled マップ テスト (TILED_TEST)");
    titleText->SetPosition({ 20.0f, 16.0f });
    titleText->SetLayer(kUiLayer);

    helpText = std::make_unique<Text>();
    helpText->Initialize(spriteCommon, 20.0f);
    helpText->SetText("[←][→] 移動  [Space] ジャンプ  [F5] マップを読み直す\n"
                      "前景の草むらはプレイヤーより手前（半透明）/ 旗はクラス未登録のタイルの物");
    helpText->SetPosition({ 20.0f, 60.0f });
    helpText->SetLayer(kUiLayer);

    statusText = std::make_unique<Text>();
    statusText->Initialize(spriteCommon, 20.0f, L"MS Gothic");
    statusText->SetPosition({ 20.0f, 120.0f });
    statusText->SetLayer(kUiLayer);
}

void TiledTestScene::Finalize() {
    // カメラはこのシーンのメンバーなので、次のシーンに残さない
    Framework::GetInstance()->GetSpriteCommon()->SetCamera2D(nullptr);
    objects.Clear();
}

void TiledTestScene::Update() {
    if (Framework::GetInstance()->GetInput()->TriggerKey(DIK_F5)) {
        ReloadMap();
    }
    objects.Update();
    UpdateCamera();

    statusText->SetText(BuildStatusText());
    titleText->Update();
    helpText->Update();
    statusText->Update();
}

void TiledTestScene::ReloadMap() {
    std::string error;
    if (spawner.Reload(objects, &error)) {
        // 見た目も新しいマップで作り直す
        tileMap.Initialize(spawner.GetMap());
        ++reloadCount;
        reloadMessage = std::format("成功（{} 回目）", reloadCount);
    } else {
        reloadMessage = "失敗（今のまま）: " + error;
    }
}

void TiledTestScene::UpdateCamera() {
    TiledTestPlayer* player = objects.FindByType<TiledTestPlayer>();
    if (!player) {
        return;
    }
    const Vector2 mapSize = spawner.GetMap().GetPixelSize();
    // 読み直しで作り直されたプレイヤーにも、落ちたと判断する高さを毎回教える
    player->SetMapBottom(mapSize.y);
    const Vector2 gameSize = Screen::GetGameSize();
    const Vector2 half = { gameSize.x * 0.5f, gameSize.y * 0.5f };
    // マップが画面より小さい向きは真ん中に置く
    auto clampAxis = [](float value, float halfScreen, float mapLength) {
        return mapLength <= halfScreen * 2.0f ? mapLength * 0.5f : std::clamp(value, halfScreen, mapLength - halfScreen);
    };
    camera.SetPosition({ clampAxis(player->transform.translate.x, half.x, mapSize.x),
                         clampAxis(player->transform.translate.y, half.y, mapSize.y) });
}

std::string TiledTestScene::BuildStatusText() const {
    const TiledTestPlayer* player = objects.FindByType<TiledTestPlayer>();
    const TiledTestGoal* goal = objects.FindByType<TiledTestGoal>();
    size_t colliderCount = 0;
    size_t flagCount = 0;
    for (const GameObject* object : objects.FindAllByType<GameObject>()) {
        if (object->GetName() == "TileCollider") {
            ++colliderCount;
        }
        if (dynamic_cast<const TiledTileObject*>(object) && !dynamic_cast<const TiledTestCoin*>(object)) {
            ++flagCount;
        }
    }
    return std::format(
        "スコア: {}（コイン 10 点、大きいコインは 50 点）\n"
        "残りのコイン: {}\n"
        "ゴール: {}\n"
        "地面の当たり判定: {} 個（タイルをまとめた四角）\n"
        "タイルの描画命令: {} 回 / クラス未登録のタイルの物: {} 個\n"
        "読み直し: {}",
        player ? player->GetScore() : 0,
        objects.FindAllByType<TiledTestCoin>().size(),
        goal && goal->IsReached() ? "到着！" : "まだ",
        colliderCount,
        tileMap.GetDrawCallCount(), flagCount,
        reloadMessage);
}

void TiledTestScene::Draw() {
    tileMap.Draw();
    objects.Draw();
    titleText->Draw();
    helpText->Draw();
    statusText->Draw();
}
