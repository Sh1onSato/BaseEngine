#pragma once
#include "UIElement.h"

// 並べる向き
enum class UILayoutDirection {
    kVertical,   // 上から下へ
    kHorizontal, // 左から右へ
};

/// <summary>
/// 子を縦（または横）に自動で並べる部品。自分は何も描かない（背景が欲しければ UIPanel の子にする）。
/// 子の位置（anchor・pivot・position）はこのグループが決めるので、子には SetSize だけすればよい（SetStretch は使わない）。
/// 隠れている子（SetVisible(false)）は詰めて並べる。
///
///   UILayoutGroup* list = window->CreateChild<UILayoutGroup>(UILayoutDirection::kVertical);
///   list->SetStretch(true, true);       // ウィンドウいっぱい
///   list->SetPadding(16.0f);            // 内側の余白
///   list->SetSpacing(8.0f);             // 子と子の間
///   list->SetExpandChildren(true);      // 子の幅をそろえる
///   list->CreateChild<UIButton>("つづきから");
///   list->CreateChild<UIButton>("はじめから");
/// </summary>
class UILayoutGroup : public UIElement {
public:
    explicit UILayoutGroup(UILayoutDirection direction = UILayoutDirection::kVertical);

    void SetDirection(UILayoutDirection newDirection) { direction = newDirection; }
    UILayoutDirection GetDirection() const { return direction; }
    // 子と子の間
    void SetSpacing(float newSpacing) { spacing = newSpacing; }
    // 内側の余白（左・上・右・下）
    void SetPadding(float left, float top, float right, float bottom);
    void SetPadding(float all) { SetPadding(all, all, all, all); }
    // 並べる向きの揃え方（0〜1。縦並びなら 0=上詰め、0.5=中央、1=下詰め）。中身がグループより小さいときに効く
    void SetMainAlignment(float alignment) { mainAlignment = alignment; }
    // 並べる向きと直角の揃え方（0〜1。縦並びなら 0=左、0.5=中央、1=右）
    void SetCrossAlignment(float alignment) { crossAlignment = alignment; }
    // true なら子を並べる向きと直角の方向にいっぱいに広げる（縦並びなら全部の子の幅 = グループの幅）
    void SetExpandChildren(bool expand) { expandChildren = expand; }
    // 自分の大きさを中身に合わせる（mainAxis: 並べる向き、crossAxis: 直角の向き。合わせた向きの size は自動で決まる）
    void SetFitContent(bool mainAxis, bool crossAxis = false);

protected:
    void OnBeforeLayout() override;
    void OnArrangeChildren() override;
    bool ControlsChildLayout() const override { return true; }

private:
    // 縦並びなら (幅, 高さ) を (直角, 並べる向き) に入れ替える
    bool IsVertical() const { return direction == UILayoutDirection::kVertical; }

    UILayoutDirection direction = UILayoutDirection::kVertical;
    float spacing = 8.0f;
    float paddingLeft = 0.0f;
    float paddingTop = 0.0f;
    float paddingRight = 0.0f;
    float paddingBottom = 0.0f;
    float mainAlignment = 0.0f;
    float crossAlignment = 0.0f;
    bool expandChildren = false;
    bool fitMainAxis = false;
    bool fitCrossAxis = false;
};
