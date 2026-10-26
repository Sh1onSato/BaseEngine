#include "CollisionManager.h"
#include "Calculation.h"
#include "GameObject.h"
#include "Logger.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace {
    enum class EventKind { kEnter, kStay, kExit };

    // Rigidbody が無い物の摩擦（Rigidbody::friction の既定値と同じ）
    constexpr float kDefaultFriction = 0.4f;
    // 接地とみなす面の傾き（「上」との内積。0.7 ≒ 45 度）
    constexpr float kGroundCosine = 0.7f;
    // 段差とみなさない高さ（床のつなぎ目・重力での小さなめり込みで引っかからないように）
    constexpr float kStepTolerance = 0.05f;
    // 法線が箱の軸とこれ以上平行なら「面」に当たった（それより斜めなら角・辺に当たった）
    constexpr float kFaceNormalCosine = 0.999f;
    // 地面に吸い付ける距離の最小値（段差の高さ 0 のキャラクターでも下り坂から離れないように）
    constexpr float kMinGroundSnapDistance = 0.1f;
    // 回る物の当たっている点: 相手の形からこの距離までの点を「触れている」とみなす
    constexpr float kContactSlop = 0.02f;
    // 1つの組で使う点の数の上限と、速度を直す繰り返しの回数（多いほど積んだ物が安定するが重い）
    constexpr size_t kMaxContactPoints = 8;
    constexpr int kSolverIterations = 10;
    // 同じ点とみなす距離
    constexpr float kSamePointDistance = 0.01f;
    // 前のステップの点と「同じ所で当たり続けている」とみなす距離
    constexpr float kWarmStartDistance = 0.05f;

    // 丸い形（球・カプセル・円）の半径。箱は 0
    float GetRoundRadius(const Collider& collider) {
        switch (collider.GetShape()) {
        case ColliderShape::kSphere: return collider.GetWorldSphere().radius;
        case ColliderShape::kCapsule: return collider.GetWorldCapsule().radius;
        case ColliderShape::kCircle: return collider.GetWorldCircle().radius;
        default: return 0.0f;
        }
    }

    // 形の中で dir の向きに一番遠い点（段差の高さを測るのに使う）。dir は長さ1
    Vector3 SupportPoint(const Collider& collider, const Vector3& dir) {
        auto boxCorner = [&dir](const Vector3& min, const Vector3& max) {
            return Vector3{ dir.x >= 0.0f ? max.x : min.x, dir.y >= 0.0f ? max.y : min.y, dir.z >= 0.0f ? max.z : min.z };
        };
        switch (collider.GetShape()) {
        case ColliderShape::kSphere: {
            const Sphere sphere = collider.GetWorldSphere();
            return sphere.center + dir * sphere.radius;
        }
        case ColliderShape::kAABB: {
            const AABB aabb = collider.GetWorldAABB();
            return boxCorner(aabb.min, aabb.max);
        }
        case ColliderShape::kOBB: {
            const OBB obb = collider.GetWorldOBB();
            const float halfSizes[3] = { obb.halfSize.x, obb.halfSize.y, obb.halfSize.z };
            Vector3 point = obb.center;
            for (int i = 0; i < 3; ++i) {
                const float sign = Calculation::Dot(dir, obb.axes[i]) >= 0.0f ? 1.0f : -1.0f;
                point += obb.axes[i] * (halfSizes[i] * sign);
            }
            return point;
        }
        case ColliderShape::kCapsule: {
            const Capsule capsule = collider.GetWorldCapsule();
            const Segment& segment = capsule.segment;
            const Vector3& end = Calculation::Dot(segment.end - segment.start, dir) >= 0.0f ? segment.end : segment.start;
            return end + dir * capsule.radius;
        }
        case ColliderShape::kCircle: {
            const Circle circle = collider.GetWorldCircle();
            return Vector3{ circle.center.x, circle.center.y, 0.0f } + dir * circle.radius;
        }
        case ColliderShape::kRect: {
            const Rect rect = collider.GetWorldRect();
            return boxCorner({ rect.min.x, rect.min.y, 0.0f }, { rect.max.x, rect.max.y, 0.0f });
        }
        }
        return collider.GetWorldBounds().min;
    }

    // 当たっている点の候補（相手のいる向き toward へ一番出ている所。箱は角を全部、カプセルは両端）
    void CollectCandidatePoints(const Collider& collider, const Vector3& toward, std::vector<Vector3>& out) {
        auto addBoxCorners = [&out](const Vector3& center, const Vector3 axes[3], const Vector3& halfSize) {
            for (int i = 0; i < 8; ++i) {
                const float sx = (i & 1) ? 1.0f : -1.0f;
                const float sy = (i & 2) ? 1.0f : -1.0f;
                const float sz = (i & 4) ? 1.0f : -1.0f;
                out.push_back(center + axes[0] * (halfSize.x * sx) + axes[1] * (halfSize.y * sy) + axes[2] * (halfSize.z * sz));
            }
        };
        switch (collider.GetShape()) {
        case ColliderShape::kSphere: {
            const Sphere sphere = collider.GetWorldSphere();
            out.push_back(sphere.center + toward * sphere.radius);
            break;
        }
        case ColliderShape::kAABB: {
            const AABB aabb = collider.GetWorldAABB();
            const Vector3 axes[3] = { { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } };
            addBoxCorners((aabb.min + aabb.max) * 0.5f, axes, (aabb.max - aabb.min) * 0.5f);
            break;
        }
        case ColliderShape::kOBB: {
            const OBB box = collider.GetWorldOBB();
            addBoxCorners(box.center, box.axes, box.halfSize);
            break;
        }
        case ColliderShape::kCapsule: {
            const Capsule capsule = collider.GetWorldCapsule();
            out.push_back(capsule.segment.start + toward * capsule.radius);
            out.push_back(capsule.segment.end + toward * capsule.radius);
            break;
        }
        case ColliderShape::kCircle: {
            const Circle circle = collider.GetWorldCircle();
            out.push_back(Vector3{ circle.center.x, circle.center.y, 0.0f } + toward * circle.radius);
            break;
        }
        case ColliderShape::kRect: {
            const Rect rect = collider.GetWorldRect();
            out.push_back({ rect.min.x, rect.min.y, 0.0f });
            out.push_back({ rect.max.x, rect.min.y, 0.0f });
            out.push_back({ rect.min.x, rect.max.y, 0.0f });
            out.push_back({ rect.max.x, rect.max.y, 0.0f });
            break;
        }
        }
    }

    // 点から形までの距離（中に入っていれば 0）
    float DistanceToShape(const Collider& collider, const Vector3& point) {
        switch (collider.GetShape()) {
        case ColliderShape::kSphere: {
            const Sphere sphere = collider.GetWorldSphere();
            return (std::max)(Calculation::Length(point - sphere.center) - sphere.radius, 0.0f);
        }
        case ColliderShape::kAABB:
            return Calculation::Length(point - Collision::ClosestPointOnAABB(point, collider.GetWorldAABB()));
        case ColliderShape::kOBB:
            return Calculation::Length(point - Collision::ClosestPointOnOBB(point, collider.GetWorldOBB()));
        case ColliderShape::kCapsule: {
            const Capsule capsule = collider.GetWorldCapsule();
            return (std::max)(Calculation::Length(point - Collision::ClosestPointOnSegment(point, capsule.segment)) - capsule.radius, 0.0f);
        }
        case ColliderShape::kCircle: {
            const Circle circle = collider.GetWorldCircle();
            const Vector3 center = { circle.center.x, circle.center.y, point.z };
            return (std::max)(Calculation::Length(point - center) - circle.radius, 0.0f);
        }
        case ColliderShape::kRect: {
            const Rect rect = collider.GetWorldRect();
            const Vector3 closest = { std::clamp(point.x, rect.min.x, rect.max.x), std::clamp(point.y, rect.min.y, rect.max.y), point.z };
            return Calculation::Length(point - closest);
        }
        }
        return 0.0f;
    }

    // n に直交する2本の向き（摩擦をかける向き）
    void MakeTangents(const Vector3& n, Vector3& t1, Vector3& t2) {
        const Vector3 helper = std::abs(n.x) < 0.57f ? Vector3{ 1.0f, 0.0f, 0.0f } : Vector3{ 0.0f, 1.0f, 0.0f };
        t1 = Calculation::Normalize(Calculation::Cross(n, helper));
        t2 = Calculation::Cross(n, t1);
    }

    // normal が箱の面の向き（どれかの軸と平行）なら true。角・辺に当たったときは斜めの向きになる。
    // 球・カプセル・円は曲面なので常に true（傾きだけで坂と同じに扱う）
    bool IsFaceNormal(const Collider& collider, const Vector3& normal) {
        switch (collider.GetShape()) {
        case ColliderShape::kAABB:
        case ColliderShape::kRect:
            return std::abs(normal.x) >= kFaceNormalCosine || std::abs(normal.y) >= kFaceNormalCosine ||
                   std::abs(normal.z) >= kFaceNormalCosine;
        case ColliderShape::kOBB: {
            const OBB obb = collider.GetWorldOBB();
            for (const Vector3& axis : obb.axes) {
                if (std::abs(Calculation::Dot(normal, axis)) >= kFaceNormalCosine) {
                    return true;
                }
            }
            return false;
        }
        default:
            return true;
        }
    }

    // 当たっていたら法線を反対向きにする（関数に渡した順番と、欲しい向きが逆のとき用）
    bool FlipNormal(bool hit, CollisionInfo* info) {
        if (hit && info) {
            info->normal = info->normal * -1.0f;
        }
        return hit;
    }

    // 回転しない箱を、軸が X/Y/Z そのままの OBB に変換する（AABB × OBB を OBB 同士の判定で行うため）
    OBB ToOBB(const AABB& aabb) {
        OBB obb;
        obb.center = (aabb.min + aabb.max) * 0.5f;
        obb.axes[0] = { 1.0f, 0.0f, 0.0f };
        obb.axes[1] = { 0.0f, 1.0f, 0.0f };
        obb.axes[2] = { 0.0f, 0.0f, 1.0f };
        obb.halfSize = (aabb.max - aabb.min) * 0.5f;
        return obb;
    }

    void LogUnsupportedOnce() {
        // 毎フレーム出すとログが埋まるので、最初の1回だけ
        static bool isLogged = false;
        if (!isLogged) {
            isLogged = true;
            Logger::Log("[CollisionManager] OBB とカプセルの当たり判定には対応していません（当たらない扱いにします）\n");
        }
    }

    // --- 細かい判定。a の形 <= b の形（enum の順番）になっている前提。normal は a を b から離す向き ---
    bool TestSphereWith(const Sphere& sphere, const Collider& b, CollisionInfo* info) {
        switch (b.GetShape()) {
        case ColliderShape::kSphere: return Collision::SphereSphere(sphere, b.GetWorldSphere(), info);
        case ColliderShape::kAABB: return Collision::SphereAABB(sphere, b.GetWorldAABB(), info);
        case ColliderShape::kOBB: return Collision::SphereOBB(sphere, b.GetWorldOBB(), info);
        // CapsuleSphere はカプセルを押し出す向きを返すので反対にする
        case ColliderShape::kCapsule: return FlipNormal(Collision::CapsuleSphere(b.GetWorldCapsule(), sphere, info), info);
        default: return false;
        }
    }

    bool TestAABBWith(const AABB& aabb, const Collider& b, CollisionInfo* info) {
        switch (b.GetShape()) {
        case ColliderShape::kAABB: return Collision::AABBAABB(aabb, b.GetWorldAABB(), info);
        case ColliderShape::kOBB: return Collision::OBBOBB(ToOBB(aabb), b.GetWorldOBB(), info);
        case ColliderShape::kCapsule: return FlipNormal(Collision::CapsuleAABB(b.GetWorldCapsule(), aabb, info), info);
        default: return false;
        }
    }

    bool TestOrdered(const Collider& a, const Collider& b, CollisionInfo* info) {
        const ColliderShape shapeB = b.GetShape();
        switch (a.GetShape()) {
        case ColliderShape::kSphere: return TestSphereWith(a.GetWorldSphere(), b, info);
        case ColliderShape::kAABB: return TestAABBWith(a.GetWorldAABB(), b, info);
        case ColliderShape::kOBB:
            if (shapeB == ColliderShape::kOBB) {
                return Collision::OBBOBB(a.GetWorldOBB(), b.GetWorldOBB(), info);
            }
            LogUnsupportedOnce(); // OBB × カプセル
            return false;
        case ColliderShape::kCapsule: return Collision::CapsuleCapsule(a.GetWorldCapsule(), b.GetWorldCapsule(), info);
        case ColliderShape::kCircle:
            if (shapeB == ColliderShape::kCircle) {
                return Collision::CircleCircle(a.GetWorldCircle(), b.GetWorldCircle(), info);
            }
            return Collision::CircleRect(a.GetWorldCircle(), b.GetWorldRect(), info);
        case ColliderShape::kRect: return Collision::RectRect(a.GetWorldRect(), b.GetWorldRect(), info);
        default: return false;
        }
    }

    // 形の組み合わせに合う関数を選ぶ。順番を入れ替えて呼んだときは法線を反対にして、常に「a を b から離す向き」にそろえる
    bool TestColliders(const Collider& a, const Collider& b, CollisionInfo* info) {
        if (a.GetShape() > b.GetShape()) {
            return FlipNormal(TestOrdered(b, a, info), info);
        }
        return TestOrdered(a, b, info);
    }

    bool OverlapsYZ(const AABB& a, const AABB& b) {
        return a.min.y <= b.max.y && a.max.y >= b.min.y &&
               a.min.z <= b.max.z && a.max.z >= b.min.z;
    }

    bool RaycastCollider(const Ray& ray, const Collider& collider, RaycastHit* hit, float maxDistance) {
        switch (collider.GetShape()) {
        case ColliderShape::kSphere: return Collision::RaySphere(ray, collider.GetWorldSphere(), hit, maxDistance);
        case ColliderShape::kAABB: return Collision::RayAABB(ray, collider.GetWorldAABB(), hit, maxDistance);
        case ColliderShape::kOBB: return Collision::RayOBB(ray, collider.GetWorldOBB(), hit, maxDistance);
        default: return false; // カプセル・2D は対象外
        }
    }

    // 受け取る側が生きていれば、種類に合った関数を呼ぶ
    // （alive に入っている = このフレームの最初に存在していた物なので、ポインタはまだ有効）
    void Dispatch(GameObject* receiver, EventKind kind, const CollisionEvent& event,
                  const std::unordered_set<const GameObject*>& alive) {
        if (!receiver || !alive.contains(receiver) || receiver->IsDestroyed()) {
            return;
        }
        switch (kind) {
        case EventKind::kEnter: receiver->OnCollisionEnter(event); break;
        case EventKind::kStay: receiver->OnCollisionStay(event); break;
        case EventKind::kExit: receiver->OnCollisionExit(event); break;
        }
    }
}

