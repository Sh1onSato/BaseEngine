#pragma once
#include "UIElement.h"
#include <memory>

class Sprite;

// 進捗バーが伸びる向き
enum class UIFillDirection {
    kLeftToRight,
    kRightToLeft,
    kBottomToTop,
    kTopToBottom,
};

/// <summary>
/// 割合を棒の長さで表す部品（HP・スタミナ・読み込み・経験値など）。表示だけでマウスは受け止めない。
/// SetTrailSpeed を設定すると、減ったときに少し遅れて追いかける2本目の棒が出る（格闘ゲームの体力のような演出）。
///
///   UIProgressBar* hpBar = hud->CreateChild<UIProgressBar>();
///   hpBar->SetSize({ 300.0f, 20.0f });
///   hpBar->SetValue(player->GetHp(), player->GetMaxHp());   // 毎フレーム呼んでよい
///   hpBar->SetTrailSpeed(0.5f);                               // 1秒で 50% 分追いつく
/// </summary>
class UIProgressBar : public UIElement {
public:
    struct Colors {
        Vector4 background = { 0.08f, 0.08f, 0.10f, 0.9f };
        Vector4 fill = { 0.30f, 0.85f, 0.40f, 1.0f };
        Vector4 trail = { 0.95f, 0.85f, 0.30f, 1.0f }; // 減ったときに遅れて追いかける棒
    };

    UIProgressBar();
    ~UIProgressBar() override;

    // 0〜1
    void SetRatio(float newRatio);
    float GetRatio() const { return ratio; }
    // current / maxValue を割合にする（maxValue が 0 以下なら 0）
    void SetValue(float current, float maxValue);

    void SetFillDirection(UIFillDirection newDirection) { fillDirection = newDirection; }
    // 遅れて追いかける棒の速さ（1秒あたりの割合。0 で出さない）。時間はゲームの時間（スローの影響を受ける）
    void SetTrailSpeed(float ratioPerSecond) { trailSpeed = ratioPerSecond; }
    void SetColors(const Colors& newColors) { colors = newColors; }

protected:
    void OnInitialize() override;
    void OnUpdate() override;
    void OnDraw() override;

private:
    // 割合 amount の棒を置く
    void PlaceBar(Sprite* sprite, float amount, const Vector4& color);

    std::unique_ptr<Sprite> background;
    std::unique_ptr<Sprite> trail;
    std::unique_ptr<Sprite> fill;
    Colors colors;
    UIFillDirection fillDirection = UIFillDirection::kLeftToRight;
    float ratio = 1.0f;
    float trailRatio = 1.0f;
    float trailSpeed = 0.0f;
};
