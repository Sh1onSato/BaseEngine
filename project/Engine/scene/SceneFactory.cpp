#include "SceneFactory.h"
#include "TitleScene.h"
#include "GameScene.h"
#include "SkinningTestScene.h"
#include "Logger.h"
#include <format>

using namespace Logger;

BaseScene* SceneFactory::CreateScene(const std::string& sceneName) {
    BaseScene* newScene = nullptr;

    if (sceneName == "TITLE") {
        newScene = new TitleScene();
    }
    else if (sceneName == "GAME") {
        newScene = new GameScene();
    }
    else if (sceneName == "SKINNING_TEST") {
        // スケルタルアニメーション フェーズ1（スキニング描画パイプラインの土台）の検証用シーン
        newScene = new SkinningTestScene();
    }
    else {
        // 該当なしの場合は無言でnullptrを返すと「シーンが何も更新・描画されない」という
        // 原因不明な無反応に見えてしまう（タイポに気づきにくい）ため、警告を残す
        Log(std::format("SceneFactory: 未知のシーン名が指定されました \"{}\"\n", sceneName));
    }

    return newScene;
}
