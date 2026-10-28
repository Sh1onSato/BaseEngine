#pragma once
#include "BaseScene.h"
#include "GameObjectManager.h"
#include "Text.h"
#include <memory>
#include <string>

class Camera;

/// <summary>
/// 物理（Rigidbody / GameObjectManager の固定間隔計算）を実行画面で確認するための検証用シーン（ゲーム本編では使わない）。
/// 確認対象: 重力 / ジャンプと接地判定 / 跳ね返り係数の違い / 質量の違い（軽い箱・重い箱）/ 坂を滑る（摩擦）/
///           動く床（キネマティック）に乗った物が運ばれる / 描画の補間（[T] スローで確認）。
/// 起動時に開くには MyGame::Initialize() の最初のシーン名を一時的に "PHYSICS_TEST" にする（恒久変更はしないこと）。
/// </summary>
class PhysicsTestScene : public BaseScene {
public:
    void Initialize() override;
    void Finalize() override;
    void Update() override;
    void Draw() override;

private:
    void SetupCamera();
    void InitializeTexts();
    // 文字を画面の端（左上・左下・右上）に合わせて置く
    void LayoutTexts();
    // 物を全部消して並べ直す
    void SpawnAll();
    void UpdateInput();
    void SetInterpolationForAll(bool enable);
    void DrawShadowPass();
    // シャドウパスで外されたメインの描画先（RTV / DSV）を張り直す
    void RebindMainRenderTarget();
    std::string BuildStatusText() const;
    // [G] で表示する確認用の線（格子・プレイヤーの速度と地面の向き・真下へのレイ・画面の枠）
    void AddDebugLines();

    GameObjectManager objects;
    // CameraManager が持ち主なので借りるだけ
    Camera* camera = nullptr;

    std::unique_ptr<Text> titleText;
    std::unique_ptr<Text> helpText;   // 操作キー（左上）
    std::unique_ptr<Text> detailText; // 置いてある物の説明（左下。[H] で表示）
    std::unique_ptr<Text> statusText; // プレイヤーの状態（右上）

    bool isDetailVisible = false;
    bool isSlowMotion = false;
    bool isInterpolationEnabled = true;
    bool isDebugLineVisible = true;
};
