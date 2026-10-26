#include "Tween.h"
#include "GameTime.h"
#include "Logger.h"
#include "Object3d.h"
#include "Sprite.h"
#include "Text.h"
#include <algorithm>
#include <cmath>
#include <limits>

std::vector<std::unique_ptr<Tween>> Tweens::tweens;
uint32_t Tweens::nextHandle = 1;

namespace {
    // 秒数だけ数える Tween（Tweens::Delay）
    class DelayTween : public Tween {
    public:
        explicit DelayTween(float duration) : duration(duration) {}
        float GetDuration() const override { return duration; }

    protected:
        void Apply(float) override {}

    private:
        float duration = 0.0f;
    };

    // Tweens::Update の途中か（途中で KillAll されたら、消すのは終わってから）
    bool isUpdating = false;
}

// ===== Tween =====

float Tween::GetTotalDuration() const {
    if (loops < 0) {
        return -1.0f;
    }
    return delay + GetDuration() * static_cast<float>((std::max)(loops, 1));
}

void Tween::Seek(float time) {
    if (isKilled) {
        return;
    }
    elapsed = time;
    const float local = time - delay;
    if (local < 0.0f) {
        // まだ始まっていない（TweenSequence が繰り返しで最初に戻した時もここ。値は書き換えない）
        isCompleted = false;
        return;
    }
    if (!isStarted) {
        isStarted = true;
        OnStart();
    }
    // 行って戻るを最後に終える時は、回数が偶数なら始めの値で終わる
    const int plays = loops < 0 ? -1 : (std::max)(loops, 1);
    const bool isYoyo = loopMode == TweenLoop::kYoyo && SupportsYoyo();
    const float finalProgress = (isYoyo && plays > 0 && (plays - 1) % 2 == 1) ? 0.0f : 1.0f;
    const float duration = GetDuration();
    const float total = plays < 0 ? (std::numeric_limits<float>::max)() : duration * static_cast<float>(plays);
    if (duration <= 0.0f || local >= total) {
        if (!isCompleted && plays > 0) {
            Apply(finalProgress);
            isCompleted = true;
            if (onComplete) {
                onComplete();
            }
        } else if (plays < 0) {
            Apply(1.0f); // 長さ 0 をずっと繰り返す（意味は無いが止まらないように）
        }
        return;
    }
    isCompleted = false;
    const float loopIndex = std::floor(local / duration);
    float progress = (local - loopIndex * duration) / duration;
    if (isYoyo && static_cast<int64_t>(loopIndex) % 2 == 1) {
        progress = 1.0f - progress;
    }
    Apply(progress);
}

// ===== TweenSequence =====

void TweenSequence::AddTween(Tween& tween, float startTime) {
    std::unique_ptr<Tween> owned = Tweens::Detach(tween);
    if (!owned) {
        Logger::Log("TweenSequence: Tweens で作った物ではない（または既に入れた）Tween は入れられません\n");
        return;
    }
    if (owned->GetTotalDuration() < 0.0f) {
        Logger::Log("TweenSequence: ずっと繰り返す Tween は 1 回として扱います\n");
        owned->SetLoops(1);
    }
    lastStartTime = startTime;
    duration = (std::max)(duration, startTime + owned->GetTotalDuration());
    items.push_back({ std::move(owned), nullptr, startTime });
}

TweenSequence& TweenSequence::Append(Tween& tween) {
    AddTween(tween, duration);
    return *this;
}

TweenSequence& TweenSequence::Join(Tween& tween) {
    AddTween(tween, lastStartTime);
    return *this;
}

TweenSequence& TweenSequence::AppendInterval(float seconds) {
    duration += (std::max)(seconds, 0.0f);
    return *this;
}

TweenSequence& TweenSequence::AppendCallback(std::function<void()> callback) {
    items.push_back({ nullptr, std::move(callback), duration });
    return *this;
}

void TweenSequence::Apply(float progress) {
    const float now = progress * duration;
    auto run = [&](float previous, float current) {
        for (Item& item : items) {
            if (item.tween) {
                item.tween->Seek(current - item.startTime);
            } else if (item.callback && item.startTime > previous && item.startTime <= current) {
                item.callback();
            }
        }
    };
    if (previousTime >= 0.0f && now < previousTime) {
        // 繰り返しで最初に戻った: 前の回を最後まで終わらせてから（飛ばした物の最後の値・関数を取りこぼさない）
        run(previousTime, duration);
        previousTime = -1.0f;
    }
    run(previousTime, now);
    previousTime = now;
}

// ===== Tweens =====

void Tweens::RegisterBase(std::unique_ptr<Tween> tween) {
    tween->handle = nextHandle++;
    if (nextHandle == 0) {
        nextHandle = 1;
    }
    tweens.push_back(std::move(tween));
}

std::unique_ptr<Tween> Tweens::Detach(Tween& tween) {
    auto it = std::find_if(tweens.begin(), tweens.end(), [&](const std::unique_ptr<Tween>& item) { return item.get() == &tween; });
    if (it == tweens.end()) {
        return nullptr;
    }
    std::unique_ptr<Tween> owned = std::move(*it);
    tweens.erase(it);
    return owned;
}

Tween& Tweens::Delay(float seconds) {
    return Register(std::make_unique<DelayTween>(seconds));
}

TweenSequence& Tweens::Sequence() {
    return Register(std::make_unique<TweenSequence>());
}

