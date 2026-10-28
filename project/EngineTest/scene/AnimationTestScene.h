#pragma once
#include "AnimationStateMachine.h"
#include "BaseScene.h"
#include "Text.h"
#include <memory>
#include <string>

class Camera;
class Object3d;
class SkinnedObject3d;

/// <summary>
/// アニメーションの拡張（レイヤーとボーンマスク・2次元で混ぜる・時刻の合図・状態ごとの速さ）を確かめるための検証用シーン（ゲーム本編では使わない）。
/// 左: 下半身は Run のまま、背中から先（首・頭・前足）だけ Survey を重ねたり外したりする（3 秒ごと）。
/// 中: Survey / Walk / Run の3本を、2次元のパラメーター（円を描いて動く）で混ぜる。画面に各アニメーションの割合が出る。
/// 右: Walk の中の2つの時刻に合図を付け、合図が来たら足の位置に球を出して音を鳴らす。状態遷移の速さのパラメーターで足の運びが速くなったり遅くなったりする。
/// 起動時に開くには MyGame::Initialize() の最初のシーン名を一時的に "ANIMATION_TEST" にする。
/// </summary>
class AnimationTestScene : public BaseScene {
public:
    AnimationTestScene();
    ~AnimationTestScene() override;

    void Initialize() override;
    void Finalize() override;
    void Update() override;
    void Draw() override;

private:
    void SetupCamera();
    void InitializeTexts();
    std::unique_ptr<SkinnedObject3d> CreateFox(const Vector3& position);
    void SetupLayerFox();
    void SetupBlendFox();
    void SetupEventFox();
    void UpdateInput();
    void UpdateLayerFox();
    void UpdateBlendFox();
    void UpdateEventFox();
    // 合図が来た足の位置に球を出し、音を鳴らす
    void OnStep(const std::string& eventName);
    void DrawShadowPass();
    std::string BuildStatusText() const;

    // CameraManager が持ち主なので借りるだけ
    Camera* camera = nullptr;
    std::unique_ptr<Object3d> floor;

    std::unique_ptr<SkinnedObject3d> layerFox;
    size_t upperBodyLayer = 0;
    bool isLayerOn = false;
    float layerTimer = 0.0f;

    std::unique_ptr<SkinnedObject3d> blendFox;
    Vector2 blendParameter = { 0.0f, 0.0f };

    std::unique_ptr<SkinnedObject3d> eventFox;
    AnimationStateMachine eventAnimator;
    int stepCount = 0;
    std::string lastEvent;

    float elapsedTime = 0.0f;
    bool isPaused = false;

    std::unique_ptr<Text> titleText;
    std::unique_ptr<Text> helpText;
    std::unique_ptr<Text> statusText;
};
