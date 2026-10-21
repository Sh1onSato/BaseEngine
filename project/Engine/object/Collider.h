#pragma once
#include "Collision.h"
#include "Structs.h"
#include <cstdint>
#include <memory>

class GameObject;
class Collider;

// 当たり判定の形
enum class ColliderShape {
    kSphere,  // 3D 球
    kAABB,    // 3D 回転しない箱
    kOBB,     // 3D 持ち主の回転に追従する箱
    kCapsule, // 3D カプセル（キャラクター向き）
    kCircle,  // 2D 円
    kRect,    // 2D 回転しない四角
};

// ぶつかったときの動き方
enum class ColliderType {
    kTrigger, // すり抜ける。通知（OnCollisionEnter など）だけ来る（アイテム・ダメージ範囲など）
    kStatic,  // 動かない。相手だけが押し戻される（壁・床など）
    kDynamic, // 動く。Static からは押し戻され、Dynamic 同士は半分ずつ押し合う（キャラクター・箱など）
};

/// <summary>
/// 当たり判定の通知（GameObject の OnCollisionEnter / Stay / Exit に渡される）。
/// 受け取った側から見た情報になっている（self = 自分のコライダー、other = 相手）。
/// 注意: Exit のとき、相手がもう削除されていれば other / otherCollider は nullptr になる（使う前に確認すること）。
///       また Exit で届いた相手は Destroy 済みのことがある（そのフレームの間はポインタ自体は有効）。
/// </summary>
struct CollisionEvent {
    GameObject* other = nullptr;       // ぶつかった相手
    Collider* self = nullptr;          // 自分のどのコライダーが当たったか
    Collider* otherCollider = nullptr; // 相手のどのコライダーが当たったか
    // 自分を相手から離す向き（正規化済み）。自分の位置に normal * depth を足すとめり込みが解ける。
    // Exit のときは最後に当たっていたフレームの値
    Vector3 normal = { 0.0f, 0.0f, 0.0f };
    float depth = 0.0f; // めり込みの深さ
};

/// <summary>
/// GameObject に付ける当たり判定。MakeSphere / MakeRect などで作り、GameObject::AddCollider() で付ける。
/// 位置と回転は持ち主の transform から毎回計算する（自分で位置を更新する必要はない）。
///
/// 大きさについて: 半径・半辺長などは「そのままの値」を使い、transform.scale は掛けない。
/// （2Dでは scale を使わずスプライトの SetSize で見た目の大きさを決めることが多く、
///   scale を掛けると見た目と判定がずれて分かりにくいため）
///
/// 使い方の例（Initialize の中で）:
///   Collider* collider = AddCollider(Collider::MakeRect({ 48.0f, 48.0f }));
///   collider->SetType(ColliderType::kDynamic);
///   collider->SetCollisionLayer(1);
/// </summary>
class Collider {
public:
    // --- 作成（基本はこちらを使う） ---
    static std::unique_ptr<Collider> MakeSphere(float radius);
    // AABB（回転しない箱）。halfSize は中心から各面までの距離
    static std::unique_ptr<Collider> MakeBox(const Vector3& halfSize);
    // OBB（持ち主の回転に追従する箱）
    static std::unique_ptr<Collider> MakeOrientedBox(const Vector3& halfSize);
    // 持ち主のローカルY方向に伸びるカプセル。height は両端の球の中心間の距離（全体の高さは height + radius * 2）
    static std::unique_ptr<Collider> MakeCapsule(float radius, float height);
    // 2D の円（transform.translate の x, y を中心にする）
    static std::unique_ptr<Collider> MakeCircle(float radius);
    // 2D の四角（回転しない）。size は幅・高さ（スプライトの SetSize と同じ値を渡せばよい）
    static std::unique_ptr<Collider> MakeRect(const Vector2& size);

    // 通常は上の MakeXxx を使うこと（大きさを自分で入れる必要があるため）
    explicit Collider(ColliderShape shape) : shape(shape) {}

    ColliderShape GetShape() const { return shape; }
    // 2D の形（円・四角）なら true。2D と 3D の形同士は判定しない
    bool Is2D() const { return shape == ColliderShape::kCircle || shape == ColliderShape::kRect; }

    // 既定は kDynamic（キャラクターなど、押されて動く物が一番多いため）
    void SetType(ColliderType newType) { type = newType; }
    ColliderType GetType() const { return type; }

    // 衝突レイヤー（0〜31）。どのレイヤー同士が当たるかは CollisionManager::SetLayerCollision で決める
    void SetCollisionLayer(uint32_t layer);
    uint32_t GetCollisionLayer() const { return collisionLayer; }

    // 持ち主の位置からのずれ（OBB・カプセルは持ち主の回転に合わせて回る）
    void SetOffset(const Vector3& newOffset) { offset = newOffset; }
    const Vector3& GetOffset() const { return offset; }

    // false にすると当たり判定から外れる（当たっていた相手には次の判定で Exit が届く）
    void SetEnabled(bool enabled) { isEnabled = enabled; }
    bool IsEnabled() const { return isEnabled; }

    // 持ち主（AddCollider されるまでは nullptr）
    GameObject* GetOwner() const { return owner; }

    // --- ワールド上の形（持ち主の transform から計算）。自分の形と違うものを呼んだときの値は意味を持たない ---
    Sphere GetWorldSphere() const;
    AABB GetWorldAABB() const;
    OBB GetWorldOBB() const;
    Capsule GetWorldCapsule() const;
    Circle GetWorldCircle() const;
    Rect GetWorldRect() const;
    // 形をすっぽり囲む回転しない箱（大まかな判定用）。2D は z = 0 の厚さ0の箱
    AABB GetWorldBounds() const;

private:
    // owner は GameObject::AddCollider だけが設定する
    friend class GameObject;

    // 持ち主の位置 + offset（offset は回転させない）
    Vector3 GetWorldCenter() const;
    // 持ち主の回転に合わせた OBB（中心は offset を回転させて足した位置）
    OBB MakeRotatedBox(const Vector3& boxHalfSize) const;

    GameObject* owner = nullptr;
    ColliderShape shape = ColliderShape::kSphere;
    ColliderType type = ColliderType::kDynamic;
    uint32_t collisionLayer = 0;
    Vector3 offset = { 0.0f, 0.0f, 0.0f };
    bool isEnabled = true;

    // 大きさ（形によって使うものが違う）
    float radius = 0.5f;                       // 球・カプセル・円
    Vector3 halfSize = { 0.5f, 0.5f, 0.5f };   // AABB・OBB
    float height = 1.0f;                       // カプセル
    Vector2 size2D = { 1.0f, 1.0f };           // 四角
};
