#pragma once
#include "BaseScene.h"
#include "Camera2D.h"
#include "GameObjectManager.h"
#include "Text.h"
#include "TileMap.h"
#include "TiledSpawner.h"
#include <memory>
#include <string>

/// <summary>
/// Tiled のマップ（TiledLoader / TiledSpawner / TileMap）を実行画面で確認するための検証用シーン（ゲーム本編では使わない）。
/// Resources/maps/test_map.tmj（外部タイルセット test_tiles.tsj）を読み、横スクロールの小さなステージにする。
/// 確認対象: タイルレイヤーの描画（背景・地面・前景の重なり、前景の半透明）/ 地面の当たり判定（"collision"）/
///           Class・名前・タイルの Class によるクラス選択 / 独自の項目（コインの "score"、タイルセットからの引き継ぎ）/
///           "collider": "trigger" のゴール / 未登録のタイルの物（旗）/ 2D カメラ / [F5] の読み直し。
/// 起動時に開くには MyGame::Initialize() の最初のシーン名を一時的に "TILED_TEST" にする（恒久変更はしないこと）。
/// </summary>
class TiledTestScene : public BaseScene {
public:
    void Initialize() override;
    void Finalize() override;
    void Update() override;
    void Draw() override;

private:
    void InitializeTexts();
    void ReloadMap();
    // プレイヤーを画面の真ん中に映す（マップの外は映さない）
    void UpdateCamera();
    std::string BuildStatusText() const;

    // spawner のマップをタイルの物が参照するので、objects より先に宣言する（後に消える）
    TiledSpawner spawner;
    GameObjectManager objects;
    TileMap tileMap;
    Camera2D camera;

    std::unique_ptr<Text> titleText;
    std::unique_ptr<Text> helpText;
    std::unique_ptr<Text> statusText;

    int reloadCount = 0;
    std::string reloadMessage = "まだ読み直していません";
};
