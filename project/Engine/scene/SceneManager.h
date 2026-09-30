#pragma once
#include "BaseScene.h"
#include "AbstractSceneFactory.h"
#include <memory>
#include <string>

/// <summary>
/// シーン管理クラス
/// </summary>
class SceneManager {
public:
    // 更新
    void Update();
    // 描画
    void Draw();

    // シーンの切り替え予約
    void ChangeScene(BaseScene* nextScene);

    // シーンの切り替え予約（文字列指定）
    void ChangeScene(const std::string& sceneName);

    // シーンファクトリーのセット
    void SetSceneFactory(AbstractSceneFactory* factory) { sceneFactory = factory; }

    // 終了処理
    void Finalize();

private:
    // 現在のシーン
    std::unique_ptr<BaseScene> currentScene = nullptr;
    // 次のシーン（遷移予約用）
    std::unique_ptr<BaseScene> nextScene = nullptr;

    // シーンファクトリー
    AbstractSceneFactory* sceneFactory = nullptr;
};
