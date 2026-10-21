#pragma once
#include "BaseScene.h"
#include "GameObjectManager.h"
#include "ObjectTest3DMode.h"
#include "Text.h"
#include <memory>
#include <string>

class Input;

/// <summary>
/// GameObject / GameObjectManager を実行画面で確認するための検証用シーン（ゲーム本編では使わない）。
/// 確認対象: Create（引数付き・Update中のCreate）/ Destroy（フレーム末にまとめて削除）/ タグ検索 / Clear / シーン読み直し時の後片付け。
/// [Tab] で3つのモードを順に切り替える:
///   生成テスト: 四角（FloatingBox）は画面内を跳ね回り、3秒で消える（最後の1秒でフェード）。跳ね返ったとき20%で子を出す。
///   当たり判定テスト: 矢印キーで動く四角で、壁（Static）・押せる箱（Dynamic）・コイン（Trigger）・敵・ゴースト（レイヤー）を確認する。
///   3D当たり判定テスト: 床の上をカプセル/球のプレイヤーで動き、AABB・OBB・球・押せる箱・トリガー・レイキャストを確認する。
/// Debug ビルドでは ImGui の "Collision" ウィンドウで数の確認とレイヤー同士の当たり ON/OFF ができる。
/// 起動時に開くには MyGame::Initialize() の最初のシーン名を一時的に "OBJECT_TEST" にする（恒久変更はしないこと）。
/// </summary>
class ObjectTestScene : public BaseScene {
public:
    void Initialize() override;
    void Finalize() override;
    void Update() override;
    void Draw() override;

private:
    enum class Mode {
        kSpawn,     // 生成テスト
        kCollision, // 当たり判定テスト
        kCollision3D, // 3D当たり判定テスト
    };

    void InitializeTexts();
    // [Tab] で次に進むモード（生成 → 2D → 3D → 生成…）
    static Mode GetNextMode(Mode current);
    // ImGui 用のモード名（既定フォントは日本語を出せないので英語）
    static const char* GetModeName(Mode current);
#ifdef _DEBUG
    // "Collision" ウィンドウ（数の確認とレイヤー同士の当たり ON/OFF）
    void UpdateDebugUI();
#endif
    // キー入力（全モード共通: Tab・読み直し。モードごとのキーもここから呼ぶ）
    void UpdateInput(Input* input);
    // 生成テストのキー入力（出す・消す）
    void UpdateSpawnInput(Input* input);
    // 物を全部消して mode の物を出し直す
    void ChangeMode(Mode newMode);
    // 四角を count 個出す（上限を超える分は出さない）
    void SpawnBoxes(int count);
    // 当たり判定テストの物を並べる
    void SpawnCollisionTest();
    void SpawnWalls();
    void SpawnCoins();
    // ゴーストとプレイヤーが当たるかどうかを切り替える
    void SetGhostCollision(bool enable);
    // 0.5秒ごとにFPSを計算する
    void UpdateFps();
    std::string BuildHelpText() const;
    std::string BuildStatusText() const;
    std::string BuildCollisionStatusText() const;

    Mode mode = Mode::kSpawn;
    // 当たり判定テスト: 取ったコインの数 / プレイヤーとゴーストが当たるか
    int score = 0;
    bool isGhostCollisionEnabled = false;

    // 3D当たり判定テストの中身（カメラ・共有メッシュなど）。
    // 物が借りているメッシュを持つので objects より前に宣言する（メンバーは宣言と逆順に破棄されるため、物の方が先に消える）
    ObjectTest::Collision3DTest collision3D;

    // このシーンに登場する物の入れ物（シーンが破棄されると中身も一緒に破棄される）
    GameObjectManager objects;

    std::unique_ptr<Text> titleText;
    std::unique_ptr<Text> helpText;
    std::unique_ptr<Text> statusText;

    // これまでに出した数（10個に1個を "Special" にするため）
    int spawnCount = 0;

    // FPS表示（毎フレーム値が変わると文字画像を毎フレーム作り直すことになるので0.5秒ごとに更新）
    float fpsTimer = 0.0f;
    int fpsFrameCount = 0;
    float fps = 0.0f;
};
