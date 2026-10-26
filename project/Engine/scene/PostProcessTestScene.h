#pragma once
#include "BaseScene.h"
#include "PostProcess.h"
#include "Text.h"
#include <memory>
#include <string>
#include <vector>

class Camera;
class Object3d;

/// <summary>
/// 画面の効果（PostProcess）を実行画面で確認するための検証用シーン（ゲーム本編では使わない）。
/// 確認対象: ブルーム / 画面全体のぼかし / 白黒 / セピア / ヴィネット / 被弾の赤い縁（値を毎フレーム動かす例）/ 夕方の色味。
///           どの効果でも左上の文字（UI）はくっきりしたまま、[F9] の内部解像度・[M] の黒帯と組み合わせても崩れないこと。
/// 起動時に開くには MyGame::Initialize() の最初のシーン名を一時的に "POSTPROCESS_TEST" にする（恒久変更はしないこと）。
/// </summary>
class PostProcessTestScene : public BaseScene {
public:
    PostProcessTestScene();
    ~PostProcessTestScene() override;

    void Initialize() override;
    void Finalize() override;
    void Update() override;
    void Draw() override;

private:
    void SetupCamera();
    void InitializeTexts();
    void InitializeObjects();
    void UpdateInput();
    // 見本 index の効果を、時間 time に合わせて作る（被弾の赤い縁は時間で薄くなる）
    PostEffectSettings MakePreset(size_t index, float time) const;
    void DrawShadowPass();
    std::string BuildStatusText() const;

    // CameraManager が持ち主なので借りるだけ
    Camera* camera = nullptr;
    std::vector<std::unique_ptr<Object3d>> objects;

    std::unique_ptr<Text> titleText;
    std::unique_ptr<Text> helpText;
    std::unique_ptr<Text> statusText;

    size_t presetIndex = 0;
    bool isAutoCycle = true; // 一定時間ごとに次の見本へ進む
    float presetTimer = 0.0f;
};