CollisionManager::CollisionManager() {
    layerMasks.fill(0xFFFFFFFFu); // 最初は全部のレイヤー同士が当たる
}

void CollisionManager::Update(const std::vector<GameObject*>& objects) {
    BeginFrame();
    Step(objects);
    EndFrame(objects);
}

void CollisionManager::BeginFrame() {
    frameContacts.clear();
}

void CollisionManager::Step(const std::vector<GameObject*>& objects) {
    GatherColliders(objects);
    const std::vector<std::pair<size_t, size_t>> pairs = FindBroadphasePairs();
    lastBroadphasePairCount = pairs.size();

    manifolds.clear();
    ContactMap current = TestPairs(pairs);
    // 回る物が混ざった組の速度は、全部の組を集めてからまとめて直す
    SolveManifolds();
    lastHitPairCount = current.size();
    for (auto& [key, contact] : current) {
        frameContacts[key] = contact;
    }
    SnapToGround();
}

void CollisionManager::SnapToGround() {
    if (Calculation::Length(upDirection) <= 0.0f) {
        return; // 重力が無い
    }
    const Vector3 down = upDirection * -1.0f;
    for (const Entry& entry : entries) {
        const Collider* collider = entry.collider;
        Rigidbody* body = entry.owner->GetRigidbody();
        // 段差の高さが決まった物で、前のステップは地面にいて、今は離れている物だけ（ジャンプ中・2D は除く）
        if (!body || body->stepHeight < 0.0f || body->isKinematic || !body->wasGrounded || body->isGrounded ||
            collider->GetType() != ColliderType::kDynamic || collider->Is2D() ||
            Calculation::Dot(body->velocity, upDirection) > 0.0f) {
            continue;
        }
        // 形の中心から真下に地面を探す。丸い形は坂の上だと足元より少し高く浮いて接するので、その分も見込む
        const AABB bounds = collider->GetWorldBounds();
        const Vector3 origin = (bounds.min + bounds.max) * 0.5f;
        const float footDistance = Calculation::Dot(origin - SupportPoint(*collider, down), upDirection);
        const float roundRadius = GetRoundRadius(*collider);
        const float snapDistance = (std::max)(body->stepHeight, kMinGroundSnapDistance);
        const float maxDistance = footDistance - roundRadius + roundRadius / kGroundCosine + snapDistance;
        RaycastHit hit{};
        if (!RaycastGround(Ray{ origin, down }, maxDistance, entry, &hit)) {
            continue;
        }
        const float slopeCosine = Calculation::Dot(hit.normal, upDirection);
        if (slopeCosine < kGroundCosine) {
            continue; // 乗れない急な面には吸い付けない
        }
        const float drop = hit.distance - (footDistance - roundRadius + roundRadius / slopeCosine);
        if (drop <= 0.0f || drop > snapDistance) {
            continue; // もう接している / 段差より高い所から落ちている
        }
        const Vector3 move = down * drop;
        entry.owner->transform.translate += move;
        if (body->stepSmoothTime > 0.0f && drop > kStepTolerance) {
            // 低い段差を降りたときも、見た目はなめらかに下げる（乗ったときと同じ）
            body->previousPosition += move;
            body->stepSmoothOffset -= move;
        }
        body->isGrounded = true;
        body->groundNormal = hit.normal;
    }
}

