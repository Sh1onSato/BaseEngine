#include "GameSettings.h"
#include "AudioManager.h"
#include <algorithm>

GameSettings* GameSettings::instance = nullptr;

GameSettings* GameSettings::GetInstance() {
	if (instance == nullptr) {
		instance = new GameSettings();
	}
	return instance;
}

void GameSettings::Load() {
	// ファイルが無い（初回起動）場合は既定値のまま
	saveData.Load(kFileName);

	masterVolume = saveData.GetFloat("masterVolume", masterVolume);
	bgmVolume = saveData.GetFloat("bgmVolume", bgmVolume);
	seVolume = saveData.GetFloat("seVolume", seVolume);
	fullscreen = saveData.GetBool("fullscreen", fullscreen);
	SetWindowSize(saveData.GetInt("windowWidth", windowWidth), saveData.GetInt("windowHeight", windowHeight));
	mouseSensitivity = saveData.GetFloat("mouseSensitivity", mouseSensitivity);
	eyeHeight = saveData.GetFloat("eyeHeight", eyeHeight);
	invertY = saveData.GetBool("invertY", invertY);

	ApplyVolumes();
}

bool GameSettings::Save() {
	saveData.SetFloat("masterVolume", masterVolume);
	saveData.SetFloat("bgmVolume", bgmVolume);
	saveData.SetFloat("seVolume", seVolume);
	saveData.SetBool("fullscreen", fullscreen);
	saveData.SetInt("windowWidth", windowWidth);
	saveData.SetInt("windowHeight", windowHeight);
	saveData.SetFloat("mouseSensitivity", mouseSensitivity);
	saveData.SetFloat("eyeHeight", eyeHeight);
	saveData.SetBool("invertY", invertY);
	return saveData.Save(kFileName);
}

void GameSettings::SetMasterVolume(float value) {
	masterVolume = (std::clamp)(value, 0.0f, 1.0f);
	ApplyVolumes();
}

void GameSettings::SetBgmVolume(float value) {
	bgmVolume = (std::clamp)(value, 0.0f, 1.0f);
	ApplyVolumes();
}

void GameSettings::SetSeVolume(float value) {
	seVolume = (std::clamp)(value, 0.0f, 1.0f);
	ApplyVolumes();
}

void GameSettings::SetWindowSize(int width, int height) {
	windowWidth = (std::max)(width, kMinWindowWidth);
	windowHeight = (std::max)(height, kMinWindowHeight);
}

void GameSettings::ApplyVolumes() {
	AudioManager* audio = AudioManager::GetInstance();
	audio->SetMasterVolume(masterVolume);
	audio->SetCategoryVolume(AudioCategory::kBGM, bgmVolume);
	audio->SetCategoryVolume(AudioCategory::kSE, seVolume);
}
