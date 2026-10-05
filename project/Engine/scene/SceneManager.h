#pragma once
#include "BaseScene.h"
#include "AbstractSceneFactory.h"
#include "Structs.h"
#include <memory>
#include <string>

class Sprite;
class SpriteCommon;

/// <summary>
/// シーン管理クラス
/// </summary>
class SceneManager {
public:
    SceneManager();
    ~SceneManager();

    // フェード描画用の初期化（Framework::Initialize()から呼ばれる）
    void Initialize(SpriteCommon* spriteCommon);

    // 更新
    void Update();
    // 描画（シーンの描画後、フェード中なら画面全体を覆う板を重ねる）
    void Draw();

    // シーンの切り替え予約（次のUpdateで即切り替え）
    void ChangeScene(BaseScene* nextScene);

    // シーンの切り替え予約（文字列指定）
    // fadeSeconds > 0 なら、fadeSeconds秒かけて暗転 → 切り替え → fadeSeconds秒かけて明転する。
    // 遷移中に呼ばれた切り替え要求は無視する（決定ボタン連打で二重遷移しないように）
    void ChangeScene(const std::string& sceneName, float fadeSeconds = 0.0f, const Vector4& fadeColor = { 0.0f, 0.0f, 0.0f, 1.0f });

    // フェード中（暗転・明転のどちらか）か。遷移中は入力を受け付けない等の判断に使う
    bool IsTransitioning() const { return fadeState != FadeState::kNone; }

    // シーンファクトリーのセット
    void SetSceneFactory(AbstractSceneFactory* factory) { sceneFactory = factory; }

    // 終了処理
    void Finalize();

private:
    enum class FadeState {
        kNone,
        kFadeOut, // 暗転中（終わったらシーンを切り替える）
        kFadeIn,  // 明転中
    };

    void SwapScene();

    // 現在のシーン
    std::unique_ptr<BaseScene> currentScene = nullptr;
    // 次のシーン（遷移予約用）
    std::unique_ptr<BaseScene> nextScene = nullptr;

    // シーンファクトリー
    AbstractSceneFactory* sceneFactory = nullptr;

    // --- フェード ---
    SpriteCommon* spriteCommon = nullptr;
    std::unique_ptr<Sprite> fadeSprite;
    FadeState fadeState = FadeState::kNone;
    std::string pendingSceneName;
    float fadeDuration = 0.0f;
    float fadeTimer = 0.0f;
    Vector4 fadeColor = { 0.0f, 0.0f, 0.0f, 1.0f };
    float fadeAlpha = 0.0f;
};
