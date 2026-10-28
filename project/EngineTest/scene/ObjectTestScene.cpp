#include "ObjectTestScene.h"
#include "CameraManager.h"
#include "Framework.h"
#include "GameTime.h"
#include "Input.h"
#include "ObjectTestObjects.h"
#include "SceneManager.h"
#include "Screen.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "TextureManager.h"
#ifdef _DEBUG
#include "externals/imgui/imgui.h"
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <random>

namespace {
    constexpr size_t kMaxObjects = 300;         // 増えすぎ防止（子が子を出しても上限で止まる）
    constexpr int kSpawnManyCount = 20;         // [Enter]で一度に出す数
    constexpr int kSpecialInterval = 10;        // 10個に1個を "Special" にする
    constexpr float kBoxSize = 40.0f;
    constexpr float kSpecialBoxSize = 80.0f;
    constexpr float kChildBoxSize = 20.0f;
    constexpr float kMinSpeed = 80.0f;          // 1秒あたりのピクセル数
    constexpr float kMaxSpeed = 300.0f;
    constexpr float kLifeTime = 3.0f;           // この秒数で消える
    constexpr float kFadeTime = 1.0f;           // 最後のこの秒数でだんだん透明になる
    constexpr float kChildSpawnChance = 0.2f;   // 跳ね返ったときに子を出す確率
    constexpr int kUiLayer = 100;               // 文字は四角より手前
    const char* kBoxTag = "Box";
    const char* kSpecialTag = "Special";

    // --- 当たり判定テストの配置（1280x720 のゲーム座標を想定。左側は文字の表示に空けておく） ---
    constexpr float kPlayLeft = 440.0f;         // 遊べる範囲の左端
    constexpr float kPlayMargin = 20.0f;        // 上・右・下の画面端からの余白
    constexpr float kWallThickness = 20.0f;
    constexpr float kCrateSize = 56.0f;
    const Vector2 kPlayerStart = { 520.0f, 100.0f };
    const Vector2 kEnemyStart = { 1000.0f, 260.0f };
    constexpr float kEnemyMinX = 760.0f;        // 敵が行き来する範囲
    constexpr float kEnemyMaxX = 1200.0f;
    const std::array<Vector2, 8> kCoinPositions = { {
        { 520.0f, 400.0f }, { 520.0f, 640.0f }, { 660.0f, 640.0f }, { 900.0f, 630.0f },
        { 1180.0f, 640.0f }, { 1180.0f, 100.0f }, { 1000.0f, 120.0f }, { 1150.0f, 420.0f },
    } };

    // シーン読み直し（[R]）の後も同じモードで始めるために覚えておく（Mode を int にしたもの）
    int savedModeIndex = 0;
    // ImGui でレイヤー同士の当たりを切り替える範囲（0〜3）
    constexpr uint32_t kDebugLayerCount = 4;

    // 乱数（rand() は偏りがあり範囲指定も面倒なので <random> を使う）
    std::mt19937& GetRandomEngine() {
        static std::mt19937 engine{ std::random_device{}() };
        return engine;
    }

    float RandomRange(float min, float max) {
        std::uniform_real_distribution<float> distribution(min, max);
        return distribution(GetRandomEngine());
    }

    /// <summary>
    /// テスト用の四角。ランダムな速さで動き、画面の端で跳ね返り、寿命が来たら Destroy() で消える。
    /// </summary>
    class FloatingBox : public GameObject {
    public:
        FloatingBox(const Vector2& position, float size, bool isSpecial)
            : size(size), isSpecial(isSpecial) {
            transform.translate = { position.x, position.y, 0.0f };
        }

        void Initialize() override {
            SetTag(isSpecial ? kSpecialTag : kBoxTag);

            // 向きも速さもランダム
            float angle = RandomRange(0.0f, 6.2831853f);
            float speed = RandomRange(kMinSpeed, kMaxSpeed);
            velocity = { std::cos(angle) * speed, std::sin(angle) * speed };
            color = { RandomRange(0.2f, 1.0f), RandomRange(0.2f, 1.0f), RandomRange(0.2f, 1.0f), 1.0f };

            // "white" はエンジン内部で作られる1x1の白画像なので LoadTexture は不要
            sprite = std::make_unique<Sprite>();
            sprite->Initialize(Framework::GetInstance()->GetSpriteCommon(), "white");
            sprite->SetSize({ size, size });
            sprite->SetAnchorPoint({ 0.5f, 0.5f });
            // Update中に作られた物は、そのフレームは Update されずに Draw だけされるので、
            // ここで見た目（位置・色）を一度反映しておく
            ApplySprite();
        }

