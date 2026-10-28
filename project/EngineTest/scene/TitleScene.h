#pragma once
#include "BaseScene.h"

/// <summary>
/// タイトルシーン（現在は空。再利用のための土台として最小構成にしてある）
/// </summary>
class TitleScene : public BaseScene {
public:
	void Initialize() override;
	void Finalize() override;
	void Update() override;
	void Draw() override;
};
