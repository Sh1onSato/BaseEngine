#pragma once
#include "SaveData.h"
#include <string>

// シーンをまたいで保持し、ファイル（SaveData::GetSaveDirectory() の settings.json）にも保存するゲーム設定。
// 音量はセット時にAudioManagerへ即反映する。
// ジャンル固有の設定値は GetSaveData() 経由で任意のキーとして読み書きできる（項目を増やすたびにこのクラスを変えなくてよい）。
class GameSettings {
public:
	static GameSettings* GetInstance();

	// settings.jsonから読み込み、音量をAudioManagerへ反映する（Framework初期化時に呼ばれる）
	void Load();
	// settings.jsonへ保存する
	bool Save();

	// --- 音量（0.0〜1.0） ---
	float GetMasterVolume() const { return masterVolume; }
	void SetMasterVolume(float value);
	float GetBgmVolume() const { return bgmVolume; }
	void SetBgmVolume(float value);
	float GetSeVolume() const { return seVolume; }
	void SetSeVolume(float value);

	// --- 画面 ---（実際の切り替えはWinApp::SetFullscreen。Frameworkが起動時/終了時に同期する）
	bool GetFullscreen() const { return fullscreen; }
	void SetFullscreen(bool value) { fullscreen = value; }
	// ウィンドウモード時のクライアントサイズ（実際の変更はWinApp::SetClientSize。Frameworkが起動時/終了時に同期する）
	int GetWindowWidth() const { return windowWidth; }
	int GetWindowHeight() const { return windowHeight; }
	void SetWindowSize(int width, int height);

	// --- 視点操作（FPS/TPS向け） ---
	float GetMouseSensitivity() const { return mouseSensitivity; }
	void SetMouseSensitivity(float value) { mouseSensitivity = value; }

	float GetEyeHeight() const { return eyeHeight; }
	void SetEyeHeight(float value) { eyeHeight = value; }

	bool GetInvertY() const { return invertY; }
	void SetInvertY(bool value) { invertY = value; }

	// ゲーム固有の追加設定用（例: GetSaveData().GetInt("difficulty", 1)）。Save()で一緒に保存される
	SaveData& GetSaveData() { return saveData; }

	// UI（SettingsMenu等）の入力範囲として使う想定の定数
	static constexpr float kMouseSensitivityMin = 0.0001f;
	static constexpr float kMouseSensitivityMax = 0.01f;
	static constexpr float kEyeHeightMin = 0.1f;
	static constexpr float kEyeHeightMax = 3.0f;

	static constexpr const char* kFileName = "settings.json";

private:
	GameSettings() = default;
	~GameSettings() = default;
	GameSettings(const GameSettings&) = delete;
	GameSettings& operator=(const GameSettings&) = delete;

	void ApplyVolumes();

	float masterVolume = 1.0f;
	float bgmVolume = 0.8f;
	float seVolume = 1.0f;

	bool fullscreen = false;
	int windowWidth = 1280;
	int windowHeight = 720;
	// 壊れた値が保存されていてもウィンドウが消えないようにする下限
	static constexpr int kMinWindowWidth = 320;
	static constexpr int kMinWindowHeight = 180;

	float mouseSensitivity = 0.002f;
	float eyeHeight = 1.5f;
	bool invertY = false;

	SaveData saveData;

	static GameSettings* instance;
};
