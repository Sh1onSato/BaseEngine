#pragma once
#include "UIPanel.h"
#include <memory>

class Sprite;
class UILayoutGroup;

/// <summary>
/// 縦にスクロールするリスト。中身は GetContent()（縦並び）に入れる。入りきらないと右にスクロールバーが出る。
/// マウスホイール（リストの上にあるとき）・スクロールバーのドラッグ・溝のクリックで動く。
/// キー・パッドで外に隠れている項目を選ぶと、見える位置まで自動でスクロールする。
/// はみ出た項目は描かれず、マウスも受けない。
///
///   UIScrollView* scroll = canvas.Create<UIScrollView>();
///   scroll->SetSize({ 360.0f, 400.0f });
///   for (int i = 0; i < 30; ++i) {
///       scroll->GetContent()->CreateChild<UIButton>(std::format("項目 {}", i))->SetSize({ 0.0f, 40.0f });
///   }
///   scroll->ScrollToBottom();    // 追加した項目を見せるなら
/// </summary>
class UIScrollView : public UIPanel {
public:
    struct Colors {
        Vector4 track = { 0.0f, 0.0f, 0.0f, 0.35f };         // スクロールバーの溝
        Vector4 handle = { 0.55f, 0.58f, 0.65f, 1.0f };      // つまみ
        Vector4 handleActive = { 0.85f, 0.87f, 0.92f, 1.0f }; // つまみ（乗った・ドラッグ中）
    };

    UIScrollView();
    ~UIScrollView() override;

    // 中身を入れる縦並び（幅はリストに合わせて自動。高さは中身に合わせて自動）
    UILayoutGroup* GetContent() const { return content; }

    // 一番上から何 px 下を見ているか（範囲外の値は次の配置の計算で範囲に収まる）
    void SetScrollOffset(float offset) { scrollOffset = offset; }
    float GetScrollOffset() const { return scrollOffset; }
    // 一番下までスクロールしたときの値（中身が入りきっていれば 0）
    float GetMaxScrollOffset() const;
    void ScrollToTop() { scrollOffset = 0.0f; }
    // 項目を足した直後に呼んでもよい（足した項目の大きさが決まってから一番下に合わせる）
    void ScrollToBottom();
    // element（中身の中の部品）が全部見える位置までスクロールする
    void ScrollIntoView(const UIElement* element);

    // ホイール1目盛りで動く量（px）
    void SetWheelStep(float step) { wheelStep = step; }
    void SetScrollbarWidth(float width) { scrollbarWidth = width; }
    void SetColors(const Colors& newColors) { colors = newColors; }

protected:
    void OnInitialize() override;
    void OnUpdate() override;
    void OnDraw() override;
    void OnPointerDown() override;
    void OnArrangeChildren() override;
    bool ControlsChildLayout() const override { return true; }
    bool ClipsChildren() const override { return true; }

private:
    bool IsScrollbarVisible() const { return GetMaxScrollOffset() > 0.0f; }
    // マウスが自分の上にあるか（中の部品の上も含む。中に別のスクロールリストがあれば、そちらが優先）
    bool IsPointerInside() const;
    bool IsDescendant(const UIElement* element) const;
    Rect GetTrackRect() const;
    Rect GetHandleRect() const;

    UILayoutGroup* content = nullptr;
    std::unique_ptr<Sprite> track;
    std::unique_ptr<Sprite> handle;
    Colors colors;

    float scrollOffset = 0.0f;
    float wheelStep = 60.0f;
    float scrollbarWidth = 10.0f;
    // つまみをドラッグしているか と、つまみのどこを掴んだか（つまみの上端からの距離）
    bool isDraggingHandle = false;
    float grabOffset = 0.0f;
    // 前のフレームに選ばれていた部品（変わったときだけ見える位置へスクロールする。比べるだけで中は見ない）
    const UIElement* lastFocused = nullptr;
};