bool CollisionManager::RaycastGround(const Ray& ray, float maxDistance, const Entry& self, RaycastHit* outHit) const {
    bool isHit = false;
    float closestDistance = maxDistance;
    for (const Entry& entry : entries) {
        const Collider* collider = entry.collider;
        if (entry.owner == self.owner || collider->GetType() == ColliderType::kTrigger || !collider->IsEnabled() ||
            entry.owner->IsDestroyed() ||
            !IsLayerCollisionEnabled(self.collider->GetCollisionLayer(), collider->GetCollisionLayer())) {
            continue; // 自分・すり抜ける物・当たらないレイヤーは地面にしない
        }
        RaycastHit hit{};
        if (!RaycastCollider(ray, *collider, &hit, closestDistance)) {
            continue;
        }
        isHit = true;
        closestDistance = hit.distance;
        *outHit = hit;
    }
    return isHit;
}

void CollisionManager::LimitGroundLaunch(GameObject* owner, const Vector3& normalForOwner, float upSpeedBefore) const {
    Rigidbody* body = owner->GetRigidbody();
    if (!body || body->stepHeight < 0.0f || Calculation::Dot(normalForOwner, upDirection) < kGroundCosine) {
        return; // キャラクター以外・床や坂でない面（壁・天井）は普通の物理のまま
    }
    // 床・坂に押し返されて増えた上向きの速さは消す（坂を登り切ったところで飛び出さないように）。
    // ぶつかる前から上向きだった分（ジャンプ）はそのまま残す
    const float limit = (std::max)(upSpeedBefore, 0.0f);
    const float upSpeed = Calculation::Dot(body->velocity, upDirection);
    if (upSpeed > limit) {
        body->velocity -= upDirection * (upSpeed - limit);
    }
}

