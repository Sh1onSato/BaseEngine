#pragma once
#include "AudioManager.h"
#include "BaseScene.h"
#include "GameObjectManager.h"
#include "NavGrid.h"
#include "SoundPropagation.h"
#include "Text.h"
#include <memory>
#include <string>

class Camera;
class GameObject;
class Object3d;

/// <summary>
/// 音の回り込み（SoundPropagation）を確かめるための検証用シーン（ゲーム本編では使わない）。
/// 確認対象: 隣の部屋の音が扉の方から聞こえる / 遠回りの長さだけ小さく、こもって聞こえる /
///           扉を閉めると壁越しのこもった音になる / 見通せる所では普通に聞こえる / 回り込み OFF（真っすぐのレイだけ）との違い。
/// 耳は緑の球（上から見下ろし、画面の右が右耳）。道は 緑 = 見通せる、黄 = 回り込み、赤 = 壁越し、白い × が聞こえる位置。
/// 起動時に開くには MyGame::Initialize() の最初のシーン名を一時的に "SOUND_PROPAGATION_TEST" にする。
/// </summary>
class SoundPropagationTestScene : public BaseScene {
public:
    SoundPropagationTestScene();
    ~SoundPropagationTestScene() override;

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
        SoundPathResult path;
        std::unique_ptr<Object3d> ball;
    };

    void SetupCamera();
    void InitializeTexts();
    void SpawnWalls();
    void BuildGrid();
    void StartSounds();
    void UpdateInput();
    void UpdateListener();
    void UpdateSources();
    // 回り込み OFF のとき: 真っすぐ見通せるかだけで、こもらせる（SOUND3D_TEST の壁と同じ考え方）
    SoundPathResult EvaluateStraight(const Vector3& sourcePosition) const;
    void ToggleSource(Source& source);
    void DrawShadowPass();
    std::string BuildStatusText() const;

    GameObjectManager objects;
    GameObject* door = nullptr;
    NavGrid grid;
    SoundPropagation propagation;
    // CameraManager が持ち主なので借りるだけ
    Camera* camera = nullptr;

    std::unique_ptr<Object3d> listenerBall;
    Vector3 listenerPosition = { 0.0f, 0.0f, 0.0f };
    size_t listenerRouteIndex = 0;
    bool isListenerManual = false;

    Source radio;  // 奥の部屋で鳴り続ける音
    Source patrol; // 隣の部屋を歩き回る音
    float elapsedTime = 0.0f;
    bool isDoorOpen = true;
    bool isPropagationEnabled = true;
    bool isGridVisible = false;

    std::unique_ptr<Text> titleText;
    std::unique_ptr<Text> helpText;
    std::unique_ptr<Text> statusText;
};
