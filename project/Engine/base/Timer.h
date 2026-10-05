#pragma once

// 秒単位のカウントダウンタイマー（クールダウン・無敵時間・演出の進行度などに使う）。
// 例:
//   Timer cooldown(0.5f);
//   cooldown.Update(Time::GetDeltaTime());
//   if (input->TriggerAction("Shot") && cooldown.IsFinished()) { Fire(); cooldown.Start(); }
class Timer {
public:
    Timer() = default;
    explicit Timer(float duration) : duration(duration), elapsed(duration) {}

    // 最初から計測し直す（durationを省略すると前回と同じ長さ）
    void Start() { elapsed = 0.0f; isRunning = true; }
    void Start(float newDuration) { duration = newDuration; Start(); }
    void Stop() { isRunning = false; }
    // 終了状態にする（IsFinished()がtrueになる）
    void Finish() { elapsed = duration; isRunning = false; }

    void Update(float deltaTime) {
        if (!isRunning) return;
        elapsed += deltaTime;
        if (elapsed >= duration) {
            elapsed = duration;
            isRunning = false;
            justFinished = true;
        }
    }

    bool IsRunning() const { return isRunning; }
    bool IsFinished() const { return !isRunning && elapsed >= duration; }
    // 終了した瞬間（そのフレームの1回だけtrue）。呼ぶとリセットされる
    bool ConsumeFinished() { bool result = justFinished; justFinished = false; return result; }

    // 経過率 0〜1（Easingと組み合わせて演出に使う）
    float GetProgress() const { return duration > 0.0f ? elapsed / duration : 1.0f; }
    float GetElapsed() const { return elapsed; }
    float GetRemaining() const { return duration - elapsed; }
    float GetDuration() const { return duration; }

private:
    float duration = 0.0f;
    float elapsed = 0.0f;
    bool isRunning = false;
    bool justFinished = false;
};