void CollisionManager::EndFrame(const std::vector<GameObject*>& objects) {
    // 通知を送ってよい物の一覧（このフレームの最初に存在していた物）
    const std::unordered_set<const GameObject*> alive(objects.begin(), objects.end());
    SendPendingExits(alive);
    SendEvents(frameContacts, alive);
}

void CollisionManager::SetLayerCollision(uint32_t a, uint32_t b, bool enable) {
    if (a >= kLayerCount || b >= kLayerCount) {
        return;
    }
    if (enable) {
        layerMasks[a] |= (1u << b);
        layerMasks[b] |= (1u << a);
    } else {
        layerMasks[a] &= ~(1u << b);
        layerMasks[b] &= ~(1u << a);
    }
}

bool CollisionManager::IsLayerCollisionEnabled(uint32_t a, uint32_t b) const {
    if (a >= kLayerCount || b >= kLayerCount) {
        return false;
    }
    return (layerMasks[a] & (1u << b)) != 0;
}

bool CollisionManager::Raycast(const Ray& ray, float maxDistance, uint32_t layerMask, RaycastHit* outHit, Collider** outCollider) const {
    bool isHit = false;
    float closestDistance = maxDistance;
    for (const Entry& entry : entries) {
        const Collider* collider = entry.collider;
        if (!collider->IsEnabled() || entry.owner->IsDestroyed()) {
            continue;
        }
        if ((layerMask & (1u << collider->GetCollisionLayer())) == 0) {
            continue;
        }
        RaycastHit hit{};
        // 今までで一番近い距離を上限にすれば、それより遠い物は自動的に外れる
        if (!RaycastCollider(ray, *collider, &hit, closestDistance)) {
            continue;
        }
        isHit = true;
        closestDistance = hit.distance;
        if (outHit) { *outHit = hit; }
        if (outCollider) { *outCollider = entry.collider; }
    }
    return isHit;
}

void CollisionManager::RemoveObjects(const std::unordered_set<const GameObject*>& removed) {
    // 1. 集めたコライダーの一覧から外す（Raycast で消えた物を触らないため）
    std::erase_if(entries, [&removed](const Entry& entry) { return removed.contains(entry.owner); });
    std::erase_if(frameContacts, [&removed](const auto& item) {
        return removed.contains(item.second.ownerA) || removed.contains(item.second.ownerB);
    });

    // 2. 消えた物を指す「当たっている組」は、消えた側を nullptr にして Exit 予定へ移す
    //    （残った側には次の Update で OnCollisionExit が届く）
    for (auto it = previousContacts.begin(); it != previousContacts.end();) {
        const bool removedA = removed.contains(it->second.ownerA);
        const bool removedB = removed.contains(it->second.ownerB);
        if (!removedA && !removedB) {
            ++it;
            continue;
        }
        PendingExit pending{ it->first, it->second };
        if (removedA) { pending.key.a = nullptr; pending.contact.ownerA = nullptr; }
        if (removedB) { pending.key.b = nullptr; pending.contact.ownerB = nullptr; }
        pendingExits.push_back(pending);
        it = previousContacts.erase(it);
    }

    // 3. Exit 予定の中にも消えた物がいれば nullptr にする（両方いなくなった組は捨てる）
    for (PendingExit& pending : pendingExits) {
        if (removed.contains(pending.contact.ownerA)) { pending.key.a = nullptr; pending.contact.ownerA = nullptr; }
        if (removed.contains(pending.contact.ownerB)) { pending.key.b = nullptr; pending.contact.ownerB = nullptr; }
    }
    std::erase_if(pendingExits, [](const PendingExit& pending) {
        return !pending.contact.ownerA && !pending.contact.ownerB;
    });
}

void CollisionManager::Reset() {
    entries.clear();
    previousContacts.clear();
    frameContacts.clear();
    pendingExits.clear();
    impulseCache.clear();
}

void CollisionManager::GatherColliders(const std::vector<GameObject*>& objects) {
    entries.clear();
    for (GameObject* object : objects) {
        if (!object->IsActive() || object->IsDestroyed()) {
            continue;
        }
        for (const std::unique_ptr<Collider>& collider : object->GetColliders()) {
            if (collider->IsEnabled()) {
                entries.push_back({ collider.get(), object, collider->GetWorldBounds() });
            }
        }
    }
    lastColliderCount = entries.size();
}

