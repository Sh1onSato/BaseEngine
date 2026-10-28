#pragma once
#include "BaseScene.h"
#include "GameObjectManager.h"
#include "Text.h"
#include <memory>
#include <string>

class Camera;

/// <summary>
/// 回転する剛体（Rigidbody::SetUseRotation）を確かめるための検証用シーン（ゲーム本編では使わない）。
/// 確認対象: 積んだ箱に球を投げると崩れて転がる / ドミノが順に倒れる / 坂の上の球が転がり落ちる（回りながら）/
///           斜めに落とした箱が角から着地して倒れ、最後は面で止まる / カプセルが倒れて転がる（カプセルは Debug ビルドの線で表示）/
///           回らない物（ResolveVelocity の今までの計算）と混ざっても動く。
/// 起動時に開くには MyGame::Initialize() の最初のシーン名を一時的に "RIGIDBODY_TEST" にする。
/// </summary>
class RigidbodyTestScene : public BaseScene {
public:
    void Initialize() override;
    void Finalize() override;
    void Update() override;
    void Draw() override;

private:
    void SetupCamera();
    void InitializeTexts();
    // 全部を最初の並びに作り直す
    void SpawnAll();
    void SpawnTower();
    void SpawnDominoes();
    void SpawnRamp();
    // カメラの前から球を投げる
    void ThrowBall();
    void UpdateInput();
    void DrawShadowPass();
    std::string BuildStatusText() const;

    GameObjectManager objects;
    // CameraManager が持ち主なので借りるだけ
    Camera* camera = nullptr;
    float elapsedTime = 0.0f;
    bool hasPushedDomino = false;
    bool hasThrownAuto = false;

    std::unique_ptr<Text> titleText;
    std::unique_ptr<Text> helpText;
    std::unique_ptr<Text> statusText;
};
