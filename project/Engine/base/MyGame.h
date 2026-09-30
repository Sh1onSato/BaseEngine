#pragma once
#include "Framework.h"
#include "SceneFactory.h"
#include <memory>

class MyGame : public Framework {
public:
	// 初期化
	void Initialize() override;

	// 終了
	void Finalize() override;

	// 更新
	void Update() override;

	// 描画
	void Draw() override;

private:
	// シーンファクトリー
	std::unique_ptr<SceneFactory> sceneFactory = nullptr;
};