std::vector<std::pair<size_t, size_t>> CollisionManager::FindBroadphasePairs() const {
    // 囲み箱の左端(min.x)の小さい順に並べる。並べた順に見ていき、
    // 相手の左端が自分の右端(max.x)を超えたら、それより後ろは全部 X で離れているので打ち切れる
    std::vector<size_t> order(entries.size());
    std::iota(order.begin(), order.end(), size_t{ 0 });
    std::sort(order.begin(), order.end(), [this](size_t lhs, size_t rhs) {
        return entries[lhs].bounds.min.x < entries[rhs].bounds.min.x;
    });

    std::vector<std::pair<size_t, size_t>> pairs;
    for (size_t i = 0; i < order.size(); ++i) {
        const Entry& a = entries[order[i]];
        for (size_t j = i + 1; j < order.size(); ++j) {
            const Entry& b = entries[order[j]];
            if (b.bounds.min.x > a.bounds.max.x) {
                break;
            }
            if (OverlapsYZ(a.bounds, b.bounds) && ShouldTest(a, b)) {
                pairs.emplace_back(order[i], order[j]);
            }
        }
    }
    return pairs;
}

bool CollisionManager::ShouldTest(const Entry& a, const Entry& b) const {
    if (a.owner == b.owner) {
        return false; // 自分のコライダー同士
    }
    if (a.collider->Is2D() != b.collider->Is2D()) {
        return false; // 2D と 3D
    }
    if (a.collider->GetType() == ColliderType::kStatic && b.collider->GetType() == ColliderType::kStatic) {
        return false; // 動かない物同士は調べる意味がない
    }
    return IsLayerCollisionEnabled(a.collider->GetCollisionLayer(), b.collider->GetCollisionLayer());
}

CollisionManager::ContactMap CollisionManager::TestPairs(const std::vector<std::pair<size_t, size_t>>& pairs) {
    ContactMap current;
    for (const std::pair<size_t, size_t>& pair : pairs) {
        const Entry* entryA = &entries[pair.first];
        const Entry* entryB = &entries[pair.second];
        // 前のフレームの組と見比べられるよう、アドレスの小さい方を a にそろえる
        if (std::less<Collider*>()(entryB->collider, entryA->collider)) {
            std::swap(entryA, entryB);
        }
        // 押し戻しで位置が変わるので、形は毎回 transform から計算し直す（囲み箱は集めたときのまま）
        CollisionInfo info;
        if (!TestColliders(*entryA->collider, *entryB->collider, &info)) {
            continue;
        }
        ResolvePenetration(entryA->collider, entryB->collider, info);
        current[{ entryA->collider, entryB->collider }] = { entryA->owner, entryB->owner, info.normal, info.depth };
    }
    return current;
}

void CollisionManager::ResolvePenetration(Collider* a, Collider* b, const CollisionInfo& info) {
    if (a->GetType() == ColliderType::kTrigger || b->GetType() == ColliderType::kTrigger) {
        return; // Trigger はすり抜ける（通知だけ）
    }
    // 動く割合は重みの比（Rigidbody が無い Dynamic 同士は 1:1 で半分ずつ、Dynamic×Static は Dynamic が全部）
    const float weightA = GetPushWeight(*a);
    const float weightB = GetPushWeight(*b);
    if (weightA + weightB <= 0.0f) {
        return; // どちらも動かない（Static・キネマティック同士）
    }
    const float shareA = weightA / (weightA + weightB);
    const float shareB = 1.0f - shareA;

    // 段差の高さが決まったキャラクターは、段差の角などで上へ押されないよう向きを直す（段差に乗れたらここで終わり）
    Vector3 normal = info.normal;
    float depth = info.depth;
    if (ApplyStepLimit(a, *b, normal, depth)) {
        return;
    }
    Vector3 normalForB = normal * -1.0f;
    if (ApplyStepLimit(b, *a, normalForB, depth)) {
        return;
    }
    normal = normalForB * -1.0f;

    // normal は a を b から離す向きなので、a は +、b は - の向きへ動かす
    GameObject* ownerA = a->GetOwner();
    GameObject* ownerB = b->GetOwner();
    // 回る物が混ざっていれば、押し戻す前の形で当たっている点を集めておく（速度は後で SolveManifolds が直す）
    const bool isRotatingPair = IsRotatingPair(ownerA, ownerB);
    if (isRotatingPair) {
        manifolds.push_back(BuildManifold(*a, *b, normal, depth));
        manifolds.back().key = { a, b };
    }
    ownerA->transform.translate += normal * (depth * shareA);
    ownerB->transform.translate -= normal * (depth * shareB);
    if (isRotatingPair) {
        UpdateGrounded(ownerA, normal);
        UpdateGrounded(ownerB, normal * -1.0f);
        return;
    }

    const float upSpeedA = ownerA->GetRigidbody() ? Calculation::Dot(ownerA->GetRigidbody()->velocity, upDirection) : 0.0f;
    const float upSpeedB = ownerB->GetRigidbody() ? Calculation::Dot(ownerB->GetRigidbody()->velocity, upDirection) : 0.0f;
    ResolveVelocity(ownerA, ownerB, normal);
    LimitGroundLaunch(ownerA, normal, upSpeedA);
    LimitGroundLaunch(ownerB, normal * -1.0f, upSpeedB);
    UpdateGrounded(ownerA, normal);
    UpdateGrounded(ownerB, normal * -1.0f);
}

