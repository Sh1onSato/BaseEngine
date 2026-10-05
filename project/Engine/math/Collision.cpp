#include "Collision.h"
#include "Calculation.h"
#include <algorithm>
#include <cmath>

namespace {
    constexpr float kEpsilon = 1e-6f;

    float Dot(const Vector3& a, const Vector3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    float LengthSq(const Vector3& v) { return Dot(v, v); }

    Vector3 Cross(const Vector3& a, const Vector3& b) {
        return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
    }

    // 中心間ベクトルから押し戻し法線を作る（中心が完全に重なっている場合は上向きにする）
    Vector3 SafeNormalize(const Vector3& v, const Vector3& fallback = { 0.0f, 1.0f, 0.0f }) {
        float lengthSq = LengthSq(v);
        if (lengthSq < kEpsilon * kEpsilon) {
            return fallback;
        }
        return v * (1.0f / std::sqrt(lengthSq));
    }

    // 点pと、半径付きの最近接点qの距離から球同士と同じ要領で判定する共通処理
    bool ResolvePointDistance(const Vector3& p, const Vector3& q, float radius, CollisionInfo* outInfo) {
        Vector3 diff = p - q;
        float distanceSq = LengthSq(diff);
        if (distanceSq > radius * radius) {
            return false;
        }
        if (outInfo) {
            float distance = std::sqrt(distanceSq);
            outInfo->normal = SafeNormalize(diff);
            outInfo->depth = radius - distance;
        }
        return true;
    }

    // OBBを軸axisに投影したときの半径
    float ProjectOBB(const OBB& obb, const Vector3& axis) {
        return std::fabs(Dot(obb.axes[0], axis)) * obb.halfSize.x +
               std::fabs(Dot(obb.axes[1], axis)) * obb.halfSize.y +
               std::fabs(Dot(obb.axes[2], axis)) * obb.halfSize.z;
    }

    float Component(const Vector3& v, int i) { return i == 0 ? v.x : (i == 1 ? v.y : v.z); }
}

// ===== 形状の生成 =====

OBB Collision::MakeOBB(const Transform& transform) {
    // Object3dと同じ回転順（X→Y→Z、行ベクトル規約）。行列の各行がローカル軸のワールド方向
    Matrix4x4 rotation = Calculation::MakeRotationXMatrix(transform.rotate.x) *
                         Calculation::MakeRotationYMatrix(transform.rotate.y) *
                         Calculation::MakeRotationZMatrix(transform.rotate.z);
    OBB obb;
    obb.center = transform.translate;
    for (int i = 0; i < 3; ++i) {
        obb.axes[i] = { rotation.m[i][0], rotation.m[i][1], rotation.m[i][2] };
    }
    obb.halfSize = transform.scale;
    return obb;
}

OBB Collision::MakeOBB(const Vector3& center, const Quaternion& rotation, const Vector3& halfSize) {
    Matrix4x4 matrix = Calculation::MakeRotateMatrix(rotation);
    OBB obb;
    obb.center = center;
    for (int i = 0; i < 3; ++i) {
        obb.axes[i] = { matrix.m[i][0], matrix.m[i][1], matrix.m[i][2] };
    }
    obb.halfSize = halfSize;
    return obb;
}

Rect Collision::MakeRect(const Vector2& center, const Vector2& size) {
    Vector2 half = size * 0.5f;
    return { center - half, center + half };
}

// ===== 最近接点 =====

Vector3 Collision::ClosestPointOnSegment(const Vector3& point, const Segment& segment) {
    Vector3 d = segment.end - segment.start;
    float lengthSq = LengthSq(d);
    if (lengthSq < kEpsilon) {
        return segment.start;
    }
    float t = (std::clamp)(Dot(point - segment.start, d) / lengthSq, 0.0f, 1.0f);
    return segment.start + d * t;
}

Vector3 Collision::ClosestPointOnAABB(const Vector3& point, const AABB& aabb) {
    return {
        (std::clamp)(point.x, aabb.min.x, aabb.max.x),
        (std::clamp)(point.y, aabb.min.y, aabb.max.y),
        (std::clamp)(point.z, aabb.min.z, aabb.max.z),
    };
}

Vector3 Collision::ClosestPointOnOBB(const Vector3& point, const OBB& obb) {
    Vector3 d = point - obb.center;
    Vector3 result = obb.center;
    for (int i = 0; i < 3; ++i) {
        float half = Component(obb.halfSize, i);
        float distance = (std::clamp)(Dot(d, obb.axes[i]), -half, half);
        result += obb.axes[i] * distance;
    }
    return result;
}

void Collision::ClosestPointsSegmentSegment(const Segment& a, const Segment& b, Vector3& outOnA, Vector3& outOnB) {
    // Real-Time Collision Detection 5.1.9 の方法
    Vector3 d1 = a.end - a.start;
    Vector3 d2 = b.end - b.start;
    Vector3 r = a.start - b.start;
    float lenA = LengthSq(d1);
    float lenB = LengthSq(d2);
    float f = Dot(d2, r);

    float s = 0.0f;
    float t = 0.0f;
    if (lenA <= kEpsilon && lenB <= kEpsilon) {
        outOnA = a.start;
        outOnB = b.start;
        return;
    }
    if (lenA <= kEpsilon) {
        t = (std::clamp)(f / lenB, 0.0f, 1.0f);
    } else {
        float c = Dot(d1, r);
        if (lenB <= kEpsilon) {
            s = (std::clamp)(-c / lenA, 0.0f, 1.0f);
        } else {
            float bDot = Dot(d1, d2);
            float denom = lenA * lenB - bDot * bDot;
            s = denom != 0.0f ? (std::clamp)((bDot * f - c * lenB) / denom, 0.0f, 1.0f) : 0.0f;
            t = (bDot * s + f) / lenB;
            if (t < 0.0f) {
                t = 0.0f;
                s = (std::clamp)(-c / lenA, 0.0f, 1.0f);
            } else if (t > 1.0f) {
                t = 1.0f;
                s = (std::clamp)((bDot - c) / lenA, 0.0f, 1.0f);
            }
        }
    }
    outOnA = a.start + d1 * s;
    outOnB = b.start + d2 * t;
}

// ===== 3D =====

bool Collision::SphereSphere(const Sphere& a, const Sphere& b, CollisionInfo* outInfo) {
    return ResolvePointDistance(a.center, b.center, a.radius + b.radius, outInfo);
}

bool Collision::SphereAABB(const Sphere& sphere, const AABB& aabb, CollisionInfo* outInfo) {
    if (!PointInAABB(sphere.center, aabb)) {
        return ResolvePointDistance(sphere.center, ClosestPointOnAABB(sphere.center, aabb), sphere.radius, outInfo);
    }

    // 中心が箱の内側にある場合は、一番近い面から外へ押し出す
    if (outInfo) {
        float distances[6] = {
            sphere.center.x - aabb.min.x, aabb.max.x - sphere.center.x,
            sphere.center.y - aabb.min.y, aabb.max.y - sphere.center.y,
            sphere.center.z - aabb.min.z, aabb.max.z - sphere.center.z,
        };
        const Vector3 normals[6] = {
            { -1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f },
            { 0.0f, -1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f },
            { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f, 1.0f },
        };
        int best = static_cast<int>(std::min_element(distances, distances + 6) - distances);
        outInfo->normal = normals[best];
        outInfo->depth = distances[best] + sphere.radius;
    }
    return true;
}

bool Collision::SphereOBB(const Sphere& sphere, const OBB& obb, CollisionInfo* outInfo) {
    // OBBのローカル空間でAABBとして判定し、結果をワールドへ戻す
    Vector3 d = sphere.center - obb.center;
    Sphere localSphere = { { Dot(d, obb.axes[0]), Dot(d, obb.axes[1]), Dot(d, obb.axes[2]) }, sphere.radius };
    AABB localBox = { { -obb.halfSize.x, -obb.halfSize.y, -obb.halfSize.z }, obb.halfSize };

    CollisionInfo localInfo;
    if (!SphereAABB(localSphere, localBox, outInfo ? &localInfo : nullptr)) {
        return false;
    }
    if (outInfo) {
        outInfo->normal = obb.axes[0] * localInfo.normal.x + obb.axes[1] * localInfo.normal.y + obb.axes[2] * localInfo.normal.z;
        outInfo->depth = localInfo.depth;
    }
    return true;
}

bool Collision::AABBAABB(const AABB& a, const AABB& b, CollisionInfo* outInfo) {
    if (a.max.x < b.min.x || a.min.x > b.max.x ||
        a.max.y < b.min.y || a.min.y > b.max.y ||
        a.max.z < b.min.z || a.min.z > b.max.z) {
        return false;
    }

    if (outInfo) {
        // めり込みが最小の軸で押し戻す
        float overlaps[3] = {
            (std::min)(a.max.x, b.max.x) - (std::max)(a.min.x, b.min.x),
            (std::min)(a.max.y, b.max.y) - (std::max)(a.min.y, b.min.y),
            (std::min)(a.max.z, b.max.z) - (std::max)(a.min.z, b.min.z),
        };
        int axis = static_cast<int>(std::min_element(overlaps, overlaps + 3) - overlaps);
        float centerA = (Component(a.min, axis) + Component(a.max, axis)) * 0.5f;
        float centerB = (Component(b.min, axis) + Component(b.max, axis)) * 0.5f;
        float sign = centerA >= centerB ? 1.0f : -1.0f;
        outInfo->normal = { axis == 0 ? sign : 0.0f, axis == 1 ? sign : 0.0f, axis == 2 ? sign : 0.0f };
        outInfo->depth = overlaps[axis];
    }
    return true;
}

bool Collision::OBBOBB(const OBB& a, const OBB& b, CollisionInfo* outInfo) {
    Vector3 axes[15];
    int axisCount = 0;
    for (int i = 0; i < 3; ++i) axes[axisCount++] = a.axes[i];
    for (int i = 0; i < 3; ++i) axes[axisCount++] = b.axes[i];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            Vector3 cross = Cross(a.axes[i], b.axes[j]);
            // 平行な軸同士の外積は0になり判定に使えないので除外
            if (LengthSq(cross) > kEpsilon) {
                axes[axisCount++] = SafeNormalize(cross);
            }
        }
    }

    Vector3 centerDiff = a.center - b.center;
    float minOverlap = 1e30f;
    Vector3 minAxis = { 0.0f, 1.0f, 0.0f };
    for (int i = 0; i < axisCount; ++i) {
        const Vector3& axis = axes[i];
        float distance = Dot(centerDiff, axis);
        float overlap = ProjectOBB(a, axis) + ProjectOBB(b, axis) - std::fabs(distance);
        if (overlap < 0.0f) {
            return false; // 分離軸が見つかった
        }
        if (overlap < minOverlap) {
            minOverlap = overlap;
            minAxis = distance >= 0.0f ? axis : axis * -1.0f;
        }
    }

    if (outInfo) {
        outInfo->normal = minAxis;
        outInfo->depth = minOverlap;
    }
    return true;
}

