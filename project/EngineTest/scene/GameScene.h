#pragma once
#include "BaseScene.h"

/// <summary>
/// ゲーム本編シーン（現在は空。再利用のための土台として最小構成にしてある）
/// </summary>
class GameScene : public BaseScene {
public:
	// 初期化
	void Initialize() override;
	// 終了処理
	void Finalize() override;
	// 更新
	void Update() override;
	// 描画
	void Draw() override;
};
