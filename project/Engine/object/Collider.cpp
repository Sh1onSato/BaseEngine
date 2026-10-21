#include "Collider.h"
#include "GameObject.h"
#include <algorithm>
#include <cmath>

namespace {
    constexpr uint32_t kMaxCollisionLayer = 31;

    // 持ち主がまだいないときに使う transform（原点・回転なし）
    const Transform kDefaultTransform{ { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };

    // 2点を両方含む最小の箱
    AABB MakeBoundsOfPoints(const Vector3& a, const Vector3& b) {
        return {
            { (std::min)(a.x, b.x), (std::min)(a.y, b.y), (std::min)(a.z, b.z) },
            { (std::max)(a.x, b.x), (std::max)(a.y, b.y), (std::max)(a.z, b.z) },
        };
    }

    // OBB をすっぽり囲む AABB（各ワールド軸への投影の半分の長さを足し合わせる）
    AABB MakeBoundsOfOBB(const OBB& obb) {
        const float halves[3] = { obb.halfSize.x, obb.halfSize.y, obb.halfSize.z };
        Vector3 extent = { 0.0f, 0.0f, 0.0f };
        for (int i = 0; i < 3; ++i) {
            extent.x += std::fabs(obb.axes[i].x) * halves[i];
            extent.y += std::fabs(obb.axes[i].y) * halves[i];
            extent.z += std::fabs(obb.axes[i].z) * halves[i];
        }
        return { obb.center - extent, obb.center + extent };
    }
}

// ===== 作成 =====

std::unique_ptr<Collider> Collider::MakeSphere(float radius) {
    std::unique_ptr<Collider> collider = std::make_unique<Collider>(ColliderShape::kSphere);
    collider->radius = radius;
    return collider;
}

std::unique_ptr<Collider> Collider::MakeBox(const Vector3& halfSize) {
    std::unique_ptr<Collider> collider = std::make_unique<Collider>(ColliderShape::kAABB);
    collider->halfSize = halfSize;
    return collider;
}

std::unique_ptr<Collider> Collider::MakeOrientedBox(const Vector3& halfSize) {
    std::unique_ptr<Collider> collider = std::make_unique<Collider>(ColliderShape::kOBB);
    collider->halfSize = halfSize;
    return collider;
}

std::unique_ptr<Collider> Collider::MakeCapsule(float radius, float height) {
    std::unique_ptr<Collider> collider = std::make_unique<Collider>(ColliderShape::kCapsule);
    collider->radius = radius;
    collider->height = height;
    return collider;
}

std::unique_ptr<Collider> Collider::MakeCircle(float radius) {
    std::unique_ptr<Collider> collider = std::make_unique<Collider>(ColliderShape::kCircle);
    collider->radius = radius;
    return collider;
}

std::unique_ptr<Collider> Collider::MakeRect(const Vector2& size) {
    std::unique_ptr<Collider> collider = std::make_unique<Collider>(ColliderShape::kRect);
    collider->size2D = size;
    return collider;
}

void Collider::SetCollisionLayer(uint32_t layer) {
    // 範囲外は一番上のレイヤーに丸める（ビット演算で範囲外を使うと動作が未定義になるため）
    collisionLayer = (std::min)(layer, kMaxCollisionLayer);
}

// ===== ワールド上の形 =====

Vector3 Collider::GetWorldCenter() const {
    const Transform& transform = owner ? owner->transform : kDefaultTransform;
    return transform.translate + offset;
}

OBB Collider::MakeRotatedBox(const Vector3& boxHalfSize) const {
    const Transform& ownerTransform = owner ? owner->transform : kDefaultTransform;
    // MakeOBB は scale を半辺長として使うので、scale の代わりに自分の大きさを入れる
    Transform boxTransform = { boxHalfSize, ownerTransform.rotate, ownerTransform.translate };
    OBB obb = Collision::MakeOBB(boxTransform);
    // offset を持ち主の回転に合わせて回してから足す（ローカルX/Y/Z軸 × 各成分）
    obb.center += obb.axes[0] * offset.x + obb.axes[1] * offset.y + obb.axes[2] * offset.z;
    return obb;
}

Sphere Collider::GetWorldSphere() const {
    return { GetWorldCenter(), radius };
}

AABB Collider::GetWorldAABB() const {
    Vector3 center = GetWorldCenter();
    return { center - halfSize, center + halfSize };
}

OBB Collider::GetWorldOBB() const {
    return MakeRotatedBox(halfSize);
}

Capsule Collider::GetWorldCapsule() const {
    // 向きと中心だけ欲しいので、大きさ0の OBB を作って軸を借りる
    OBB frame = MakeRotatedBox({ 0.0f, 0.0f, 0.0f });
    Vector3 halfLine = frame.axes[1] * (height * 0.5f);
    return { { frame.center - halfLine, frame.center + halfLine }, radius };
}

Circle Collider::GetWorldCircle() const {
    Vector3 center = GetWorldCenter();
    return { { center.x, center.y }, radius };
}

Rect Collider::GetWorldRect() const {
    Vector3 center = GetWorldCenter();
    return Collision::MakeRect({ center.x, center.y }, size2D);
}

AABB Collider::GetWorldBounds() const {
    switch (shape) {
    case ColliderShape::kSphere: {
        Vector3 center = GetWorldCenter();
        Vector3 extent = { radius, radius, radius };
        return { center - extent, center + extent };
    }
    case ColliderShape::kAABB:
        return GetWorldAABB();
    case ColliderShape::kOBB:
        return MakeBoundsOfOBB(GetWorldOBB());
    case ColliderShape::kCapsule: {
        Capsule capsule = GetWorldCapsule();
        AABB bounds = MakeBoundsOfPoints(capsule.segment.start, capsule.segment.end);
        Vector3 extent = { radius, radius, radius };
        return { bounds.min - extent, bounds.max + extent };
    }
    case ColliderShape::kCircle: {
        Circle circle = GetWorldCircle();
        return { { circle.center.x - radius, circle.center.y - radius, 0.0f },
                 { circle.center.x + radius, circle.center.y + radius, 0.0f } };
    }
    case ColliderShape::kRect:
    default: {
        Rect rect = GetWorldRect();
        return { { rect.min.x, rect.min.y, 0.0f }, { rect.max.x, rect.max.y, 0.0f } };
    }
    }
}
