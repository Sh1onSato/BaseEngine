#include "GameTime.h"

std::chrono::steady_clock::time_point Time::previous{};
float Time::deltaTime = 0.0f;
float Time::unscaledDeltaTime = 0.0f;
double Time::totalTime = 0.0;
double Time::unscaledTotalTime = 0.0;
float Time::timeScale = 1.0f;
uint64_t Time::frameCount = 0;

void Time::Initialize() {
	previous = std::chrono::steady_clock::now();
	deltaTime = 0.0f;
	unscaledDeltaTime = 0.0f;
	totalTime = 0.0;
	unscaledTotalTime = 0.0;
	timeScale = 1.0f;
	frameCount = 0;
}

void Time::Tick() {
	std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	float elapsed = std::chrono::duration<float>(now - previous).count();
	previous = now;

	if (elapsed > kMaxDeltaTime) {
		elapsed = kMaxDeltaTime;
	}

	unscaledDeltaTime = elapsed;
	deltaTime = elapsed * timeScale;
	unscaledTotalTime += unscaledDeltaTime;
	totalTime += deltaTime;
	++frameCount;
}