bool Collision::CapsuleSphere(const Capsule& capsule, const Sphere& sphere, CollisionInfo* outInfo) {
    Vector3 closest = ClosestPointOnSegment(sphere.center, capsule.segment);
    // Aをカプセル側にするので、球の中心 → カプセル上の点 の向きを法線にする
    return ResolvePointDistance(closest, sphere.center, capsule.radius + sphere.radius, outInfo);
}

bool Collision::CapsuleCapsule(const Capsule& a, const Capsule& b, CollisionInfo* outInfo) {
    Vector3 onA, onB;
    ClosestPointsSegmentSegment(a.segment, b.segment, onA, onB);
    return ResolvePointDistance(onA, onB, a.radius + b.radius, outInfo);
}

bool Collision::CapsuleAABB(const Capsule& capsule, const AABB& aabb, CollisionInfo* outInfo) {
    // 線分上の点と箱上の点を交互に最近接点へ寄せていく近似（キャラクター判定なら2回で十分な精度）
    Vector3 boxCenter = (aabb.min + aabb.max) * 0.5f;
    Vector3 onSegment = ClosestPointOnSegment(boxCenter, capsule.segment);
    for (int i = 0; i < 2; ++i) {
        Vector3 onBox = ClosestPointOnAABB(onSegment, aabb);
        onSegment = ClosestPointOnSegment(onBox, capsule.segment);
    }
    return SphereAABB({ onSegment, capsule.radius }, aabb, outInfo);
}

