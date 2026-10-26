#pragma once
#include "Easing.h"
#include "Structs.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class Sprite;
class Object3d;
class Text;

// 繰り返し方
enum class TweenLoop {
    kRestart, // 毎回 最初から
    kYoyo,    // 行って戻る（2回目は逆向き）
};

// イージング関数（Easing::OutCubic などをそのまま渡せる）
using EaseFunction = float (*)(float);

/// <summary>
/// 決めた秒数で値を変える動き1本分（Tweens::To などが作り、Tweens が毎フレーム進める）。
/// 設定は作った直後に続けて書く（例: Tweens::To(&x, 10.0f, 0.5f).SetEase(Easing::OutBack).SetDelay(0.2f);）。
/// 返ってくる参照は終わると消えるので、後で止めたいときは GetHandle() の番号を覚えて Tweens::Kill(handle) を使う。
/// </summary>
class Tween {
public:
    virtual ~Tween() = default;

    Tween& SetEase(EaseFunction newEase) { ease = newEase ? newEase : Easing::Linear; return *this; }
    // 始めるまで待つ秒数
    Tween& SetDelay(float seconds) { delay = seconds; return *this; }
    // 何回再生するか（1 = 1回だけ、-1 = ずっと）と、繰り返し方
    Tween& SetLoops(int count, TweenLoop mode = TweenLoop::kRestart) { loops = count; loopMode = mode; return *this; }
    // true なら Time::SetTimeScale・ヒットストップ・ポーズの影響を受けない（ポーズメニューの UI の動きなど）
    Tween& SetUnscaledTime(bool unscaled = true) { isUnscaled = unscaled; return *this; }
    // 持ち主（その物を消す前に Tweens::Kill(持ち主) でまとめて止める）。Sprite / Object3d / Text の動きは自動で入る
    Tween& SetOwner(const void* newOwner) { owner = newOwner; return *this; }
    // 最後まで行ったときに呼ぶ関数（ずっと繰り返すものは呼ばれない）
    Tween& OnComplete(std::function<void()> callback) { onComplete = std::move(callback); return *this; }

    uint32_t GetHandle() const { return handle; }
    const void* GetOwner() const { return owner; }
    // 1回分の長さ（待つ秒数を除く）
    virtual float GetDuration() const = 0;
    bool IsCompleted() const { return isCompleted; }
    bool IsUnscaled() const { return isUnscaled; }

    // 始めてから time 秒の状態にする（待つ秒数・繰り返しを含む）。Tweens と TweenSequence が呼ぶ
    void Seek(float time);
    // deltaTime 秒進める
    void Advance(float deltaTime) { Seek(elapsed + deltaTime); }
    // 何もしないで終わったことにする（Tweens::Kill）
    void Kill() { isKilled = true; }
    bool IsKilled() const { return isKilled; }
    // 全部を何回も再生する長さ（ずっと繰り返すなら -1）
    float GetTotalDuration() const;

protected:
    // 初めて動き始めたとき（今の値を「始めの値」として覚えるなど）
    virtual void OnStart() {}
    // 1回のうちの進み具合 progress（0〜1。イージング前）の状態にする
    virtual void Apply(float progress) = 0;
    // TweenLoop::kYoyo（逆向きに戻る）ができるか
    virtual bool SupportsYoyo() const { return true; }

    EaseFunction ease = Easing::Linear;

private:
    friend class Tweens;
    uint32_t handle = 0;
    const void* owner = nullptr;
    float delay = 0.0f;
    int loops = 1;
    TweenLoop loopMode = TweenLoop::kRestart;
    bool isUnscaled = false;
    std::function<void()> onComplete;

    float elapsed = 0.0f;
    bool isStarted = false;
    bool isCompleted = false;
    bool isKilled = false;
};

/// <summary>
/// 値1つを from から to へ変える Tween（float / Vector2 / Vector3 / Vector4）。getter があれば、動き始めた時の値を from にする
/// </summary>
template <typename T>
class ValueTween : public Tween {
public:
    ValueTween(std::function<T()> getter, std::function<void(const T&)> setter, const T& to, float duration)
        : getter(std::move(getter)), setter(std::move(setter)), to(to), duration(duration) {}
    ValueTween(const T& from, std::function<void(const T&)> setter, const T& to, float duration)
        : setter(std::move(setter)), from(from), to(to), duration(duration) {}

    float GetDuration() const override { return duration; }

protected:
    void OnStart() override {
        if (getter) {
            from = getter();
        }
    }
    void Apply(float progress) override { setter(Easing::Lerp(from, to, ease(progress))); }

private:
    std::function<T()> getter;
    std::function<void(const T&)> setter;
    T from{};
    T to{};
    float duration = 0.0f;
};

