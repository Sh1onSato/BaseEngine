#pragma once
#include "BaseScene.h"
#include "GameObjectManager.h"
#include "NavGrid.h"
#include "Text.h"
#include <memory>
#include <string>

class Camera;

/// <summary>
/// 経路探索（NavGrid / PathFollower）を実行画面で確認するための検証用シーン（ゲーム本編では使わない）。
/// 確認対象: 壁（Static のコライダー）から歩けない格子が作られる / 追いかけ役が壁を回り込んで目標へ向かう /
///           目標が動くと道を探し直す / 斜めの道で壁の角をすり抜けない / 見通せる所は真っすぐ進む（[S] で比べる）。
/// 起動時に開くには MyGame::Initialize() の最初のシーン名を一時的に "PATHFIND_TEST" にする。
/// </summary>
class PathfindTestScene : public BaseScene {
public:
    void Initialize() override;
    void Finalize() override;
    void Update() override;
    void Draw() override;

    // 追いかけ役が使う（シーンが持つ格子と設定）
    const NavGrid& GetGrid() const { return grid; }
    const NavPathOptions& GetPathOptions() const { return pathOptions; }

private:
    void SetupCamera();
    void InitializeTexts();
    void SpawnAll();
    void BuildGrid();
    void UpdateInput();
    void DrawShadowPass();
    std::string BuildStatusText() const;

    GameObjectManager objects;
    NavGrid grid;
    NavPathOptions pathOptions;
    // CameraManager が持ち主なので借りるだけ
    Camera* camera = nullptr;

    std::unique_ptr<Text> titleText;
    std::unique_ptr<Text> helpText;
    std::unique_ptr<Text> statusText;

    bool isGridVisible = true;
    bool isPathVisible = true;
    float lastBuildMilliseconds = 0.0f;
};