bool Collision::PointInAABB(const Vector3& point, const AABB& aabb) {
    return point.x >= aabb.min.x && point.x <= aabb.max.x &&
           point.y >= aabb.min.y && point.y <= aabb.max.y &&
           point.z >= aabb.min.z && point.z <= aabb.max.z;
}

bool Collision::PointInOBB(const Vector3& point, const OBB& obb) {
    Vector3 d = point - obb.center;
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(Dot(d, obb.axes[i])) > Component(obb.halfSize, i)) {
            return false;
        }
    }
    return true;
}

// ===== レイキャスト =====

bool Collision::RaySphere(const Ray& ray, const Sphere& sphere, RaycastHit* outHit, float maxDistance) {
    Vector3 m = ray.origin - sphere.center;
    float b = Dot(m, ray.direction);
    float c = LengthSq(m) - sphere.radius * sphere.radius;
    // 始点が球の外にあり、かつ球から離れる向きなら当たらない
    if (c > 0.0f && b > 0.0f) {
        return false;
    }
    float discriminant = b * b - c;
    if (discriminant < 0.0f) {
        return false;
    }
    float t = (std::max)(0.0f, -b - std::sqrt(discriminant));
    if (t > maxDistance) {
        return false;
    }
    if (outHit) {
        outHit->distance = t;
        outHit->hitPoint = ray.origin + ray.direction * t;
        outHit->normal = SafeNormalize(outHit->hitPoint - sphere.center);
    }
    return true;
}

