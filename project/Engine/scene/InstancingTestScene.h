#pragma once
#include "BaseScene.h"
#include "ModelInstancer.h"
#include "Structs.h"
#include "Text.h"
#include <memory>
#include <string>
#include <vector>

class Camera;
class Model;
class Object3d;

/// <summary>
/// ModelInstancer（インスタンシング描画）を実行画面で確認するための検証用シーン（ゲーム本編では使わない）。
/// 格子に並べた立方体を回転させ、[Space] で「まとめて描く（ModelInstancer）」と「1体ずつ描く（Object3d）」を切り替える。
/// 位置・色・回転は両方で同じなので、切り替えても見た目が変わらないこと・影が出ることを確認する。
/// FPS は垂直同期で 60 が上限なので、描画命令を積むのにかかった CPU 時間と描画命令の数も表示する。
/// 起動時に開くには MyGame::Initialize() の最初のシーン名を一時的に "INSTANCING_TEST" にする（恒久変更はしないこと）。
/// </summary>
class InstancingTestScene : public BaseScene {
public:
    InstancingTestScene();
    ~InstancingTestScene() override;

    void Initialize() override;
    void Finalize() override;
    void Update() override;
    void Draw() override;

private:
    void SetupCamera();
    void InitializeTexts();
    void UpdateInput();
    // 個数に合わせてカメラの高さと床の大きさを変える
    void ApplyCountLayout();
    // i 番目の立方体の transform と色
    Transform MakeCubeTransform(size_t index) const;
    Vector4 MakeCubeColor(size_t index) const;
    // 1体ずつ描くモードで使う Object3d を count 個まで用意する
    void EnsureIndividualObjects(size_t count);
    // 今のモードで実際に描く数（1体ずつ描くモードは上限あり）
    size_t GetDrawCount() const;
    void DrawShadowPass();
    // シャドウパスで外されたメインの描画先（RTV / DSV）を張り直す
    void RebindMainRenderTarget();
    void UpdateStats();
    std::string BuildStatusText() const;

    // CameraManager が持ち主なので借りるだけ
    Camera* camera = nullptr;
    // ModelManager が持ち主なので借りるだけ
    Model* cubeModel = nullptr;

    ModelInstancer instancer;
    std::vector<std::unique_ptr<Object3d>> individualObjects;
    std::unique_ptr<Object3d> floor;

    std::unique_ptr<Text> titleText;
    std::unique_ptr<Text> helpText;
    std::unique_ptr<Text> statusText;

    size_t countIndex = 1;
    bool useInstancing = true;
    bool isPaused = false;
    float elapsedTime = 0.0f;

    // 表示用（0.5秒ごとに平均を更新。毎フレーム文字を作り直さないため）
    float statsTimer = 0.0f;
    int statsFrameCount = 0;
    double drawTimeSumMs = 0.0;
    float fps = 0.0f;
    double drawTimeMs = 0.0;
    // 直前の Draw で積んだ描画命令の数（影 + 通常）
    size_t lastDrawCallCount = 0;
};
