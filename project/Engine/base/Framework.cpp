#include "Framework.h"
#include "Logger.h"
#include "ModelManager.h"
#include "CameraManager.h"
#include "GameSettings.h"

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

	spriteCommon = std::make_unique<SpriteCommon>();
	spriteCommon->Initialize(dxCommon.get(), srvManager.get());

	object3dCommon = std::make_unique<Object3dCommon>();
	object3dCommon->Initialize(dxCommon.get(), srvManager.get());

	particleCommon = std::make_unique<ParticleCommon>();
	particleCommon->Initialize(dxCommon.get(), srvManager.get());

	skyboxCommon = std::make_unique<SkyboxCommon>();
	skyboxCommon->Initialize(dxCommon.get(), srvManager.get());

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
	input->Update();
	// フェードはポーズ(timeScale=0)中も進めたいのでunscaledを使う
	AudioManager::GetInstance()->Update(Time::GetUnscaledDeltaTime());
#ifdef _DEBUG
	ImGuiManager::GetInstance()->Begin();
#endif
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