bool CollisionManager::ApplyStepLimit(Collider* self, const Collider& other, Vector3& normalForSelf, float& depth) const {
    GameObject* owner = self->GetOwner();
    Rigidbody* body = owner->GetRigidbody();
    if (!body || body->stepHeight < 0.0f || GetPushWeight(*self) <= 0.0f) {
        return false; // 段差の制限なし・押されない物は今まで通り
    }
    const float upAmount = Calculation::Dot(normalForSelf, upDirection);
    if (upAmount <= 0.0f) {
        return false; // 横・下向きに押される（重力が無いときもここ）
    }
    const bool isFace = IsFaceNormal(other, normalForSelf);
    if (isFace && upAmount >= kGroundCosine) {
        return false; // 乗れる傾きの面（床・坂）
    }

    // 足元から段差の上までの高さ。角・辺に当たったなら当たった所、急な面なら相手の一番上までを測る
    const float footHeight = Calculation::Dot(SupportPoint(*self, upDirection * -1.0f), upDirection);
    const Vector3 contactPoint = SupportPoint(*self, normalForSelf * -1.0f) + normalForSelf * depth;
    const float topHeight = isFace ? Calculation::Dot(SupportPoint(other, upDirection), upDirection)
                                   : Calculation::Dot(contactPoint, upDirection);
    const float rise = topHeight - footHeight;
    if (rise <= kStepTolerance) {
        return false; // ほぼ同じ高さ（床のつなぎ目・段差の縁から降りるとき）。普通に押し戻す
    }

    const Vector3 sideNormal = normalForSelf - upDirection * upAmount;
    const Vector3 sideVelocity = body->velocity - upDirection * Calculation::Dot(body->velocity, upDirection);
    const bool isMovingInto = Calculation::Dot(sideVelocity, sideNormal) < 0.0f;
    if (rise <= body->stepHeight && isMovingInto) {
        // 乗れる段差: 足元を段差の上にそろえる（落ちる速さは消す。横の速さはそのまま）
        const Vector3 lift = upDirection * rise;
        owner->transform.translate += lift;
        if (body->stepSmoothTime > 0.0f) {
            // 見た目は元の高さから少しずつ上げる。補間の始点も一緒に上げておかないと、補間とずれが重なって一瞬沈んで見える
            body->previousPosition += lift;
            body->stepSmoothOffset -= lift;
        }
        const float fallSpeed = Calculation::Dot(body->velocity, upDirection);
        if (fallSpeed < 0.0f) {
            body->velocity -= upDirection * fallSpeed;
        }
        body->isGrounded = true;
        body->groundNormal = upDirection;
        return true;
    }

    // 乗れない段差: 壁として横にだけ押し返す。横向きでも同じだけめり込みが解けるよう深さを伸ばす
    const float sideLength = Calculation::Length(sideNormal);
    if (sideLength < 0.1f) {
        return false; // ほぼ真上向き（ここには来ない想定）。深さが大きくなりすぎるので今まで通り
    }
    normalForSelf = sideNormal * (1.0f / sideLength);
    depth /= sideLength;
    return false;
}

float CollisionManager::GetPushWeight(const Collider& collider) {
    if (collider.GetType() != ColliderType::kDynamic) {
        return 0.0f;
    }
    const Rigidbody* body = collider.GetOwner()->GetRigidbody();
    return body ? body->GetInverseMass() : 1.0f; // キネマティックは 0（押されない）
}

void CollisionManager::ResolveVelocity(GameObject* ownerA, GameObject* ownerB, const Vector3& normal) const {
    Rigidbody* bodyA = ownerA->GetRigidbody();
    Rigidbody* bodyB = ownerB->GetRigidbody();
    // Rigidbody が無い物・キネマティックは「速度が変わらない（無限に重い）」物として扱う
    const float inverseMassA = bodyA ? bodyA->GetInverseMass() : 0.0f;
    const float inverseMassB = bodyB ? bodyB->GetInverseMass() : 0.0f;
    const float inverseMassSum = inverseMassA + inverseMassB;
    if (inverseMassSum <= 0.0f) {
        return;
    }
    const Vector3 zero = { 0.0f, 0.0f, 0.0f };
    auto relativeVelocity = [&]() {
        return (bodyA ? bodyA->velocity : zero) - (bodyB ? bodyB->velocity : zero);
    };

    // 1. 法線方向: 近づく速度を打ち消し、跳ね返り係数の分だけ跳ね返す
    const float approachSpeed = Calculation::Dot(relativeVelocity(), normal);
    if (approachSpeed >= 0.0f) {
        return; // もう離れようとしている
    }
    float restitution = (std::max)(bodyA ? bodyA->restitution : 0.0f, bodyB ? bodyB->restitution : 0.0f);
    if (-approachSpeed < restingSpeed) {
        restitution = 0.0f; // 重力で少し沈んだだけのぶつかりは跳ねさせない（置いた物が震えないように）
    }
    const float normalImpulse = -(1.0f + restitution) * approachSpeed / inverseMassSum;
    if (bodyA) { bodyA->velocity += normal * (normalImpulse * inverseMassA); }
    if (bodyB) { bodyB->velocity -= normal * (normalImpulse * inverseMassB); }

    // 2. 面に沿った方向: 摩擦で滑る速度を減らす（押し付ける強さ × 摩擦 まで。止まりきったらそれ以上は減らさない）
    const Vector3 velocity = relativeVelocity();
    const Vector3 tangent = velocity - normal * Calculation::Dot(velocity, normal);
    const float tangentSpeed = Calculation::Length(tangent);
    if (tangentSpeed <= 1e-6f) {
        return;
    }
    // 2つの小さい方を使う（どちらかがつるつるなら滑る。摩擦 0 のキャラクターが壁に張り付かないように）
    const float friction = (std::min)(bodyA ? bodyA->friction : kDefaultFriction, bodyB ? bodyB->friction : kDefaultFriction);
    const float frictionImpulse = (std::min)(tangentSpeed / inverseMassSum, friction * normalImpulse);
    const Vector3 direction = tangent * (1.0f / tangentSpeed);
    if (bodyA) { bodyA->velocity -= direction * (frictionImpulse * inverseMassA); }
    if (bodyB) { bodyB->velocity += direction * (frictionImpulse * inverseMassB); }
}

bool CollisionManager::IsRotatingPair(const GameObject* ownerA, const GameObject* ownerB) {
    const Rigidbody* bodyA = ownerA->GetRigidbody();
    const Rigidbody* bodyB = ownerB->GetRigidbody();
    return (bodyA && bodyA->IsRotating()) || (bodyB && bodyB->IsRotating());
}

