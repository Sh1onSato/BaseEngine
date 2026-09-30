#include "SceneManager.h"

void SceneManager::Update() {
    // シーン切り替え予約がある場合
    if (nextScene) {
        // 旧シーンの終了
        if (currentScene) {
            currentScene->Finalize();
        }

        // シーンの差し替え
        currentScene = std::move(nextScene);
        nextScene = nullptr;

        // 次のシーンに自分（マネージャ）を教える
        currentScene->SetSceneManager(this);
        // 新シーンの初期化
        currentScene->Initialize();

        if (currentScene) {
            currentScene->Update();
        }
        return;
    }

    // 現在のシーンを更新
    if (currentScene) {
        currentScene->Update();
    }
}

void SceneManager::Draw() {
    // 現在のシーンを描画
    if (currentScene) {
        currentScene->Draw();
    }
}

void SceneManager::ChangeScene(BaseScene* nextScene) {
    // 次のシーンを予約
    this->nextScene.reset(nextScene);
}

void SceneManager::ChangeScene(const std::string& sceneName) {
    if (sceneFactory) {
        // ファクトリーを使ってシーンを生成し、予約する
        BaseScene* next = sceneFactory->CreateScene(sceneName);
        ChangeScene(next);
    }
}


void SceneManager::Finalize() {
    if (currentScene) {
        currentScene->Finalize();
        currentScene.reset();
    }
}
