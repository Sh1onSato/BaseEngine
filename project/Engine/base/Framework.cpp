#include "Framework.h"
#include "ModelManager.h"
#include "CameraManager.h"

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
}

void Framework::Update() {
	input->Update();
#ifdef _DEBUG
	ImGuiManager::GetInstance()->Begin();
#endif
	sceneManager->Update();
}

void Framework::Finalize() {
	sceneManager->Finalize();

	// 各種マネージャーの終了処理
	ModelManager::GetInstance()->Finalize();
	TextureManager::GetInstance()->Finalize();

	// 解放処理（unique_ptr が自動で行うため delete は不要）
	
	AudioManager::GetInstance()->Finalize();
#ifdef _DEBUG
	ImGuiManager::GetInstance()->Finalize();
#endif
	CloseHandle(dxCommon->GetFenceEvent());

	winApp->Finalize();
}