        void Update() override {
            const float deltaTime = Time::GetDeltaTime();
            transform.translate.x += velocity.x * deltaTime;
            transform.translate.y += velocity.y * deltaTime;
            if (Bounce()) {
                TrySpawnChild();
            }

            lifeTimer += deltaTime;
            if (lifeTimer >= kLifeTime) {
                Destroy(); // すぐには消えず、このフレームの最後にマネージャーが片付ける
            }

            // 残り kFadeTime 秒からαを 1→0 に下げる
            float remaining = kLifeTime - lifeTimer;
            color.w = (std::clamp)(remaining / kFadeTime, 0.0f, 1.0f);
            ApplySprite();
        }

        void Draw() override {
            sprite->Draw();
        }

    private:
        // transform と色をスプライトに反映する
        void ApplySprite() {
            sprite->SetColor(color);
            sprite->SetPosition({ transform.translate.x, transform.translate.y });
            sprite->Update();
        }

        // 画面の端に当たったら向きを反転する。当たったら true
        bool Bounce() {
            const Vector2 gameSize = Screen::GetGameSize();
            const float half = size * 0.5f;
            bool bounced = false;
            if (transform.translate.x < half) { transform.translate.x = half; velocity.x = std::abs(velocity.x); bounced = true; }
            if (transform.translate.x > gameSize.x - half) { transform.translate.x = gameSize.x - half; velocity.x = -std::abs(velocity.x); bounced = true; }
            if (transform.translate.y < half) { transform.translate.y = half; velocity.y = std::abs(velocity.y); bounced = true; }
            if (transform.translate.y > gameSize.y - half) { transform.translate.y = gameSize.y - half; velocity.y = -std::abs(velocity.y); bounced = true; }
            return bounced;
        }

        // 「Update中のCreate」のテスト。作った子は次のフレームから動き出す
        void TrySpawnChild() {
            if (RandomRange(0.0f, 1.0f) >= kChildSpawnChance) {
                return;
            }
            GameObjectManager* manager = GetManager();
            if (manager->GetCount() >= kMaxObjects) {
                return;
            }
            manager->Create<FloatingBox>(Vector2{ transform.translate.x, transform.translate.y }, kChildBoxSize, false);
        }

        std::unique_ptr<Sprite> sprite;
        Vector2 velocity = { 0.0f, 0.0f };
        Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
        float size = kBoxSize;
        float lifeTimer = 0.0f;
        bool isSpecial = false;
    };
}

void ObjectTestScene::Initialize() {
    InitializeTexts();
    // コイン・敵の丸い見た目に使う画像
    TextureManager::GetInstance()->LoadTexture(ObjectTest::kCircleTexturePath);
    // 最初はプレイヤーとゴーストが当たらない（すり抜ける）設定
    SetGhostCollision(false);
    // 3Dテスト用のカメラ・メッシュ（どのモードでも作っておく。カメラは CameraManager のアクティブになる）
    collision3D.Initialize();
    ChangeMode(static_cast<Mode>(savedModeIndex));
}

void ObjectTestScene::InitializeTexts() {
    SpriteCommon* spriteCommon = Framework::GetInstance()->GetSpriteCommon();

    titleText = std::make_unique<Text>();
    titleText->Initialize(spriteCommon, 32.0f, L"Yu Gothic UI", true);
    titleText->SetText("GameObject テスト (OBJECT_TEST)");
    titleText->SetPosition({ 20.0f, 16.0f });
    titleText->SetLayer(kUiLayer);

    helpText = std::make_unique<Text>();
    helpText->Initialize(spriteCommon, 20.0f);
    // 文字はモードごとに ChangeMode で入れる
    helpText->SetPosition({ 20.0f, 64.0f });
    helpText->SetLayer(kUiLayer);

    statusText = std::make_unique<Text>();
    statusText->Initialize(spriteCommon, 22.0f, L"MS Gothic");
    statusText->SetPosition({ 20.0f, 210.0f });
    statusText->SetLayer(kUiLayer);
}

