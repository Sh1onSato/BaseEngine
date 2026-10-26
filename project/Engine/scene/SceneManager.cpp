#include "SceneManager.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "GameTime.h"
#include "Screen.h"
#include "TextureManager.h"
#include "ModelManager.h"
#include "Framework.h"
#include "DebugDraw.h"
#include <algorithm>

SceneManager::SceneManager() = default;
// Spriteの完全な型が必要なため、デストラクタは.cppで定義する
SceneManager::~SceneManager() = default;

void SceneManager::Initialize(SpriteCommon* common) {
    spriteCommon = common;
    fadeSprite = std::make_unique<Sprite>();
    fadeSprite->Initialize(spriteCommon, "white");
    fadeSprite->SetPosition({ 0.0f, 0.0f });
    fadeSprite->SetSize(Screen::GetGameSize());
}

void SceneManager::SwapScene() {
    // 旧シーンの終了
    if (currentScene) {
        currentScene->Finalize();
    }

    // シーンの差し替え（ここで旧シーンが消え、旧シーンの Sprite などが持っていた画像の使用数が減る）
    currentScene = std::move(nextScene);
    nextScene = nullptr;

    // 旧シーンまでに読んだモデルと画像のうち、新シーンの Initialize で使われなかった物を次のフレームの始めに解放する
    ModelManager::GetInstance()->OnSceneChanged();
    TextureManager::GetInstance()->OnSceneChanged();
    // 旧シーンで積んだ確認用の線（秒数付きの物）を残さない。旧シーンのカメラを指したままにもしない
    DebugDraw::Clear();
    DebugDraw::SetCamera(nullptr);
    // 旧シーンで変えた画面の効果を、既定（MyGame で決めた物）に戻す
    Framework::GetInstance()->GetPostProcess()->ResetToDefault();

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
        // ゲーム座標のサイズはウィンドウサイズや表示モードで変わるので、毎回ゲーム画面全体を覆うように合わせる
        fadeSprite->SetPosition({ 0.0f, 0.0f });
        fadeSprite->SetSize(Screen::GetGameSize());
        fadeSprite->SetColor({ fadeColor.x, fadeColor.y, fadeColor.z, fadeColor.w * fadeAlpha });
        fadeSprite->Update();
    }
}

void SceneManager::Draw() {
    // 現在のシーンを描画
    if (currentScene) {
        currentScene->Draw();
    }
    // 確認用の 3D の線（シーンの深度で物に隠れるように、3D と同じ描画先に描く）
    DebugDraw::Flush3D();
    // 内部解像度を下げて描いた 3D を画面に引き伸ばし、描画先をバックバッファに戻す（UI はこの後に等倍で描く）
    Framework::GetInstance()->GetRenderScaler()->EndScene();
    // シーンが積んだスプライトをレイヤー順に描画する
    if (spriteCommon) {
        spriteCommon->Flush();
    }
    // 確認用の 2D の線（UI より手前、フェードより奥）
    DebugDraw::Flush2D();

    // フェード用の板を最前面に描画
    // （シーンのFlushとは別に後からFlushするので、レイヤー値に関係なく必ず一番手前になる）
    if (fadeSprite && spriteCommon && fadeAlpha > 0.0f) {
        fadeSprite->Draw();
        spriteCommon->Flush();
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
