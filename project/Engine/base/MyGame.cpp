#include "MyGame.h"
#include "Screen.h"

void MyGame::Initialize() {
	// 基底クラスの初期化
	Framework::Initialize();

	// シーンファクトリーの生成
	sceneFactory = std::make_unique<SceneFactory>();
	// シーンマネージャにファクトリーをセット
	sceneManager->SetSceneFactory(sceneFactory.get());

	// 画面の縦横比がベース解像度(WinApp::KclientWidth x KclientHeight)と違うときの表示方法
	//   kLetterbox: ゲーム座標はベース解像度のまま、余りは黒帯（2D・STG・パズル等、見える範囲を揃えたいゲーム向け）
	//   kExpand   : 画面いっぱいに描き、ゲーム座標の幅(または高さ)を広げる（3Dアクション・FPS等向け。
	//               端に置くUIは Screen::GetAnchoredPosition() で置くこと）
	Screen::SetScaleMode(Screen::ScaleMode::kLetterbox);

	// 最初のアクティブシーンを設定
	// ※スキニング検証(SkinningTestScene)を試す場合は、ここを一時的に"SKINNING_TEST"に変更してください（恒久変更はしないこと）
	sceneManager->ChangeScene("ANIMATION_TEST");
}

void MyGame::Update() {
	// 基底クラスの更新
	Framework::Update();
}

void MyGame::Draw() {
	// 描画前処理
	dxCommon->PreDraw();
	srvManager->PreDraw();
	// 内部解像度を下げているなら、3D の描画先を小さい物にする（SceneManager がシーンの Draw の後で引き伸ばす）
	renderScaler->BeginScene();

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