void Tweens::Kill(uint32_t handle) {
    for (const std::unique_ptr<Tween>& tween : tweens) {
        if (tween->handle == handle) {
            tween->Kill();
        }
    }
}

void Tweens::Kill(const void* owner) {
    if (!owner) {
        return;
    }
    for (const std::unique_ptr<Tween>& tween : tweens) {
        if (tween->owner == owner) {
            tween->Kill();
        }
    }
}

void Tweens::KillAll() {
    for (const std::unique_ptr<Tween>& tween : tweens) {
        tween->Kill();
    }
    if (!isUpdating) {
        tweens.clear();
    }
}

bool Tweens::IsActive(uint32_t handle) {
    return std::any_of(tweens.begin(), tweens.end(), [&](const std::unique_ptr<Tween>& tween) {
        return tween->handle == handle && !tween->IsKilled() && !tween->IsCompleted();
    });
}

void Tweens::Update() {
    isUpdating = true;
    // 途中で作られた物（OnComplete の中で次の動きを作るなど）は次のフレームから動かす
    const size_t count = tweens.size();
    for (size_t i = 0; i < count; ++i) {
        Tween& tween = *tweens[i];
        if (!tween.IsKilled()) {
            tween.Advance(tween.IsUnscaled() ? Time::GetUnscaledDeltaTime() : Time::GetDeltaTime());
        }
    }
    isUpdating = false;
    std::erase_if(tweens, [](const std::unique_ptr<Tween>& tween) { return tween->IsKilled() || tween->IsCompleted(); });
}

// ===== よく使う物の動き =====

Tween& Tweens::Move(Sprite* sprite, const Vector2& to, float duration) {
    return To<Vector2>([sprite] { return sprite->GetPosition(); }, [sprite](const Vector2& value) { sprite->SetPosition(value); }, to, duration)
        .SetOwner(sprite);
}

Tween& Tweens::Resize(Sprite* sprite, const Vector2& to, float duration) {
    return To<Vector2>([sprite] { return sprite->GetSize(); }, [sprite](const Vector2& value) { sprite->SetSize(value); }, to, duration)
        .SetOwner(sprite);
}

Tween& Tweens::Rotate(Sprite* sprite, float to, float duration) {
    return To<float>([sprite] { return sprite->GetRotation(); }, [sprite](const float& value) { sprite->SetRotation(value); }, to, duration)
        .SetOwner(sprite);
}

Tween& Tweens::Color(Sprite* sprite, const Vector4& to, float duration) {
    return To<Vector4>([sprite] { return sprite->GetColor(); }, [sprite](const Vector4& value) { sprite->SetColor(value); }, to, duration)
        .SetOwner(sprite);
}

Tween& Tweens::Fade(Sprite* sprite, float alpha, float duration) {
    return To<float>([sprite] { return sprite->GetColor().w; },
                     [sprite](const float& value) {
                         Vector4 color = sprite->GetColor();
                         color.w = value;
                         sprite->SetColor(color);
                     },
                     alpha, duration)
        .SetOwner(sprite);
}

Tween& Tweens::Move(Object3d* object, const Vector3& to, float duration) {
    return To<Vector3>([object] { return object->GetTransform().translate; },
                       [object](const Vector3& value) { object->SetTranslate(value); }, to, duration)
        .SetOwner(object);
}

Tween& Tweens::Scale(Object3d* object, const Vector3& to, float duration) {
    return To<Vector3>([object] { return object->GetTransform().scale; }, [object](const Vector3& value) { object->SetScale(value); },
                       to, duration)
        .SetOwner(object);
}

Tween& Tweens::Rotate(Object3d* object, const Vector3& to, float duration) {
    return To<Vector3>([object] { return object->GetTransform().rotate; }, [object](const Vector3& value) { object->SetRotate(value); },
                       to, duration)
        .SetOwner(object);
}

Tween& Tweens::Color(Object3d* object, const Vector4& to, float duration) {
    return To<Vector4>([object] { return object->GetColor(); }, [object](const Vector4& value) { object->SetColor(value); }, to, duration)
        .SetOwner(object);
}

Tween& Tweens::Fade(Object3d* object, float alpha, float duration) {
    return To<float>([object] { return object->GetColor().w; },
                     [object](const float& value) {
                         Vector4 color = object->GetColor();
                         color.w = value;
                         object->SetColor(color);
                     },
                     alpha, duration)
        .SetOwner(object);
}

Tween& Tweens::Move(Text* text, const Vector2& to, float duration) {
    return To<Vector2>([text] { return text->GetPosition(); }, [text](const Vector2& value) { text->SetPosition(value); }, to, duration)
        .SetOwner(text);
}

Tween& Tweens::Scale(Text* text, float to, float duration) {
    return To<float>([text] { return text->GetScale(); }, [text](const float& value) { text->SetScale(value); }, to, duration)
        .SetOwner(text);
}

Tween& Tweens::Color(Text* text, const Vector4& to, float duration) {
    return To<Vector4>([text] { return text->GetColor(); }, [text](const Vector4& value) { text->SetColor(value); }, to, duration)
        .SetOwner(text);
}

Tween& Tweens::Fade(Text* text, float alpha, float duration) {
    return To<float>([text] { return text->GetColor().w; },
                     [text](const float& value) {
                         Vector4 color = text->GetColor();
                         color.w = value;
                         text->SetColor(color);
                     },
                     alpha, duration)
        .SetOwner(text);
}
