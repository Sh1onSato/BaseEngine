#pragma once

// シーンマネージャの前方宣言
class SceneManager;

/// <summary>
/// 全てのシーンの基底クラス
/// </summary>
class BaseScene {
public:
    virtual ~BaseScene() = default;

    // 初期化
    virtual void Initialize() = 0;
    // 終了処理
    virtual void Finalize() = 0;
    // 更新
    virtual void Update() = 0;
    // 描画
    virtual void Draw() = 0;

    // シーンマネージャのセット
    virtual void SetSceneManager(SceneManager* manager) { sceneManager = manager; }

protected:
    // シーンマネージャへのポインタ（シーン遷移用）
    SceneManager* sceneManager = nullptr;
};