/// <summary>
/// 複数の Tween を順番に・同時に動かす（DOTween の Sequence と同じ考え方）。
///   TweenSequence& seq = Tweens::Sequence();
///   seq.Append(Tweens::Move(sprite, { 400, 300 }, 0.4f).SetEase(Easing::OutBack));  // 動いて
///   seq.Join(Tweens::Fade(sprite, 1.0f, 0.4f));                                    // 同時に表示
///   seq.AppendInterval(0.5f);                                                       // 0.5 秒待って
///   seq.AppendCallback([] { 効果音; });                                             // 何かして
///   seq.Append(Tweens::Scale(sprite, { 0, 0 }, 0.2f));                              // 消える
///   seq.SetLoops(-1);                                                               // ずっと繰り返す（繰り返し方は kRestart だけ）
/// 中に入れた Tween の SetLoops(-1) は 1回として扱う
/// </summary>
class TweenSequence : public Tween {
public:
    // 今までの全部が終わった後に始める
    TweenSequence& Append(Tween& tween);
    // 直前に Append / Join した物と同時に始める
    TweenSequence& Join(Tween& tween);
    // 待つ
    TweenSequence& AppendInterval(float seconds);
    // 今までの全部が終わった時に関数を呼ぶ
    TweenSequence& AppendCallback(std::function<void()> callback);

    float GetDuration() const override { return duration; }

protected:
    void Apply(float progress) override;
    // 中の関数を逆向きに呼べないので、繰り返しは最初からだけ
    bool SupportsYoyo() const override { return false; }

private:
    struct Item {
        std::unique_ptr<Tween> tween;    // 無ければ関数を呼ぶだけ
        std::function<void()> callback;
        float startTime = 0.0f;
    };
    void AddTween(Tween& tween, float startTime);

    std::vector<Item> items;
    float duration = 0.0f;
    float lastStartTime = 0.0f;
    float previousTime = -1.0f; // 関数を呼ぶ判定用（前に Apply した時刻。-1 ならまだ）
};

/// <summary>
/// Tween を作って毎フレーム進める所（Framework がシーンの Update の前に Update を呼ぶ）。
///   Tweens::To(&value, 1.0f, 0.5f);                                     // float* などを直接
///   Tweens::To<Vector3>([&] { return obj->GetTransform().translate; },  // getter / setter で
///                       [&](const Vector3& v) { obj->SetTranslate(v); }, target, 0.5f);
///   Tweens::Move(sprite, { 100, 200 }, 0.3f).SetEase(Easing::OutCubic);  // Sprite / Object3d / Text 用
///   Tweens::Kill(sprite);                                               // その物の動きを全部止める（消す前に）
/// シーンを切り替えると全部止まる。
/// </summary>
class Tweens {
public:
    Tweens() = delete;

    // ===== 作る =====
    // *target を、動き始めた時の値から to へ
    template <typename T>
    static ValueTween<T>& To(T* target, const T& to, float duration) {
        return Register(std::make_unique<ValueTween<T>>([target] { return *target; }, [target](const T& value) { *target = value; }, to,
                                                        duration));
    }
    // getter の値（動き始めた時）から to へ、setter で書き込む
    template <typename T>
    static ValueTween<T>& To(std::function<T()> getter, std::function<void(const T&)> setter, const T& to, float duration) {
        return Register(std::make_unique<ValueTween<T>>(std::move(getter), std::move(setter), to, duration));
    }
    // from から to へ（始めの値を決めて動かす。現れる演出など）
    template <typename T>
    static ValueTween<T>& FromTo(std::function<void(const T&)> setter, const T& from, const T& to, float duration) {
        return Register(std::make_unique<ValueTween<T>>(from, std::move(setter), to, duration));
    }
    // 何も動かさず、秒数だけ数える（OnComplete で「○秒後に〜する」）
    static Tween& Delay(float seconds);
    // 順番・同時に動かす入れ物
    static TweenSequence& Sequence();

    // ===== よく使う物の動き（持ち主はその物になる） =====
    static Tween& Move(Sprite* sprite, const Vector2& to, float duration);
    static Tween& Resize(Sprite* sprite, const Vector2& to, float duration);
    static Tween& Rotate(Sprite* sprite, float to, float duration);
    static Tween& Color(Sprite* sprite, const Vector4& to, float duration);
    static Tween& Fade(Sprite* sprite, float alpha, float duration);
    static Tween& Move(Object3d* object, const Vector3& to, float duration);
    static Tween& Scale(Object3d* object, const Vector3& to, float duration);
    static Tween& Rotate(Object3d* object, const Vector3& to, float duration);
    static Tween& Color(Object3d* object, const Vector4& to, float duration);
    static Tween& Fade(Object3d* object, float alpha, float duration);
    static Tween& Move(Text* text, const Vector2& to, float duration);
    static Tween& Scale(Text* text, float to, float duration);
    static Tween& Color(Text* text, const Vector4& to, float duration);
    static Tween& Fade(Text* text, float alpha, float duration);

    // ===== 止める・調べる =====
    static void Kill(uint32_t handle);
    // 持ち主の動きを全部止める（その物を消す前に呼ぶ。消した後に動きが書き込むと落ちる）
    static void Kill(const void* owner);
    static void KillAll();
    static bool IsActive(uint32_t handle);
    static size_t GetActiveCount() { return tweens.size(); }

    // Framework 専用。全部を進め、終わった物を消す
    static void Update();

private:
    friend class TweenSequence;
    template <typename T>
    static T& Register(std::unique_ptr<T> tween) {
        T& reference = *tween;
        RegisterBase(std::move(tween));
        return reference;
    }
    static void RegisterBase(std::unique_ptr<Tween> tween);
    // TweenSequence に入れる Tween を、毎フレーム進める一覧から外して渡す
    static std::unique_ptr<Tween> Detach(Tween& tween);

    static std::vector<std::unique_ptr<Tween>> tweens;
    static uint32_t nextHandle;
};
