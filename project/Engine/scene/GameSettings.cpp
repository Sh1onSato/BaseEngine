#include "GameSettings.h"

GameSettings* GameSettings::instance = nullptr;

GameSettings* GameSettings::GetInstance() {
	if (instance == nullptr) {
		instance = new GameSettings();
	}
	return instance;
}