void ObjectTestScene::Finalize() {
    // 残っている物の Finalize を呼んで片付ける（シーン読み直しで前の物が残らないこと）
    objects.Clear();
    // 3Dテストで登録したカメラを消す（次のシーンに残さない。SkinningTestScene と同じ）
    CameraManager::GetInstance()->Initialize();
}

void ObjectTestScene::Update() {
    UpdateInput(Framework::GetInstance()->GetInput());

    // 全員の Update → (当たり判定) → 新しい物の追加 → Destroy された物の削除
    objects.Update();
    if (mode == Mode::kCollision3D) {
        // レイキャストは押し戻しが終わった後の位置で行う
        collision3D.Update(objects);
    }

    UpdateFps();
    statusText->SetText(mode == Mode::kSpawn ? BuildStatusText() : BuildCollisionStatusText());

#ifdef _DEBUG
    UpdateDebugUI();
#endif

    titleText->Update();
    helpText->Update();
    statusText->Update();
}

void ObjectTestScene::Draw() {
    if (mode == Mode::kCollision3D) {
        // 3D: 影 → 通常描画の順で Object3d を描く（objects.Draw() もこの中で呼ぶ）
        collision3D.Draw(objects);
    } else {
        // 2D: Drawは描画キューに積むだけ。実際の描画はSceneManagerがシーンのDraw後にレイヤー順に行う
        objects.Draw();
    }

    titleText->Draw();
    helpText->Draw();
    statusText->Draw();
}

void ObjectTestScene::UpdateInput(Input* input) {
    if (input->TriggerKey(DIK_TAB)) {
        ChangeMode(GetNextMode(mode));
    }
    if (input->TriggerKey(DIK_R)) {
        sceneManager->ChangeScene("OBJECT_TEST", 0.5f);
    }
    if (mode == Mode::kSpawn) {
        UpdateSpawnInput(input);
    } else if (mode == Mode::kCollision && input->TriggerKey(DIK_G)) {
        SetGhostCollision(!isGhostCollisionEnabled);
    } else if (mode == Mode::kCollision3D && input->TriggerKey(DIK_P)) {
        collision3D.TogglePlayerShape(objects);
    }
}

ObjectTestScene::Mode ObjectTestScene::GetNextMode(Mode current) {
    switch (current) {
    case Mode::kSpawn:     return Mode::kCollision;
    case Mode::kCollision: return Mode::kCollision3D;
    default:               return Mode::kSpawn;
    }
}

const char* ObjectTestScene::GetModeName(Mode current) {
    switch (current) {
    case Mode::kSpawn:     return "Spawn";
    case Mode::kCollision: return "Collision 2D";
    default:               return "Collision 3D";
    }
}

#ifdef _DEBUG
void ObjectTestScene::UpdateDebugUI() {
    // （ImGuiの既定フォントは日本語を表示できないので英語で書く）
    CollisionManager& collision = objects.GetCollisionManager();
    ImGui::Begin("Collision");
    ImGui::Text("Mode: %s", GetModeName(mode));
    ImGui::Text("GameObjects: %zu", objects.GetCount());
    ImGui::Text("Colliders: %zu", collision.GetLastColliderCount());
    ImGui::Text("Broadphase pairs: %zu", collision.GetLastBroadphasePairCount());
    ImGui::Text("Hit pairs: %zu", collision.GetLastHitPairCount());
    ImGui::Separator();
    ImGui::Text("Layer collision (2D: 1=player 2=ghost / 3D: 0=world 1=player 3=trigger)");
    for (uint32_t a = 0; a < kDebugLayerCount; ++a) {
        for (uint32_t b = a; b < kDebugLayerCount; ++b) {
            bool enabled = collision.IsLayerCollisionEnabled(a, b);
            std::string label = std::format("{}-{}", a, b);
            if (ImGui::Checkbox(label.c_str(), &enabled)) {
                collision.SetLayerCollision(a, b, enabled);
                // [G] キーの表示と食い違わないよう、ゴーストの組はシーンの変数にも反映する
                if (a == ObjectTest::kPlayerLayer && b == ObjectTest::kGhostLayer) {
                    isGhostCollisionEnabled = enabled;
                }
            }
            if (b + 1 < kDebugLayerCount) {
                ImGui::SameLine();
            }
        }
    }
    ImGui::End();
}
#endif

