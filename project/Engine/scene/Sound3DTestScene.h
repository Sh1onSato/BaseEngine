#pragma once
#include "AudioManager.h"
#include "BaseScene.h"
#include "Text.h"
#include <memory>
#include <string>
#include <vector>

class Camera;
class Object3d;

/// <summary>
/// 3D の音（AudioManager::Play3D / SetSoundPosition / SetPan、リスナーがカメラに付いていく）を確かめるための検証用シーン（ゲーム本編では使わない）。
/// 確認対象: カメラの周りを回る音が左右に動いて聞こえる / 遠ざかる音が小さくなり、範囲の外で聞こえなくなる /
///           3D でない音の左右の振り分け（SetPan）/ カメラを回すと左右が入れ替わる。画面の数字（左右の音量）でも確かめられる。
/// 起動時に開くには MyGame::Initialize() の最初のシーン名を一時的に "SOUND3D_TEST" にする。
/// </summary>
class Sound3DTestScene : public BaseScene {
public:
    Sound3DTestScene();
    ~Sound3DTestScene() override;

    void Initialize() override;
    void Finalize() override;
    void Update() override;
    void Draw() override;

private:
    // 鳴らしている音1つ分
    struct Source {
        std::string label;
        SoundHandle handle = kInvalidSoundHandle;
        bool isEnabled = true;
        Vector3 position = { 0.0f, 0.0f, 0.0f };
    };

    void SetupCamera();
    void InitializeTexts();
    void StartSounds();
    void UpdateInput();
    void UpdateSources();
    void DrawShadowPass();
    std::string BuildStatusText() const;

    // CameraManager が持ち主なので借りるだけ
    Camera* camera = nullptr;
    std::unique_ptr<Object3d> floor;
    std::unique_ptr<Object3d> orbitBall;
    std::unique_ptr<Object3d> distanceBall;

    Source orbitSource;    // カメラの前を回る音
    Source distanceSource; // 近づいたり遠ざかったりする音
    Source panSource;      // 3D でない、左右だけ振る音
    float elapsedTime = 0.0f;
    float cameraYaw = 0.0f;

    std::unique_ptr<Text> titleText;
    std::unique_ptr<Text> helpText;
    std::unique_ptr<Text> statusText;
};
