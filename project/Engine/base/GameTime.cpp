#include "GameTime.h"

std::chrono::steady_clock::time_point Time::previous{};
float Time::deltaTime = 0.0f;
float Time::unscaledDeltaTime = 0.0f;
double Time::totalTime = 0.0;
double Time::unscaledTotalTime = 0.0;
float Time::timeScale = 1.0f;
uint64_t Time::frameCount = 0;
float Time::hitStopRemaining = 0.0f;
float Time::hitStopScale = 0.0f;

void Time::HitStop(float seconds, float scale) {
	if (hitStopRemaining > 0.0f) {
		// 止まっている途中なら、長い方・遅い方
		hitStopRemaining = seconds > hitStopRemaining ? seconds : hitStopRemaining;
		hitStopScale = scale < hitStopScale ? scale : hitStopScale;
	} else {
		hitStopRemaining = seconds;
		hitStopScale = scale;
	}
	if (hitStopScale < 0.0f) {
		hitStopScale = 0.0f;
	}
}

void Time::Initialize() {
	previous = std::chrono::steady_clock::now();
	deltaTime = 0.0f;
	unscaledDeltaTime = 0.0f;
	totalTime = 0.0;
	unscaledTotalTime = 0.0;
	timeScale = 1.0f;
	frameCount = 0;
	hitStopRemaining = 0.0f;
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
	// ヒットストップ中は、さらに遅くする（このフレームの分を数えてから終わる）
	if (hitStopRemaining > 0.0f) {
		deltaTime *= hitStopScale;
		hitStopRemaining -= elapsed;
	}
	unscaledTotalTime += unscaledDeltaTime;
	totalTime += deltaTime;
	++frameCount;
}
