#pragma once
#include "WinApp.h"
#include "DirectXCommon.h"
#include "SrvManager.h"
#include "RenderScaler.h"
#include "Input.h"
#include "TextureManager.h"
#include "SpriteCommon.h"
#include "Object3dCommon.h"
#include "ParticleCommon.h"
#include "SkyboxCommon.h"
#include "DebugDraw.h"
#include "SceneManager.h"
#include "ImGuiManager.h"
#include "AudioManager.h"
#include "GameTime.h"

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
	SceneManager* GetSceneManager() const { return sceneManager.get(); }
	RenderScaler* GetRenderScaler() const { return renderScaler.get(); }
	PostProcess* GetPostProcess() const { return postProcess.get(); }
	// Debug の [F6] で PostProcess の調整の窓を開いているか（テストシーンが自動で値を変えないようにするため）
	bool IsPostProcessUIVisible() const { return isPostProcessUIVisible; }

protected:
	std::unique_ptr<WinApp> winApp = nullptr;
	std::unique_ptr<DirectXCommon> dxCommon = nullptr;
	std::unique_ptr<SrvManager> srvManager = nullptr;
	// 内部解像度スケール（3D を小さい解像度で描いて引き伸ばす）
	std::unique_ptr<RenderScaler> renderScaler = nullptr;
	// 画面の効果（ブルーム・色の調整など。RenderScaler が使う）
	std::unique_ptr<PostProcess> postProcess = nullptr;
	// Debug の [F6] で PostProcess の調整用の窓を出しているか
	bool isPostProcessUIVisible = false;
	std::unique_ptr<Input> input = nullptr;
	
	// これらはゲーム側で使う可能性が高いので protected に配置
	std::unique_ptr<SpriteCommon> spriteCommon = nullptr;
	std::unique_ptr<Object3dCommon> object3dCommon = nullptr;
	std::unique_ptr<ParticleCommon> particleCommon = nullptr;
	std::unique_ptr<SkyboxCommon> skyboxCommon = nullptr;
	// 確認用の線（DebugDraw::Line などの静的関数から使う）
	std::unique_ptr<DebugDraw> debugDraw = nullptr;
	std::unique_ptr<SceneManager> sceneManager = nullptr;

	bool endRequest = false;

	static Framework* instance;
};
