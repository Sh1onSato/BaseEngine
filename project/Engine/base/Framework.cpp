#include "Framework.h"
#include "Logger.h"
#include "ModelManager.h"
#include "CameraManager.h"
#include "GameSettings.h"
#include "Tween.h"

Framework* Framework::instance = nullptr;

void Framework::Run() {
	// 初期化
	Initialize();

	// メインループ
	while (true) {
		// ウィンドウメッセージ処理
		if (winApp->ProcessMassage()) {
			break;
		}

		// 更新
		Update();

		// 終了リクエストがあればループを抜ける
		if (IsEndRequest()) {
			break;
		}

		// 描画
		Draw();
	}

	// 終了
	Finalize();
}

void Framework::Initialize() {
	instance = this;
	winApp = std::make_unique<WinApp>();
	winApp->Initialize();

	dxCommon = std::make_unique<DirectXCommon>();
	dxCommon->Initialize(winApp.get());

	srvManager = std::make_unique<SrvManager>();
	srvManager->Initialize(dxCommon.get());

	input = std::make_unique<Input>();
	input->Initialize(winApp.get());

	AudioManager::GetInstance()->Initialize();
	// 保存済みの設定（音量など）を読み込んでAudioManagerへ反映
	GameSettings::GetInstance()->Load();
	// ウィンドウサイズを先に戻してからフルスクリーンにする（フルスクリーン解除時にこのサイズへ戻るように）
	winApp->SetClientSize(GameSettings::GetInstance()->GetWindowWidth(), GameSettings::GetInstance()->GetWindowHeight());
	winApp->SetFullscreen(GameSettings::GetInstance()->GetFullscreen());
#ifdef _DEBUG
	// ImGuiはDebugビルドのみ初期化する（Release/Developmentでは調整UIとして使えないようにするため）
	ImGuiManager::GetInstance()->Initialize(winApp.get(), dxCommon.get(), srvManager.get());
#endif

	TextureManager::GetInstance()->Initialize(dxCommon.get(), srvManager.get());
	ModelManager::GetInstance()->Initialize(dxCommon.get());

	postProcess = std::make_unique<PostProcess>();
	postProcess->Initialize(dxCommon.get(), srvManager.get());

	renderScaler = std::make_unique<RenderScaler>();
	renderScaler->Initialize(dxCommon.get(), srvManager.get());
	renderScaler->SetPostProcess(postProcess.get());
	// 保存済みの「描画の細かさ」を反映する（[F9] で変えた倍率は確認用なので保存しない）
	renderScaler->SetScale(GameSettings::GetInstance()->GetRenderScale());

	spriteCommon = std::make_unique<SpriteCommon>();
	spriteCommon->Initialize(dxCommon.get(), srvManager.get());

	object3dCommon = std::make_unique<Object3dCommon>();
	object3dCommon->Initialize(dxCommon.get(), srvManager.get());

	particleCommon = std::make_unique<ParticleCommon>();
	particleCommon->Initialize(dxCommon.get(), srvManager.get());

	skyboxCommon = std::make_unique<SkyboxCommon>();
	skyboxCommon->Initialize(dxCommon.get(), srvManager.get());

	debugDraw = std::make_unique<DebugDraw>();
	debugDraw->Initialize(dxCommon.get());

	CameraManager::GetInstance()->Initialize();

	sceneManager = std::make_unique<SceneManager>();
	sceneManager->Initialize(spriteCommon.get());

	// ウィンドウの枠をドラッグしている間はメインループが止まるので、WinAppのタイマーから1フレームずつ進めてもらう
	// （終了リクエストの確認はメインループに戻ってから行う）
	winApp->SetModalTickCallback([this]() {
		Update();
		Draw();
	});

	// 初期化にかかった時間が最初のdeltaTimeに乗らないよう、最後に計測を開始する
	Time::Initialize();
}

void Framework::Update() {
	Time::Tick();
	// ウィンドウサイズが変わっていたらバックバッファを作り直す。
	// マウス座標の変換に新しいレイアウトを使うため、入力の更新より前に行う
	// （前フレームのPostDrawでGPUの完了は待っているが、Resize内でも念のため待つ）
	uint32_t width = 0;
	uint32_t height = 0;
	if (winApp->ConsumeResize(width, height)) {
		dxCommon->Resize(width, height);
	}
	// 使われなくなったモデルと画像を解放する（前フレームの描画は PostDraw で GPU の完了を待っているので安全）。
	// モデルが消えると画像の使用数が減るので、モデルを先にする
	ModelManager::GetInstance()->Update();
	TextureManager::GetInstance()->Update();
	input->Update();
	// フェードはポーズ(timeScale=0)中も進めたいのでunscaledを使う
	AudioManager::GetInstance()->Update(Time::GetUnscaledDeltaTime());
#ifdef _DEBUG
	ImGuiManager::GetInstance()->Begin();
	// [F9] 内部解像度の倍率を 1.0 → 0.75 → 0.5 → 0.25 → 1.0 … と切り替える（重さ・見え方の確認用）
	if (input->TriggerKey(DIK_F9)) {
		const float scale = renderScaler->GetScale();
		renderScaler->SetScale(scale > 0.9f ? 0.75f : (scale > 0.6f ? 0.5f : (scale > 0.3f ? 0.25f : 1.0f)));
		Logger::Log(std::format("RenderScaler: 内部解像度の倍率 {:.2f}\n", renderScaler->GetScale()));
	}
	// [F7] 当たり判定の形と Rigidbody の速度を線で表示する / しない（全シーン共通）
	if (input->TriggerKey(DIK_F7)) {
		DebugDraw::SetColliderVisible(!DebugDraw::IsColliderVisible());
	}
	// [F6] 画面の効果（PostProcess）の調整用の窓を出す / しまう
	if (input->TriggerKey(DIK_F6)) {
		isPostProcessUIVisible = !isPostProcessUIVisible;
	}
	if (isPostProcessUIVisible) {
		postProcess->DrawDebugUI();
	}
#endif
	// Tween は シーンの Update の前に進める（シーンが行列を作る時に、このフレームの値が入っているように）
	Tweens::Update();
	sceneManager->Update();
}

void Framework::Finalize() {
	winApp->SetModalTickCallback(nullptr);
	sceneManager->Finalize();

	// 各種マネージャーの終了処理
	ModelManager::GetInstance()->Finalize();
	TextureManager::GetInstance()->Finalize();

	// 解放処理（unique_ptr が自動で行うため delete は不要）
	
	// 設定画面で変えた値を次回起動時に引き継ぐ
	GameSettings::GetInstance()->SetFullscreen(winApp->IsFullscreen());
	// 枠をドラッグして変えたサイズも次回に引き継ぐ
	uint32_t windowWidth = 0;
	uint32_t windowHeight = 0;
	winApp->GetWindowedClientSize(windowWidth, windowHeight);
	GameSettings::GetInstance()->SetWindowSize(static_cast<int>(windowWidth), static_cast<int>(windowHeight));
	GameSettings::GetInstance()->Save();
	AudioManager::GetInstance()->Finalize();
#ifdef _DEBUG
	ImGuiManager::GetInstance()->Finalize();
#endif
	CloseHandle(dxCommon->GetFenceEvent());
	// この後デバイスが解放されるので、Loggerに渡していた参照を外す
	Logger::SetDiagnosticsDevice(nullptr);

	winApp->Finalize();
}
