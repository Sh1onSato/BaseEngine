#pragma once
#include "BaseScene.h"
#include "Text.h"
#include "Sprite.h"
#include "SpriteBlendMode.h"
#include "Camera2D.h"
#include "SpriteAnimation.h"
#include <memory>
#include <string>

class Input;

/// <summary>
/// エンジン機能を実行画面で確認するための検証用シーン（ゲーム本編では使わない）。
/// 確認対象: Text（日本語・改行・揃え・色）/ AudioManager（SE・BGMのフェードと切り替え・一時停止・音量・ピッチ）/
/// パッド入力と振動 / シーンのフェード遷移 / フルスクリーン切り替え（Alt+Enter・F11はWinApp側で処理）。
/// 2Dカメラ（Camera2D）とスプライトアニメーション（SpriteAnimation）も確認できる。
/// 音声は Resources/sounds/GenerateTestSounds.ps1 で生成したテスト用wavを使う。
/// 画像は Resources/textures/GenerateTestTextures.ps1 で生成したテスト用pngを使う。
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
    void InitializeSpriteTest();
    void UpdateSpriteTest(Input* input);
    std::string BuildStatusText(Input* input) const;
#ifdef _DEBUG
    void UpdateDebugUI(Input* input);
    int debugClickCount = 0;
#endif

    std::unique_ptr<Text> titleText;
    std::unique_ptr<Text> helpText;
    std::unique_ptr<Text> statusText;
    std::unique_ptr<Text> alignText;

    // ブレンドテスト: 重なった3色の四角（通常ブレンド）の上に、[B]でモードを切り替える半透明の四角を重ねる
    static constexpr size_t kBlendBaseSpriteCount = 3;
    std::unique_ptr<Sprite> blendBaseSprites[kBlendBaseSpriteCount];
    std::unique_ptr<Sprite> blendTopSprite;
    SpriteBlendMode blendTestMode = SpriteBlendMode::kNormal;
    // レイヤーテスト: [L]で3色の四角のレイヤーを {0,1,2}→{1,2,0}→{2,0,1} と回す（Drawの呼び出し順は固定のまま）
    int layerTestShift = 0;
    // 黄色の四角は常に3色より手前に出すためのレイヤー
    static constexpr int kBlendTopLayer = 10;
    // 3色の四角に現在のレイヤーを設定する
    void ApplyLayerTest();
    // 一番手前にある四角の番号（レイヤーが最大のもの）
    size_t GetFrontBlendBaseIndex() const;
    // フィルタテスト: 小さいドットフォント画像を拡大表示し、[N]でポイント/リニアを切り替える
    std::unique_ptr<Sprite> filterTestSprite;
    bool usePointFilter = false;

    // 2Dカメラテスト: ワールド座標のグリッド背景とアニメーションするキャラを、カメラで移動・ズーム・回転して見る
    // （どちらもレイヤー-10でUIより奥。SpriteCommonにはInitializeでセットし、Finalizeで外す）
    Camera2D camera2D;
    std::unique_ptr<Sprite> worldGridSprite;
    std::unique_ptr<Sprite> characterSprite;
    SpriteAnimation characterAnimation;
    void InitializeCameraTest();
    void UpdateCameraTest(Input* input);
    // カメラの移動・ズーム・回転・リセット
    void UpdateCameraControl(Input* input);

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