CollisionManager::Manifold CollisionManager::BuildManifold(const Collider& a, const Collider& b, const Vector3& normal, float depth) const {
    Manifold manifold;
    manifold.ownerA = a.GetOwner();
    manifold.ownerB = b.GetOwner();
    manifold.normal = normal;
    MakeTangents(normal, manifold.tangents[0], manifold.tangents[1]);
    const Rigidbody* bodyA = manifold.ownerA->GetRigidbody();
    const Rigidbody* bodyB = manifold.ownerB->GetRigidbody();
    // 摩擦は小さい方（ResolveVelocity と同じ）
    manifold.friction = (std::min)(bodyA ? bodyA->friction : kDefaultFriction, bodyB ? bodyB->friction : kDefaultFriction);

    // 片方の形の候補の点のうち、相手に触れていて、めり込みの深さが一番深い所と同じくらいの物を使う。
    // 一番深い点は「相手の方へ一番出ている点」。そこから法線の向きに離れている分だけ浅い
    std::vector<std::pair<Vector3, float>> found;
    auto collect = [&](const Collider& self, const Collider& other, const Vector3& towardOther) {
        std::vector<Vector3> candidates;
        CollectCandidatePoints(self, towardOther, candidates);
        const Vector3 deepest = SupportPoint(self, towardOther);
        for (const Vector3& point : candidates) {
            const float pointDepth = depth - Calculation::Dot(deepest - point, towardOther);
            if (pointDepth < -kContactSlop || DistanceToShape(other, point) > kContactSlop) {
                continue;
            }
            const bool isDuplicate = std::any_of(found.begin(), found.end(), [&](const std::pair<Vector3, float>& existing) {
                return Calculation::Length(existing.first - point) < kSamePointDistance;
            });
            if (!isDuplicate) {
                found.emplace_back(point, pointDepth);
            }
        }
    };
    collect(a, b, normal * -1.0f);
    collect(b, a, normal);
    if (found.empty()) {
        // 辺と辺が交わっているなど、どちらの角も相手に入っていない: 両方の一番深い所の真ん中を1点だけ使う
        found.emplace_back((SupportPoint(a, normal * -1.0f) + SupportPoint(b, normal)) * 0.5f, depth);
    }
    // 多すぎるときは深い順に残す
    if (found.size() > kMaxContactPoints) {
        std::sort(found.begin(), found.end(), [](const auto& lhs, const auto& rhs) { return lhs.second > rhs.second; });
        found.resize(kMaxContactPoints);
    }
    for (const auto& [position, pointDepth] : found) {
        ContactPoint point;
        point.position = position;
        manifold.points.push_back(point);
    }
    return manifold;
}

void CollisionManager::SolveManifolds() {
    if (manifolds.empty()) {
        return;
    }
    const Vector3 zero = { 0.0f, 0.0f, 0.0f };
    auto pointVelocity = [&zero](const Rigidbody* body, const Vector3& position) { return body ? body->GetPointVelocity(position) : zero; };
    auto inverseMass = [](const Rigidbody* body) { return body ? body->GetInverseMass() : 0.0f; };
    // 押す向き direction に、点で押したときの「動きにくさ」（質量と回りにくさを合わせた物）の逆数
    auto effectiveMass = [&](const Rigidbody* bodyA, const Rigidbody* bodyB, const ContactPoint& point, const Vector3& direction) {
        float sum = inverseMass(bodyA) + inverseMass(bodyB);
        if (bodyA) {
            sum += Calculation::Dot(direction, Calculation::Cross(bodyA->ApplyInverseInertia(Calculation::Cross(point.offsetA, direction)), point.offsetA));
        }
        if (bodyB) {
            sum += Calculation::Dot(direction, Calculation::Cross(bodyB->ApplyInverseInertia(Calculation::Cross(point.offsetB, direction)), point.offsetB));
        }
        return sum > 0.0f ? 1.0f / sum : 0.0f;
    };
    // 点を押す（A は +impulse、B は -impulse）。回る物は回転の速さも変わる
    auto applyImpulse = [&](Rigidbody* bodyA, Rigidbody* bodyB, const ContactPoint& point, const Vector3& impulse) {
        if (bodyA && !bodyA->isKinematic) {
            bodyA->velocity += impulse * bodyA->GetInverseMass();
            bodyA->angularVelocity += bodyA->ApplyInverseInertia(Calculation::Cross(point.offsetA, impulse));
        }
        if (bodyB && !bodyB->isKinematic) {
            bodyB->velocity -= impulse * bodyB->GetInverseMass();
            bodyB->angularVelocity -= bodyB->ApplyInverseInertia(Calculation::Cross(point.offsetB, impulse));
        }
    };

    // 1. 点ごとの準備（中心からの位置・動きにくさ・跳ね返る速さ）
    for (Manifold& manifold : manifolds) {
        Rigidbody* bodyA = manifold.ownerA->GetRigidbody();
        Rigidbody* bodyB = manifold.ownerB->GetRigidbody();
        const float restitution = (std::max)(bodyA ? bodyA->restitution : 0.0f, bodyB ? bodyB->restitution : 0.0f);
        for (ContactPoint& point : manifold.points) {
            point.offsetA = point.position - manifold.ownerA->transform.translate;
            point.offsetB = point.position - manifold.ownerB->transform.translate;
            point.normalMass = effectiveMass(bodyA, bodyB, point, manifold.normal);
            point.tangentMass[0] = effectiveMass(bodyA, bodyB, point, manifold.tangents[0]);
            point.tangentMass[1] = effectiveMass(bodyA, bodyB, point, manifold.tangents[1]);
            const float approachSpeed = Calculation::Dot(pointVelocity(bodyA, point.position) - pointVelocity(bodyB, point.position), manifold.normal);
            // 重力で少し沈んだだけのぶつかりは跳ねさせない（置いた物が震えないように）
            point.bounceSpeed = approachSpeed < -restingSpeed ? -restitution * approachSpeed : 0.0f;
        }
        // 前のステップに近い所で当たっていた点は、前に押した量を先に押しておく（繰り返しの回数が少なくても積んだ物が支えられる）
        auto cached = impulseCache.find(manifold.key);
        if (cached == impulseCache.end()) {
            continue;
        }
        for (ContactPoint& point : manifold.points) {
            for (const CachedImpulse& previous : cached->second) {
                if (Calculation::Length(previous.position - point.position) > kWarmStartDistance) {
                    continue;
                }
                point.normalImpulse = previous.normalImpulse;
                point.tangentImpulse[0] = previous.tangentImpulse[0];
                point.tangentImpulse[1] = previous.tangentImpulse[1];
                applyImpulse(bodyA, bodyB, point,
                             manifold.normal * point.normalImpulse + manifold.tangents[0] * point.tangentImpulse[0] +
                                 manifold.tangents[1] * point.tangentImpulse[1]);
                break;
            }
        }
    }

    // 2. 全部の点を何回か繰り返して直す（1つ直すと他の点の速度が変わるので、繰り返すほど全体がそろう）
    for (int iteration = 0; iteration < kSolverIterations; ++iteration) {
        for (Manifold& manifold : manifolds) {
            Rigidbody* bodyA = manifold.ownerA->GetRigidbody();
            Rigidbody* bodyB = manifold.ownerB->GetRigidbody();
            for (ContactPoint& point : manifold.points) {
                // 法線の向き: 近づく速さを消して跳ね返す（引っ張る向きには押さない = 合計がマイナスにならない）
                const Vector3 relative = pointVelocity(bodyA, point.position) - pointVelocity(bodyB, point.position);
                const float normalSpeed = Calculation::Dot(relative, manifold.normal);
                const float previousNormal = point.normalImpulse;
                point.normalImpulse = (std::max)(previousNormal + point.normalMass * (point.bounceSpeed - normalSpeed), 0.0f);
                applyImpulse(bodyA, bodyB, point, manifold.normal * (point.normalImpulse - previousNormal));

                // 面に沿った向き: 滑る速さを消す（押し付けている強さ × 摩擦 まで）
                const float maxFriction = manifold.friction * point.normalImpulse;
                for (int i = 0; i < 2; ++i) {
                    const Vector3 slide = pointVelocity(bodyA, point.position) - pointVelocity(bodyB, point.position);
                    const float tangentSpeed = Calculation::Dot(slide, manifold.tangents[i]);
                    const float previousTangent = point.tangentImpulse[i];
                    point.tangentImpulse[i] = std::clamp(previousTangent - point.tangentMass[i] * tangentSpeed, -maxFriction, maxFriction);
                    applyImpulse(bodyA, bodyB, point, manifold.tangents[i] * (point.tangentImpulse[i] - previousTangent));
                }
            }
        }
    }

    // 3. 次のステップのために、点ごとに押した量を覚える（今回当たっていない組は忘れる）
    impulseCache.clear();
    for (const Manifold& manifold : manifolds) {
        std::vector<CachedImpulse>& cache = impulseCache[manifold.key];
        for (const ContactPoint& point : manifold.points) {
            cache.push_back({ point.position, point.normalImpulse, { point.tangentImpulse[0], point.tangentImpulse[1] } });
        }
    }
}