void ObjectTestScene::UpdateSpawnInput(Input* input) {
    if (input->TriggerKey(DIK_SPACE)) {
        SpawnBoxes(1);
    }
    if (input->TriggerKey(DIK_RETURN)) {
        SpawnBoxes(kSpawnManyCount);
    }
    if (input->TriggerKey(DIK_D)) {
        // ここでは印を付けるだけ。次の objects.Update() の最後で消える
        for (GameObject* object : objects.FindAllByTag(kSpecialTag)) {
            object->Destroy();
        }
    }
    if (input->TriggerKey(DIK_C)) {
        objects.Clear();
    }
}

void ObjectTestScene::ChangeMode(Mode newMode) {
    mode = newMode;
    savedModeIndex = static_cast<int>(mode);
    // 前のモードの物は全部片付ける（Update の外なのですぐに消える）
    objects.Clear();
    score = 0;
    if (mode == Mode::kCollision) {
        SpawnCollisionTest();
    } else if (mode == Mode::kCollision3D) {
        collision3D.Spawn(objects);
    }
    helpText->SetText(BuildHelpText());
}

void ObjectTestScene::SpawnCollisionTest() {
    SpawnWalls();
    SpawnCoins();
    objects.Create<ObjectTest::Crate>(Vector2{ 760.0f, 140.0f }, kCrateSize);
    objects.Create<ObjectTest::Crate>(Vector2{ 880.0f, 140.0f }, kCrateSize);
    objects.Create<ObjectTest::Ghost>(Vector2{ 640.0f, 440.0f }, Vector2{ 120.0f, 120.0f });
    objects.Create<ObjectTest::Enemy>(kEnemyStart, kEnemyMinX, kEnemyMaxX);
    objects.Create<ObjectTest::Player>(kPlayerStart);
}

void ObjectTestScene::SpawnWalls() {
    // 外周（遊べる範囲を四方から囲む）
    const Vector2 gameSize = Screen::GetGameSize();
    const float left = kPlayLeft;
    const float right = gameSize.x - kPlayMargin;
    const float top = kPlayMargin;
    const float bottom = gameSize.y - kPlayMargin;
    const float centerX = (left + right) * 0.5f;
    const float centerY = (top + bottom) * 0.5f;
    const float half = kWallThickness * 0.5f;
    objects.Create<ObjectTest::Wall>(Vector2{ centerX, top + half }, Vector2{ right - left, kWallThickness });
    objects.Create<ObjectTest::Wall>(Vector2{ centerX, bottom - half }, Vector2{ right - left, kWallThickness });
    objects.Create<ObjectTest::Wall>(Vector2{ left + half, centerY }, Vector2{ kWallThickness, bottom - top });
    objects.Create<ObjectTest::Wall>(Vector2{ right - half, centerY }, Vector2{ kWallThickness, bottom - top });

    // 中のブロック
    objects.Create<ObjectTest::Wall>(Vector2{ 640.0f, 260.0f }, Vector2{ 120.0f, 40.0f });
    objects.Create<ObjectTest::Wall>(Vector2{ 760.0f, 620.0f }, Vector2{ 60.0f, 60.0f });
    // L字（2つの四角を隣り合わせに置く。角や継ぎ目に沿って滑れるかの確認用）
    objects.Create<ObjectTest::Wall>(Vector2{ 900.0f, 520.0f }, Vector2{ 200.0f, 40.0f });
    objects.Create<ObjectTest::Wall>(Vector2{ 1020.0f, 440.0f }, Vector2{ 40.0f, 200.0f });
}

