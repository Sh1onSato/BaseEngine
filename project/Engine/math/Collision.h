#pragma once
#include "Structs.h"

// --- 追加の形状（Sphere/AABB/Ray/RaycastHitはStructs.h） ---

// 向きを持った箱。axes[0..2]は正規化済みのローカルX/Y/Z軸、halfSizeは各軸方向の半辺長
struct OBB {
    Vector3 center;
    Vector3 axes[3];
    Vector3 halfSize;
};

// 線分
struct Segment {
    Vector3 start;
    Vector3 end;
};

// カプセル（線分を半径分太らせた形。キャラクターの当たり判定向き）
struct Capsule {
    Segment segment;
    float radius;
};

// 2D用
struct Circle {
    Vector2 center;
    float radius;
};

struct Rect {
    Vector2 min; // 左上
    Vector2 max; // 右下
};

// 衝突時の押し戻し情報。normalはBからAへ向かう向き（Aの位置に normal * depth を足すと離れる）
struct CollisionInfo {
    Vector3 normal = { 0.0f, 0.0f, 0.0f };
    float depth = 0.0f;
};

// 当たり判定関数群。outInfoを渡すとめり込み解消用の情報を返す（不要ならnullptrでよい）
namespace Collision {
    // --- 形状の生成 ---
    // Object3dのTransform（scale=半辺長, rotate=オイラー角, translate=中心）からOBBを作る
    OBB MakeOBB(const Transform& transform);
    OBB MakeOBB(const Vector3& center, const Quaternion& rotation, const Vector3& halfSize);
    Rect MakeRect(const Vector2& center, const Vector2& size);

    // --- 最近接点 ---
    Vector3 ClosestPointOnSegment(const Vector3& point, const Segment& segment);
    Vector3 ClosestPointOnAABB(const Vector3& point, const AABB& aabb);
    Vector3 ClosestPointOnOBB(const Vector3& point, const OBB& obb);
    // 2線分間の最近接点の組
    void ClosestPointsSegmentSegment(const Segment& a, const Segment& b, Vector3& outOnA, Vector3& outOnB);

    // --- 3D ---
    bool SphereSphere(const Sphere& a, const Sphere& b, CollisionInfo* outInfo = nullptr);
    bool SphereAABB(const Sphere& sphere, const AABB& aabb, CollisionInfo* outInfo = nullptr);
    bool SphereOBB(const Sphere& sphere, const OBB& obb, CollisionInfo* outInfo = nullptr);
    bool AABBAABB(const AABB& a, const AABB& b, CollisionInfo* outInfo = nullptr);
    // 分離軸判定（押し戻し情報は最小めり込み軸で返す）
    bool OBBOBB(const OBB& a, const OBB& b, CollisionInfo* outInfo = nullptr);
    bool CapsuleSphere(const Capsule& capsule, const Sphere& sphere, CollisionInfo* outInfo = nullptr);
    bool CapsuleCapsule(const Capsule& a, const Capsule& b, CollisionInfo* outInfo = nullptr);
    bool CapsuleAABB(const Capsule& capsule, const AABB& aabb, CollisionInfo* outInfo = nullptr);

    bool PointInAABB(const Vector3& point, const AABB& aabb);
    bool PointInOBB(const Vector3& point, const OBB& obb);

    // --- レイキャスト（ray.directionは正規化済みを想定。maxDistanceを超える交差は無視） ---
    bool RaySphere(const Ray& ray, const Sphere& sphere, RaycastHit* outHit = nullptr, float maxDistance = 1e30f);
    bool RayAABB(const Ray& ray, const AABB& aabb, RaycastHit* outHit = nullptr, float maxDistance = 1e30f);
    bool RayOBB(const Ray& ray, const OBB& obb, RaycastHit* outHit = nullptr, float maxDistance = 1e30f);
    // 三角形（v0,v1,v2は反時計回り/時計回りどちらでも可。法線はv0→v1→v2の外積方向）
    bool RayTriangle(const Ray& ray, const Vector3& v0, const Vector3& v1, const Vector3& v2, RaycastHit* outHit = nullptr, float maxDistance = 1e30f);

    // --- 2D ---
    bool CircleCircle(const Circle& a, const Circle& b);
    bool RectRect(const Rect& a, const Rect& b);
    bool CircleRect(const Circle& circle, const Rect& rect);
    bool PointInRect(const Vector2& point, const Rect& rect);
    bool PointInCircle(const Vector2& point, const Circle& circle);
}
