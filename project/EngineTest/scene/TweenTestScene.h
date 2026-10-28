#pragma once
#include "BaseScene.h"
#include "Text.h"
#include <memory>
#include <string>
#include <vector>

class Camera;
class Object3d;
class Sprite;

/// <summary>
/// Tween（値を決めた秒数で変える）・TweenSequence（順番に動かす）・カメラの揺れ・ヒットストップを確かめるための検証用シーン（ゲーム本編では使わない）。
/// 左: イージングの違い（同じ距離を同じ秒数で、行って戻る）。右上: 順番に動く演出（出てくる → 動く → 回る → 色 → 消える）。
/// 右下: 回る立方体。[H] でヒットストップ（立方体とイージングが一瞬止まる）とカメラの揺れ、[S] で揺れだけ、[P] で時間を止める
/// （時間を止めても「UI」の文字は動き続ける = SetUnscaledTime）。
/// 起動時に開くには MyGame::Initialize() の最初のシーン名を一時的に "TWEEN_TEST" にする。
/// </summary>
class TweenTestScene : public BaseScene {
public:
    TweenTestScene();
    ~TweenTestScene() override;

    void Initialize() override;
    void Finalize() override;
    void Update() override;
    void Draw() override;

private:
    // イージングの見本1本分
    struct EaseSample {
        std::unique_ptr<Sprite> box;
        std::unique_ptr<Text> label;
    };

    void SetupCamera();
    void InitializeTexts();
    void SetupEaseSamples();
    void SetupSequence();
    void SetupCube();
    void UpdateInput();
    void HitStop();
    void DrawShadowPass();
    std::string BuildStatusText() const;

    // CameraManager が持ち主なので借りるだけ
    Camera* camera = nullptr;
    std::unique_ptr<Object3d> cube;
    std::unique_ptr<Object3d> floor;

    std::vector<EaseSample> easeSamples;
    std::unique_ptr<Sprite> sequenceBox;
    std::unique_ptr<Text> sequenceText;
    int sequenceLoopCount = 0;
    std::unique_ptr<Text> unscaledText;

    bool isTimeStopped = false;
    int hitStopCount = 0;

    std::unique_ptr<Text> titleText;
    std::unique_ptr<Text> helpText;
    std::unique_ptr<Text> statusText;
};
