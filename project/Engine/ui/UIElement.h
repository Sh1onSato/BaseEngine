#pragma once
#include "Collision.h"
#include "Structs.h"
#include <array>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

class UICanvas;
class SpriteCommon;

// キー・パッドで選ぶ部品を動かす向き
enum class UINavigation {
    kUp,
    kDown,
    kLeft,
    kRight,
};

/// <summary>
/// UI 部品（パネル・文字・ボタンなど）の基底クラス。UICanvas::Create か UIElement::CreateChild で作る（new しない）。
///
/// 位置の決め方（座標はゲーム座標 = Screen::GetGameSize() の範囲。左上が 0,0）:
///   親の矩形の anchor の点（0〜1。{0,0}=左上、{1,1}=右下）から position だけずらした点に、
///   自分の pivot の点（0〜1）を合わせる。大きさは size。
///   例: 画面の右下から 20px 内側 → SetAlignment({ 1, 1 }); SetPosition({ -20, -20 });
///       親の中央              → SetAlignment({ 0.5f, 0.5f });
///   SetStretch で親いっぱいに広げることもできる（そのとき size と anchor は使わず、margin だけ内側に縮める）。
///
/// 描画は親 → 子の順（子が手前）。兄弟は作った順（後に作った物が手前）。マウスも手前の物が先に受け取る。
/// </summary>
class UIElement {
public:
    UIElement() = default;
    virtual ~UIElement();
    UIElement(const UIElement&) = delete;
    UIElement& operator=(const UIElement&) = delete;

    // --- 親子 ---
    // 子を作る（この要素より手前に描かれ、この要素と一緒に動く・隠れる・消える）
    template <class T, class... Args>
    T* CreateChild(Args&&... args);
    // 親を変える（nullptr でキャンバス直下）。自分や自分の子孫は親にできない
    void SetParent(UIElement* newParent);
    UIElement* GetParent() const { return parent; }
    // 子の一覧（SetParent で抜けた跡の nullptr が入っていることがあるので確認して使う）
    const std::vector<std::unique_ptr<UIElement>>& GetChildren() const { return children; }

    // 子ごと削除する（実際に消えるのはキャンバスの Update の最後。それまでポインタは有効）
    void Destroy() { isDestroyed = true; }
    bool IsDestroyed() const { return isDestroyed; }

    // --- 配置 ---
    void SetAnchor(const Vector2& newAnchor) { anchor = newAnchor; }
    const Vector2& GetAnchor() const { return anchor; }
    void SetPivot(const Vector2& newPivot) { pivot = newPivot; }
    const Vector2& GetPivot() const { return pivot; }
    // anchor と pivot を同じ値にする（{1,1} なら親の右下に内側から合わせる。よく使うのはこちら）
    void SetAlignment(const Vector2& value);
    void SetPosition(const Vector2& newPosition) { position = newPosition; }
    const Vector2& GetPosition() const { return position; }
    void SetSize(const Vector2& newSize) { size = newSize; }
    const Vector2& GetSize() const { return size; }
    // 親の幅（x）・高さ（y）いっぱいに広げる。広げた向きは size・anchor・position を使わない
    void SetStretch(bool x, bool y) { stretchX = x; stretchY = y; }
    // 広げたときに親の端から空ける量（左・上・右・下）
    void SetMargins(float left, float top, float right, float bottom);
    void SetMargin(float all) { SetMargins(all, all, all, all); }

    // 画面上の矩形（ゲーム座標。キャンバスの Update で計算される）
    const Rect& GetWorldRect() const { return worldRect; }
    Vector2 GetWorldSize() const { return { worldRect.max.x - worldRect.min.x, worldRect.max.y - worldRect.min.y }; }
    Vector2 GetWorldCenter() const;
    bool ContainsPoint(const Vector2& point) const;

    // --- 状態 ---
    void SetName(const std::string& newName) { name = newName; }
    const std::string& GetName() const { return name; }
    // false にすると自分と子が表示されず、マウスも受けない
    void SetVisible(bool visible) { isVisible = visible; }
    bool IsVisible() const { return isVisible; }
    // 自分と親が全部表示中なら true
    bool IsVisibleInHierarchy() const;
    // false にすると押せなくなる（ボタンは灰色になる）。子にも効く
    void SetInteractable(bool interactable) { isInteractable = interactable; }
    bool IsInteractable() const { return isInteractable; }
    bool IsInteractableInHierarchy() const;
    // true ならマウスを受け止める（後ろの UI やゲームにクリックが通らない）。パネル・ボタンは true、文字は false
    void SetBlocksPointer(bool blocks) { blocksPointer = blocks; }
    bool GetBlocksPointer() const { return blocksPointer; }

    // マウスが乗っているか / 押されているか（キャンバスが設定する）
    bool IsHovered() const { return isHovered; }
    bool IsPressed() const { return isPressed; }

    // --- キー・パッドでの操作 ---
    // true ならキー・パッドの上下左右で選べる（ボタンは true）
    void SetFocusable(bool focusable) { isFocusable = focusable; }
    bool IsFocusable() const { return isFocusable; }
    // キー・パッドで選ばれているか（選ぶのは UICanvas::SetFocus か上下左右キー）
    bool IsFocused() const { return isFocused; }
    // direction を押したときに選ぶ部品を自分で決める（nullptr で自動 = その向きで一番近い部品）
    void SetNavigation(UINavigation direction, UIElement* target) { navigation[static_cast<size_t>(direction)] = target; }
    UIElement* GetNavigation(UINavigation direction) const { return navigation[static_cast<size_t>(direction)]; }

