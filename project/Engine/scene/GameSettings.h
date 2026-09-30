#pragma once

// シーンをまたいで値を保持するための最小限のプロセス内メモリ保持シングルトン（ファイル永続化はしない）。
// 現状はFPS視点ゲーム向けの項目（マウス感度・目線の高さ）のみ保持。他ジャンルで使う場合は項目自体を見直すこと。
class GameSettings {
public:
	static GameSettings* GetInstance();

	float GetMouseSensitivity() const { return mouseSensitivity; }
	void SetMouseSensitivity(float value) { mouseSensitivity = value; }

	float GetEyeHeight() const { return eyeHeight; }
	void SetEyeHeight(float value) { eyeHeight = value; }

	// UI（SettingsMenu等）の入力範囲として使う想定の定数
	static constexpr float kMouseSensitivityMin = 0.0001f;
	static constexpr float kMouseSensitivityMax = 0.01f;
	static constexpr float kEyeHeightMin = 0.1f;
	static constexpr float kEyeHeightMax = 3.0f;

private:
	GameSettings() = default;
	~GameSettings() = default;
	GameSettings(const GameSettings&) = delete;
	GameSettings& operator=(const GameSettings&) = delete;

	float mouseSensitivity = 0.002f;
	float eyeHeight = 1.5f;

	static GameSettings* instance;
};
