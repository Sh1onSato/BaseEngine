#pragma once
#include "BaseScene.h"
#include "GameObjectManager.h"
#include "LevelSpawner.h"
#include "Structs.h"
#include "Text.h"
#include <memory>
#include <string>

class Camera;

/// <summary>
/// レベルデータ（LevelLoader / LevelSpawner）を実行画面で確認するための検証用シーン（ゲーム本編では使わない）。
/// Resources/levels/test_level.json（Blender アドオン形式）を読み込んで配置する。
/// 確認対象: 座標変換（Z上→Y上・回転）/ 置物と当たり判定 / "class" と名前によるクラス選択 / 独自項目（コインの "score"）/
///           親子 / "disabled" / CAMERA を作らないこと / モデルが無いときの仮の見た目 / [F5] の読み直し。
/// 起動時に開くには MyGame::Initialize() の最初のシーン名を一時的に "LEVEL_TEST" にする（恒久変更はしないこと）。
/// </summary>
class LevelTestScene : public BaseScene {
public:
    void Initialize() override;
    void Finalize() override;
    void Update() override;
    void Draw() override;

private:
    void SetupCamera();
    void InitializeTexts();
    void ReloadLevel();
    void DrawShadowPass();
    // シャドウパスで外されたメインの描画先（RTV / DSV）を張り直す
    void RebindMainRenderTarget();
    std::string BuildStatusText() const;

    // spawner は物に覚えられているが、指す先には触られないので宣言順は問わない
    LevelSpawner spawner;
    GameObjectManager objects;

    // CameraManager が持ち主なので借りるだけ
    Camera* camera = nullptr;

    std::unique_ptr<Text> titleText;
    std::unique_ptr<Text> helpText;
    std::unique_ptr<Text> statusText;

    size_t spawnedCount = 0;
    int reloadCount = 0;
    std::string reloadMessage = "まだ読み直していません";
};
