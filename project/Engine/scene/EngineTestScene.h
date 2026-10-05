#pragma once
#include "BaseScene.h"
#include "Text.h"
#include <memory>
#include <string>

class Input;

/// <summary>
/// エンジン機能を実行画面で確認するための検証用シーン（ゲーム本編では使わない）。
/// 確認対象: Text（日本語・改行・揃え・色）/ AudioManager（SE・BGMのフェードと切り替え・一時停止・音量・ピッチ）/
/// パッド入力と振動 / シーンのフェード遷移 / フルスクリーン切り替え（Alt+Enter・F11はWinApp側で処理）。
/// 音声は Resources/sounds/GenerateTestSounds.ps1 で生成したテスト用wavを使う。
/// 起動時に開くには MyGame::Initialize() の最初のシーン名を一時的に "ENGINE_TEST" にする（恒久変更はしないこと）。
/// </summary>
class EngineTestScene : public BaseScene {
public:
    void Initialize() override;
    void Finalize() override;
    void Update() override;
    void Draw() override;

private:
    void UpdateAudio(Input* input);
    void UpdatePad(Input* input);
    void UpdateSceneChange(Input* input);
    void PlayBgm(const std::string& name);
    std::string BuildStatusText(Input* input) const;
#ifdef _DEBUG
    void UpdateDebugUI(Input* input);
    int debugClickCount = 0;
#endif

    std::unique_ptr<Text> titleText;
    std::unique_ptr<Text> helpText;
    std::unique_ptr<Text> statusText;
    std::unique_ptr<Text> alignText;

    // 再生中BGMの表示用（AudioManagerはハンドルしか持たないため名前はこちらで覚えておく）
    std::string bgmName;
    bool isBgmPaused = false;
    bool isPitchUp = false;

    int titleColorIndex = 0;

    // FPS表示（毎フレーム値が変わると文字画像を毎フレーム作り直すことになるので0.5秒ごとに更新）
    float fpsTimer = 0.0f;
    int fpsFrameCount = 0;
    float fps = 0.0f;
};