void ObjectTestScene::SpawnCoins() {
    for (const Vector2& position : kCoinPositions) {
        // スコアの変数を指すポインタを渡す（コインはシーンより先に片付くので安全）
        objects.Create<ObjectTest::Coin>(position, &score);
    }
}

void ObjectTestScene::SetGhostCollision(bool enable) {
    isGhostCollisionEnabled = enable;
    // レイヤーの設定は GameObjectManager が持つ CollisionManager に入れる（Clear しても残る）
    objects.GetCollisionManager().SetLayerCollision(ObjectTest::kPlayerLayer, ObjectTest::kGhostLayer, enable);
}

void ObjectTestScene::SpawnBoxes(int count) {
    const Vector2 gameSize = Screen::GetGameSize();
    for (int i = 0; i < count; ++i) {
        if (objects.GetCount() >= kMaxObjects) {
            return;
        }
        ++spawnCount;
        bool isSpecial = (spawnCount % kSpecialInterval == 0);
        float size = isSpecial ? kSpecialBoxSize : kBoxSize;
        Vector2 position = { RandomRange(size, gameSize.x - size), RandomRange(size, gameSize.y - size) };
        // コンストラクタの引数 (位置, 大きさ, Specialか) をそのまま渡せる
        objects.Create<FloatingBox>(position, size, isSpecial);
    }
}

void ObjectTestScene::UpdateFps() {
    fpsTimer += Time::GetUnscaledDeltaTime();
    ++fpsFrameCount;
    if (fpsTimer >= 0.5f) {
        fps = static_cast<float>(fpsFrameCount) / fpsTimer;
        fpsTimer = 0.0f;
        fpsFrameCount = 0;
    }
}

std::string ObjectTestScene::BuildHelpText() const {
    if (mode == Mode::kCollision3D) {
        return ObjectTest::Collision3DTest::GetHelpText();
    }
    if (mode == Mode::kCollision) {
        return
            "[←↑↓→] 移動  [Tab] モード切替\n"
            "[G] ゴースト(紫)との当たり ON/OFF\n"
            "[R] シーン読み直し\n"
            "黄=コイン(Trigger) 茶=押せる箱\n"
            "赤=敵(当たると点滅) 灰=壁(Static)";
    }
    return
        "[Space] 1個出す  [Enter] 20個まとめて出す\n"
        "[D] Specialタグを全部Destroy  [C] 全部消す (Clear)\n"
        "[R] シーン読み直し  [Tab] モード切替\n"
        "跳ね返ったとき20%で子を出す（Update中のCreateテスト）\n"
        "10個に1個は大きい Special。3秒で消える";
}

std::string ObjectTestScene::BuildCollisionStatusText() const {
    if (mode == Mode::kCollision3D) {
        return std::format("モード: 3D当たり判定テスト\nFPS: {:.1f}\n", fps) + collision3D.BuildStatusText(objects);
    }
    const CollisionManager& collision = objects.GetCollisionManager();
    const ObjectTest::Player* player = objects.FindByType<ObjectTest::Player>();
    return std::format(
        "モード: 当たり判定テスト\n"
        "FPS: {:.1f}\n"
        "スコア: {} / {}\n"
        "コライダー数: {}\n"
        "大まかな判定の組: {}\n"
        "当たっている組: {}\n"
        "プレイヤーの接触数: {}\n"
        "ゴーストとの当たり: {}",
        fps,
        score, kCoinPositions.size(),
        collision.GetLastColliderCount(),
        collision.GetLastBroadphasePairCount(),
        collision.GetLastHitPairCount(),
        player ? player->GetContactCount() : 0,
        isGhostCollisionEnabled ? "ON" : "OFF");
}

std::string ObjectTestScene::BuildStatusText() const {
    return std::format(
        "モード: 生成テスト\n"
        "FPS: {:.1f}\n"
        "合計 (GetCount): {} / {}\n"
        "Box: {}\n"
        "Special: {}\n"
        "出した数: {}",
        fps,
        objects.GetCount(), kMaxObjects,
        objects.FindAllByTag(kBoxTag).size(),
        objects.FindAllByTag(kSpecialTag).size(),
        spawnCount);
}
