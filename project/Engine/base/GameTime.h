#pragma once
#include <chrono>
#include <cstdint>

// フレーム時間の管理（Framework::Update()の先頭で毎フレーム1回Tick()される）。
// ゲーム側は Time::GetDeltaTime() を使って「1秒あたりの量 × deltaTime」で動かすこと。
// こうしておけば処理落ちや高リフレッシュレート環境でも動きの速さが変わらない。
class Time {
public:
	Time() = delete;

	// Framework専用。計測の起点をリセットする
	static void Initialize();
	// Framework専用。前フレームからの経過時間を計測する
	static void Tick();

	// timeScaleを掛けた経過秒数（ゲームロジック用。ポーズ中は0になる）
	static float GetDeltaTime() { return deltaTime; }
	// timeScaleの影響を受けない経過秒数（UI演出・ポーズメニュー用）
	static float GetUnscaledDeltaTime() { return unscaledDeltaTime; }
	// 起動からの累計秒数（timeScale適用済み）
	static double GetTotalTime() { return totalTime; }
	// 起動からの累計秒数（timeScale非適用）
	static double GetUnscaledTotalTime() { return unscaledTotalTime; }
	static uint64_t GetFrameCount() { return frameCount; }

	// 時間の進む速さ（1.0=通常、0.0=停止、0.5=スロー）。ヒットストップやポーズに使う
	static float GetTimeScale() { return timeScale; }
	static void SetTimeScale(float scale) { timeScale = scale < 0.0f ? 0.0f : scale; }

	// 1フレームの経過時間の上限（ブレークポイント停止やウィンドウドラッグ後に巨大なdeltaTimeで
	// オブジェクトが壁を突き抜けるのを防ぐ）
	static constexpr float kMaxDeltaTime = 0.1f;

private:
	static std::chrono::steady_clock::time_point previous;
	static float deltaTime;
	static float unscaledDeltaTime;
	static double totalTime;
	static double unscaledTotalTime;
	static float timeScale;
	static uint64_t frameCount;
};
