#pragma once
#include "WinApp.h"
#include "DirectXCommon.h"
#include "SrvManager.h"
#include "Input.h"
#include "TextureManager.h"
#include "SpriteCommon.h"
#include "Object3dCommon.h"
#include "ParticleCommon.h"
#include "SkyboxCommon.h"
#include "SceneManager.h"
#include "ImGuiManager.h"
#include "AudioManager.h"

#include <memory>

class Framework {
public:
	virtual ~Framework() = default;

	static Framework* GetInstance() { return instance; }

	// 実行
	void Run();

	// 初期化
	virtual void Initialize();

	// 終了
	virtual void Finalize();

	// 更新
	virtual void Update();

	// 描画（派生クラスで実装）
	virtual void Draw() = 0;

	// 終了フラグのゲッター
	virtual bool IsEndRequest() { return endRequest; }

	// シーン側からのアプリケーション終了リクエスト（次のUpdate後、メインループを抜ける）
	void RequestEnd() { endRequest = true; }

	// 各種ゲッター
	WinApp* GetWinApp() const { return winApp.get(); }
	DirectXCommon* GetDxCommon() const { return dxCommon.get(); }
	SrvManager* GetSrvManager() const { return srvManager.get(); }
	Input* GetInput() const { return input.get(); }
	SpriteCommon* GetSpriteCommon() const { return spriteCommon.get(); }
	Object3dCommon* GetObject3dCommon() const { return object3dCommon.get(); }
	ParticleCommon* GetParticleCommon() const { return particleCommon.get(); }
	SkyboxCommon* GetSkyboxCommon() const { return skyboxCommon.get(); }

protected:
	std::unique_ptr<WinApp> winApp = nullptr;
	std::unique_ptr<DirectXCommon> dxCommon = nullptr;
	std::unique_ptr<SrvManager> srvManager = nullptr;
	std::unique_ptr<Input> input = nullptr;
	
	// これらはゲーム側で使う可能性が高いので protected に配置
	std::unique_ptr<SpriteCommon> spriteCommon = nullptr;
	std::unique_ptr<Object3dCommon> object3dCommon = nullptr;
	std::unique_ptr<ParticleCommon> particleCommon = nullptr;
	std::unique_ptr<SkyboxCommon> skyboxCommon = nullptr;
	std::unique_ptr<SceneManager> sceneManager = nullptr;

	bool endRequest = false;

	static Framework* instance;
};