bool Collision::RayAABB(const Ray& ray, const AABB& aabb, RaycastHit* outHit, float maxDistance) {
    RaycastHit hit;
    if (!Calculation::TestRayAABB(ray, aabb, &hit) || hit.distance > maxDistance) {
        return false;
    }
    if (outHit) {
        *outHit = hit;
    }
    return true;
}

bool Collision::RayOBB(const Ray& ray, const OBB& obb, RaycastHit* outHit, float maxDistance) {
    // レイをOBBのローカル空間へ変換してAABBとして判定する
    Vector3 d = ray.origin - obb.center;
    Ray localRay = {
        { Dot(d, obb.axes[0]), Dot(d, obb.axes[1]), Dot(d, obb.axes[2]) },
        { Dot(ray.direction, obb.axes[0]), Dot(ray.direction, obb.axes[1]), Dot(ray.direction, obb.axes[2]) },
    };
    AABB localBox = { { -obb.halfSize.x, -obb.halfSize.y, -obb.halfSize.z }, obb.halfSize };

    RaycastHit localHit;
    if (!RayAABB(localRay, localBox, &localHit, maxDistance)) {
        return false;
    }
    if (outHit) {
        outHit->distance = localHit.distance;
        outHit->hitPoint = ray.origin + ray.direction * localHit.distance;
        outHit->normal = obb.axes[0] * localHit.normal.x + obb.axes[1] * localHit.normal.y + obb.axes[2] * localHit.normal.z;
    }
    return true;
}

bool Collision::RayTriangle(const Ray& ray, const Vector3& v0, const Vector3& v1, const Vector3& v2, RaycastHit* outHit, float maxDistance) {
    // Möller–Trumbore法
    Vector3 edge1 = v1 - v0;
    Vector3 edge2 = v2 - v0;
    Vector3 p = Cross(ray.direction, edge2);
    float det = Dot(edge1, p);
    if (std::fabs(det) < kEpsilon) {
        return false; // レイと三角形が平行
    }
    float invDet = 1.0f / det;
    Vector3 s = ray.origin - v0;
    float u = Dot(s, p) * invDet;
    if (u < 0.0f || u > 1.0f) {
        return false;
    }
    Vector3 q = Cross(s, edge1);
    float v = Dot(ray.direction, q) * invDet;
    if (v < 0.0f || u + v > 1.0f) {
        return false;
    }
    float t = Dot(edge2, q) * invDet;
    if (t < 0.0f || t > maxDistance) {
        return false;
    }
    if (outHit) {
        outHit->distance = t;
        outHit->hitPoint = ray.origin + ray.direction * t;
        outHit->normal = SafeNormalize(Cross(edge1, edge2));
    }
    return true;
}

// ===== 2D =====

bool Collision::CircleCircle(const Circle& a, const Circle& b) {
    Vector2 d = a.center - b.center;
    float radius = a.radius + b.radius;
    return d.x * d.x + d.y * d.y <= radius * radius;
}

bool Collision::RectRect(const Rect& a, const Rect& b) {
    return a.min.x <= b.max.x && a.max.x >= b.min.x &&
           a.min.y <= b.max.y && a.max.y >= b.min.y;
}

bool Collision::CircleRect(const Circle& circle, const Rect& rect) {
    Vector2 closest = {
        (std::clamp)(circle.center.x, rect.min.x, rect.max.x),
        (std::clamp)(circle.center.y, rect.min.y, rect.max.y),
    };
    Vector2 d = circle.center - closest;
    return d.x * d.x + d.y * d.y <= circle.radius * circle.radius;
}

bool Collision::PointInRect(const Vector2& point, const Rect& rect) {
    return point.x >= rect.min.x && point.x <= rect.max.x &&
           point.y >= rect.min.y && point.y <= rect.max.y;
}

bool Collision::PointInCircle(const Vector2& point, const Circle& circle) {
    Vector2 d = point - circle.center;
    return d.x * d.x + d.y * d.y <= circle.radius * circle.radius;
}
