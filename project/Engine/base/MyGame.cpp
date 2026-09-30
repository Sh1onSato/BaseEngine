#include "MyGame.h"

void MyGame::Initialize() {
	// 基底クラスの初期化
	Framework::Initialize();

	// シーンファクトリーの生成
	sceneFactory = std::make_unique<SceneFactory>();
	// シーンマネージャにファクトリーをセット
	sceneManager->SetSceneFactory(sceneFactory.get());

	// 最初のアクティブシーンを設定
	// ※スキニング検証(SkinningTestScene)を試す場合は、ここを一時的に"SKINNING_TEST"に変更してください（恒久変更はしないこと）
	sceneManager->ChangeScene("TITLE");
}

void MyGame::Update() {
	// 基底クラスの更新
	Framework::Update();
}

void MyGame::Draw() {
	// 描画前処理
	dxCommon->PreDraw();
	srvManager->PreDraw();

	// シーンマネージャ経由で現在のシーンを描画
	sceneManager->Draw();

	// ImGuiの描画（Debugビルドのみ）
#ifdef _DEBUG
	ImGuiManager::GetInstance()->End();
	ImGuiManager::GetInstance()->Draw();
#endif

	// 画面表示
	dxCommon->PostDraw();
}

void MyGame::Finalize() {
	// 基底クラスの終了処理
	Framework::Finalize();
}
