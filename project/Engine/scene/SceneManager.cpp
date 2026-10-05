#include "SceneManager.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "GameTime.h"
#include "WinApp.h"
#include <algorithm>

SceneManager::SceneManager() = default;
// Spriteの完全な型が必要なため、デストラクタは.cppで定義する
SceneManager::~SceneManager() = default;

void SceneManager::Initialize(SpriteCommon* common) {
    spriteCommon = common;
    fadeSprite = std::make_unique<Sprite>();
    fadeSprite->Initialize(spriteCommon, "white");
    fadeSprite->SetPosition({ 0.0f, 0.0f });
    fadeSprite->SetSize({ static_cast<float>(WinApp::KclientWidth), static_cast<float>(WinApp::KclientHeight) });
}

void SceneManager::SwapScene() {
    // 旧シーンの終了
    if (currentScene) {
        currentScene->Finalize();
    }

    // シーンの差し替え
    currentScene = std::move(nextScene);
    nextScene = nullptr;

    if (currentScene) {
        // 次のシーンに自分（マネージャ）を教える
        currentScene->SetSceneManager(this);
        // 新シーンの初期化
        currentScene->Initialize();
    }
}

void SceneManager::Update() {
    // --- フェードの進行（ポーズ中のtimeScaleに影響されないようunscaledで進める） ---
    if (fadeState != FadeState::kNone) {
        fadeTimer += Time::GetUnscaledDeltaTime();
        float t = fadeDuration > 0.0f ? (std::min)(1.0f, fadeTimer / fadeDuration) : 1.0f;

        if (fadeState == FadeState::kFadeOut) {
            fadeAlpha = t;
            if (t >= 1.0f) {
                // 真っ暗になった所でシーンを切り替え、明転に移る
                if (sceneFactory) {
                    nextScene.reset(sceneFactory->CreateScene(pendingSceneName));
                }
                fadeState = FadeState::kFadeIn;
                fadeTimer = 0.0f;
            }
        } else {
            fadeAlpha = 1.0f - t;
            if (t >= 1.0f) {
                fadeState = FadeState::kNone;
                fadeAlpha = 0.0f;
            }
        }
    }

    // シーン切り替え予約がある場合
    if (nextScene) {
        SwapScene();
    }

    // 現在のシーンを更新
    if (currentScene) {
        currentScene->Update();
    }

    if (fadeSprite && fadeAlpha > 0.0f) {
        fadeSprite->SetColor({ fadeColor.x, fadeColor.y, fadeColor.z, fadeColor.w * fadeAlpha });
        fadeSprite->Update();
    }
}

void SceneManager::Draw() {
    // 現在のシーンを描画
    if (currentScene) {
        currentScene->Draw();
    }

    // フェード用の板を最前面に描画
    if (fadeSprite && spriteCommon && fadeAlpha > 0.0f) {
        spriteCommon->PreDraw();
        fadeSprite->Draw();
    }
}

void SceneManager::ChangeScene(BaseScene* nextScene) {
    // 次のシーンを予約
    this->nextScene.reset(nextScene);
}

void SceneManager::ChangeScene(const std::string& sceneName, float fadeSeconds, const Vector4& color) {
    if (!sceneFactory || IsTransitioning()) {
        return;
    }

    if (fadeSeconds <= 0.0f || !fadeSprite) {
        // ファクトリーを使ってシーンを生成し、予約する
        ChangeScene(sceneFactory->CreateScene(sceneName));
        return;
    }

    pendingSceneName = sceneName;
    fadeDuration = fadeSeconds;
    fadeTimer = 0.0f;
    fadeColor = color;
    fadeState = FadeState::kFadeOut;
}


void SceneManager::Finalize() {
    if (currentScene) {
        currentScene->Finalize();
        currentScene.reset();
    }
    nextScene.reset();
}