void CollisionManager::UpdateGrounded(GameObject* owner, const Vector3& normalForOwner) const {
    Rigidbody* body = owner->GetRigidbody();
    if (!body) {
        return;
    }
    // 「上」から 45 度以内の面に押し返されていれば、その面に乗っている
    if (Calculation::Dot(normalForOwner, upDirection) >= kGroundCosine) {
        body->isGrounded = true;
        body->groundNormal = normalForOwner;
    }
}

void CollisionManager::SetPhysicsParameters(const Vector3& gravity, float fixedTimeStep) {
    const float gravityLength = Calculation::Length(gravity);
    upDirection = gravityLength > 0.0f ? gravity * (-1.0f / gravityLength) : Vector3{ 0.0f, 0.0f, 0.0f };
    // 重力で 2 ステップ分落ちた速さより遅いぶつかりは「置いてあるだけ」とみなす
    restingSpeed = gravityLength * fixedTimeStep * 2.0f;
}

void CollisionManager::SendEvents(const ContactMap& current, const std::unordered_set<const GameObject*>& alive) {
    // 先に「前のフレーム」を手元へ移しておく（通知の中で何が起きても、ここで使うデータは変わらない）
    const ContactMap previous = std::move(previousContacts);
    previousContacts = current;

    auto notifyBoth = [&alive](const PairKey& key, const Contact& contact, EventKind kind) {
        // 受け取る側から見た向きにする（b には法線を反対にして渡す）
        Dispatch(contact.ownerA, kind, { contact.ownerB, key.a, key.b, contact.normal, contact.depth }, alive);
        Dispatch(contact.ownerB, kind, { contact.ownerA, key.b, key.a, contact.normal * -1.0f, contact.depth }, alive);
    };

    // 前は当たっていて、今は当たっていない → Exit
    for (const auto& [key, contact] : previous) {
        if (!current.contains(key)) {
            notifyBoth(key, contact, EventKind::kExit);
        }
    }
    // 今当たっている → 前も当たっていれば Stay、初めてなら Enter
    for (const auto& [key, contact] : current) {
        notifyBoth(key, contact, previous.contains(key) ? EventKind::kStay : EventKind::kEnter);
    }
}

void CollisionManager::SendPendingExits(const std::unordered_set<const GameObject*>& alive) {
    // 相手が消えた組の Exit。消えた側は nullptr なので Dispatch が何もしない
    const std::vector<PendingExit> exits = std::move(pendingExits);
    pendingExits.clear();
    for (const PendingExit& pending : exits) {
        const Contact& contact = pending.contact;
        Dispatch(contact.ownerA, EventKind::kExit, { contact.ownerB, pending.key.a, pending.key.b, contact.normal, contact.depth }, alive);
        Dispatch(contact.ownerB, EventKind::kExit, { contact.ownerA, pending.key.b, pending.key.a, contact.normal * -1.0f, contact.depth }, alive);
    }
}
