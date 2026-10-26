#pragma once
#include "UIElement.h"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

class Sprite;
class SpriteCommon;

/// <summary>
/// UI 部品をまとめて管理するクラス。シーンのメンバーに1つ持ち、Update / Draw を毎フレーム呼ぶ。
/// 部品は Create<UIButton>() などで作る（キャンバス直下）。部品の中に入れるなら panel->CreateChild<UIButton>()。
///
/// 使い方の例:
///   UICanvas canvas;                                     // シーンのメンバー
///   UIButton* button = canvas.Create<UIButton>();        // Initialize で
///   button->SetText("はじめる");
///   button->SetAlignment({ 0.5f, 0.5f });                // 画面の中央
///   button->SetSize({ 240.0f, 56.0f });
///   button->SetOnClick([this]() { ... });
///   canvas.SetFocus(button);                             // キー・パッドで最初から選んでおくなら
///   canvas.Update();                                     // Update で
///   canvas.Draw();                                       // Draw で（Sprite と同じく描画キューに積むだけ）
///
/// キー・パッドの操作（アクションマップの名前。まだ登録されていなければキャンバスが下の既定で登録する。BindAction で変えてよい）:
///   UI_Up / UI_Down / UI_Left / UI_Right … 矢印キー・十字キー（左スティックでも動く）。押しっぱなしで繰り返す
///   UI_Submit … Enter・Space・パッド A（選んでいる部品を押す）
///   UI_Cancel … Esc・Backspace・パッド B（IsCancelTriggered / SetOnCancel で受け取る）
///   何も選んでいないときに上下左右を押すと、マウスが乗っている部品か左上の部品から選び始める。
///   ゲーム中の HUD など、矢印キーをゲームに使う画面では SetNavigationEnabled(false) にする。
///
/// 1回の Update の流れ: 配置の計算 → マウス（乗った・押した・クリック）→ キー・パッド（選ぶ・決定・キャンセル）→
///                      配置の計算（クリックで変わった分）→ 各部品の OnUpdate → Destroy された部品の削除
/// ゲーム側のクリック処理は IsPointerOverUI() が false のときだけ行うと、UI の上のクリックがゲームに漏れない。
/// </summary>
class UICanvas {
public:
    // UI は 3D や普通の Sprite より手前に出したいので大きめ（シーン遷移のフェードはさらに手前）
    static constexpr int kDefaultLayer = 1000;

    UICanvas();
    ~UICanvas();
    UICanvas(const UICanvas&) = delete;
    UICanvas& operator=(const UICanvas&) = delete;

    void Update();
    void Draw();

    // キャンバス直下に部品を作る
    template <class T, class... Args>
    T* Create(Args&&... args) { return root->CreateChild<T>(std::forward<Args>(args)...); }

    // 一番上の要素（画面全体の大きさ。Create した部品の親）
    UIElement* GetRoot() const { return root.get(); }
    // 名前で探す（見つからなければ nullptr）
    UIElement* Find(const std::string& name) const;
    // 部品を全部消す（Update の最後に消える。クリックの中から呼んでもよい）
    void Clear();

    // --- マウス ---
    // マウスが UI（マウスを受け止める部品）の上にあるか。モーダル中は画面のどこでも true。
    // ゲーム側のクリックはこれが false のときだけ処理する
    bool IsPointerOverUI() const { return hoveredElement != nullptr || modalRoot != nullptr; }
    // マウスが乗っている部品（無ければ nullptr）
    UIElement* GetHoveredElement() const { return hoveredElement; }

    // --- キー・パッド ---
    // element を選ぶ（nullptr で選ばない）。選べない部品（隠れている・押せない・SetFocusable(false)）なら何もしない
    void SetFocus(UIElement* element);
    UIElement* GetFocusedElement() const { return focusedElement; }
    // false にすると上下左右・決定・キャンセルを受け付けない（マウスは使える）
    void SetNavigationEnabled(bool enabled);
    bool IsNavigationEnabled() const { return isNavigationEnabled; }
    // このフレームの Update でキャンセルが押されたか（ウィンドウを閉じる処理などに使う）
    bool IsCancelTriggered() const { return isCancelTriggered; }
    // キャンセルが押されたときに呼ぶ関数
    void SetOnCancel(std::function<void()> callback) { onCancel = std::move(callback); }
    // 選んでいる部品の枠を出しているか（最後の操作がキー・パッドなら true、マウスを動かすと false）
    bool IsFocusVisible() const { return focusedElement != nullptr && isFocusVisible; }
    // 枠の見た目（色・太さ。太さ 0 で出さない）
    void SetFocusFrame(const Vector4& color, float thickness);

