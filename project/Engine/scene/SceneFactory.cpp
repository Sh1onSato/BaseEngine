#include "SceneFactory.h"
#include "TitleScene.h"
#include "GameScene.h"
#include "SkinningTestScene.h"
#include "EngineTestScene.h"
#include "ObjectTestScene.h"
#include "LevelTestScene.h"
#include "InstancingTestScene.h"
#include "PhysicsTestScene.h"
#include "UITestScene.h"
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
    else if (sceneName == "ENGINE_TEST") {
        // Text・音・パッド・フェード遷移・フルスクリーンを実行画面で確認する検証用シーン
        newScene = new EngineTestScene();
    }
    else if (sceneName == "OBJECT_TEST") {
        // GameObject / GameObjectManager（生成・削除・タグ検索・Update中の生成）の検証用シーン
        newScene = new ObjectTestScene();
    }
    else if (sceneName == "LEVEL_TEST") {
        // レベルデータ（LevelLoader / LevelSpawner・F5 の読み直し）の検証用シーン
        newScene = new LevelTestScene();
    }
    else if (sceneName == "INSTANCING_TEST") {
        // ModelInstancer（インスタンシング描画）と Object3d の1体ずつ描画の比較用シーン
        newScene = new InstancingTestScene();
    }
    else if (sceneName == "PHYSICS_TEST") {
        // 物理（Rigidbody・重力・跳ね返り・摩擦・接地・動く床・補間）の検証用シーン
        newScene = new PhysicsTestScene();
    }
    else if (sceneName == "UI_TEST") {
        // UI（UICanvas・パネル・文字・ボタン・配置・クリック）の検証用シーン
        newScene = new UITestScene();
    }
    else {
        // 該当なしの場合は無言でnullptrを返すと「シーンが何も更新・描画されない」という
        // 原因不明な無反応に見えてしまう（タイポに気づきにくい）ため、警告を残す
        Log(std::format("SceneFactory: 未知のシーン名が指定されました \"{}\"\n", sceneName));
    }

    return newScene;
}