    UICanvas* GetCanvas() const { return canvas; }

protected:
    // --- 派生クラスが書く部分 ---
    // 作られた直後（GetCanvas / GetSpriteCommon が使える。Sprite・Text はここで作る）
    virtual void OnInitialize() {}
    // 毎フレーム（配置の計算の後。GetWorldRect に合わせて Sprite・Text を置き、Update を呼ぶ）
    virtual void OnUpdate() {}
    // 描画（Sprite・Text の Draw。子はこの後に描かれる）
    virtual void OnDraw() {}
    // マウス
    virtual void OnPointerEnter() {}
    virtual void OnPointerExit() {}
    virtual void OnPointerDown() {}
    virtual void OnPointerUp() {}
    // 押して、同じ要素の上で離した（IsInteractableInHierarchy のときだけ）
    virtual void OnClick() {}
    // キー・パッド
    virtual void OnFocus() {}
    virtual void OnBlur() {}
    // 決定（Enter・Space・パッド A）が押された。既定はクリックと同じ
    virtual void OnSubmit() { OnClick(); }
    // 選ばれているときに上下左右が押された。true を返すと選ぶ部品を動かさない（スライダーの左右など）
    virtual bool OnNavigate(UINavigation /*direction*/) { return false; }
    // UICanvas::EndTextInput で文字入力が終わった（外をクリックした・ほかの部品が入力を始めた・隠れた など）
    virtual void OnTextInputEnd() {}
    // 配置の計算（並べる部品用）。OnBeforeLayout は自分の矩形を計算する前（子の大きさから自分の size を決める）、
    // OnArrangeChildren は自分の矩形を計算した後・子の矩形を計算する前（子の position・size を決める）
    virtual void OnBeforeLayout() {}
    virtual void OnArrangeChildren() {}
    // 子の位置を自分で決める部品なら true（子の大きさが変わったら自分から計算し直す必要がある）
    virtual bool ControlsChildLayout() const { return false; }
    // true なら子を自分の矩形の中だけに描く（はみ出た部分は描かれず、マウスも受けない。スクロールリスト用）
    virtual bool ClipsChildren() const { return false; }

    SpriteCommon* GetSpriteCommon() const;
    // Sprite・Text に設定する描画レイヤー（キャンバスのレイヤー）
    int GetDrawLayer() const;
    // OnUpdate の中で自分の size などを変えたとき、このフレームのうちに矩形を計算し直す（子も）。
    // 親が並べる部品（UILayoutGroup）なら、親から計算し直す
    void RecalculateLayout();

private:
    friend class UICanvas;

    // 子として登録して OnInitialize を呼ぶ
    void AttachChild(std::unique_ptr<UIElement> child);
    // 親から自分を取り外す（unique_ptr を受け取る。親の一覧には nullptr が残る）
    std::unique_ptr<UIElement> DetachFromParent();
    bool IsAncestorOf(const UIElement* element) const;

    // キャンバスが呼ぶ。Measure（子から順に OnBeforeLayout）→ UpdateLayout（親から順に矩形を計算）の順
    void Measure();
    void UpdateLayout(const Rect& parentRect);
    void UpdateTree();
    void DrawTree();
    // point の上にある一番手前の「マウスを受け止める」要素（無ければ nullptr）
    UIElement* HitTest(const Vector2& point);
    // Destroy された子と、SetParent で抜けた跡（nullptr）を一覧から消す
    void RemoveDestroyedChildren();
    UIElement* FindRecursive(const std::string& targetName);
    // キー・パッドで選べる部品（表示中・押せる・Destroy されていない）を集める
    void CollectFocusable(std::vector<UIElement*>& out);
    // 今キー・パッドで選べる状態か
    bool CanFocus() const;

    UICanvas* canvas = nullptr;
    UIElement* parent = nullptr;
    std::vector<std::unique_ptr<UIElement>> children;

    std::string name;
    Vector2 anchor = { 0.0f, 0.0f };
    Vector2 pivot = { 0.0f, 0.0f };
    Vector2 position = { 0.0f, 0.0f };
    Vector2 size = { 100.0f, 100.0f };
    bool stretchX = false;
    bool stretchY = false;
    float marginLeft = 0.0f;
    float marginTop = 0.0f;
    float marginRight = 0.0f;
    float marginBottom = 0.0f;
    Rect worldRect{ { 0.0f, 0.0f }, { 0.0f, 0.0f } };

    bool isVisible = true;
    bool isInteractable = true;
    bool blocksPointer = false;
    bool isDestroyed = false;
    bool isHovered = false;
    bool isPressed = false;
    bool isFocusable = false;
    bool isFocused = false;
    // 上下左右で次に選ぶ部品（UINavigation の順。消えた部品はキャンバスが使う前に確認する）
    std::array<UIElement*, 4> navigation{};
};

template <class T, class... Args>
T* UIElement::CreateChild(Args&&... args) {
    static_assert(std::is_base_of_v<UIElement, T>, "UIElement を継承したクラスを指定してください");
    std::unique_ptr<T> element = std::make_unique<T>(std::forward<Args>(args)...);
    T* raw = element.get();
    AttachChild(std::move(element));
    return raw;
}