    // --- 文字入力（UITextInput） ---
    // 文字入力欄に打っている最中か（このフレームの途中で終わった場合も true）。
    // true の間はゲーム側のキー操作（WASD・Esc など）を無視する。キャンバスも上下左右・決定・キャンセルを受け付けない
    bool IsTextInputActive() const { return activeTextInput != nullptr || wasTextInputActive; }
    UIElement* GetActiveTextInput() const { return activeTextInput; }
    // element で文字入力を始める（前の入力欄は終わる）。入力中は日本語入力（IME）を OFF にする。UITextInput が呼ぶ
    void BeginTextInput(UIElement* element);
    // 文字入力を終える（入力欄の OnTextInputEnd が呼ばれる）
    void EndTextInput();

    // --- モーダル（ダイアログ） ---
    // element の中だけを操作できるようにする（外の部品はマウスでもキーでも選べない）。nullptr で解除。
    // 中の部品を SetFocus しておくとパッドですぐ操作できる。element が消えると自動で解除される
    void SetModalRoot(UIElement* element);
    UIElement* GetModalRoot() const { return modalRoot; }

    // 描画レイヤー（大きいほど手前）。全部の部品がこのレイヤーで、前後は親子と作った順で決まる
    void SetLayer(int newLayer) { layer = newLayer; }
    int GetLayer() const { return layer; }

    SpriteCommon* GetSpriteCommon() const { return spriteCommon; }
    // Update が呼ばれた回数（「このフレームにクリックされたか」の判定用）
    uint64_t GetUpdateCount() const { return updateCount; }

private:
    friend class UIElement;

    // 部品が作られた・消えるときに呼ばれる（消えた部品を指すポインタを残さない）
    void OnElementAdded(const UIElement* element);
    void OnElementRemoved(const UIElement* element);
    bool IsAlive(const UIElement* element) const { return element && aliveElements.contains(element); }

    void UpdateLayout();
    void UpdatePointer();
    void UpdateNavigation();
    // 入力欄が隠れた・押せなくなった・モーダルの外になったら文字入力を終える
    void UpdateTextInput();
    void SetHoveredElement(UIElement* element);
    // 今押されている向き（無ければ空）。キーと十字キーとスティック
    std::optional<UINavigation> ReadHeldDirection() const;
    void Navigate(UINavigation direction);
    // from から direction の向きで次に選ぶ部品
    UIElement* FindNavigationTarget(UIElement* from, UINavigation direction);
    // 何も選んでいないときに最初に選ぶ部品（マウスが乗っている部品か、一番左上の部品）
    UIElement* FindFirstFocusTarget();
    // 操作できる範囲（モーダル中はその中だけ）
    UIElement* GetInteractiveRoot() const { return modalRoot ? modalRoot : root.get(); }
    static void BindDefaultActions();
    void UpdateFocusFrame();

    SpriteCommon* spriteCommon = nullptr;
    std::unique_ptr<UIElement> root;
    std::unordered_set<const UIElement*> aliveElements;
    int layer = kDefaultLayer;
    uint64_t updateCount = 0;

    UIElement* hoveredElement = nullptr;
    UIElement* pressedElement = nullptr;
    UIElement* focusedElement = nullptr;
    UIElement* modalRoot = nullptr;
    UIElement* activeTextInput = nullptr;
    // このフレームの Update を始めたときに文字入力中だったか
    bool wasTextInputActive = false;

    bool isNavigationEnabled = true;
    bool isFocusVisible = false;
    bool isCancelTriggered = false;
    std::function<void()> onCancel;
    // 押しっぱなしの繰り返し用
    std::optional<UINavigation> heldDirection;
    float repeatTimer = 0.0f;

    // 選んでいる部品の枠（上・下・左・右の4本）
    std::array<std::unique_ptr<Sprite>, 4> focusFrame;
    Vector4 focusFrameColor = { 1.0f, 0.85f, 0.2f, 1.0f };
    float focusFrameThickness = 3.0f;
};
