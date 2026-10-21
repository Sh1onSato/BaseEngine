#pragma once
#include "Collider.h"
#include "Collision.h"
#include "Structs.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

class GameObject;

/// <summary>
/// GameObject に付いたコライダー同士の当たり判定をまとめて行うクラス。
/// GameObjectManager が1つ持ち、GameObjectManager::Update の中で自動的に呼ばれる（自分で Update を呼ぶ必要はない）。
/// シーンからは objects.GetCollisionManager() で取り出して、レイヤー設定やレイキャストに使う。
///
/// 1回の Update の流れ:
///   1. 有効なコライダーを全部集める（非アクティブ・Destroy済みの物、無効なコライダーは除く）
///   2. 大まかな判定（ワールド上の囲み箱を X 座標で並べて、重なりそうな組だけ選ぶ）
///   3. 細かい判定（形の組み合わせごとの関数で、当たっているか・めり込みの量を調べる）
///   4. 押し戻し（どちらも Trigger でないとき。Dynamic×Static は Dynamic だけ、Dynamic 同士は半分ずつ動かす。
///      Rigidbody がある物は質量に応じて分ける）。押し戻しは1回だけ行う（多数が押し合うと少しめり込みが残ることがあるが、次のフレームでまた直る）
///      Rigidbody がある物は、さらに速度を変える（跳ね返り・摩擦）と接地判定を行う
///   5. 前のフレームと比べて OnCollisionEnter / Stay / Exit を両方の持ち主に送る（Trigger でも送る）
///
/// 判定できない組み合わせ: 2D と 3D、Static 同士、OBB とカプセル（ログを1回だけ出して当たらない扱い）
/// </summary>
class CollisionManager {
public:
    static constexpr uint32_t kLayerCount = 32;

    CollisionManager();

    // 1フレーム分の判定・押し戻し・通知をまとめて行う（BeginFrame → Step → EndFrame と同じ）。
    // objects には Destroy されていない物を全部渡す（非アクティブも含む）
    void Update(const std::vector<GameObject*>& objects);

    // --- 物理の固定間隔に合わせて分けた版（GameObjectManager が使う） ---
    // フレームの最初に1回（このフレームに当たった組の記録を空にする）
    void BeginFrame();
    // 判定と押し戻しを1回。物理の1ステップごとに呼ぶ（1フレームに0〜数回）。当たった組はフレーム分まとめて覚える
    void Step(const std::vector<GameObject*>& objects);
    // フレームの最後に1回。このフレームに1回でも当たった組で Enter / Stay / Exit を送る
    void EndFrame(const std::vector<GameObject*>& objects);
    // 物理の設定（GameObjectManager が毎フレーム渡す）。gravity は接地判定の「上」の向きに、
    // fixedTimeStep は「これより遅いぶつかりは跳ねさせない」速さの目安に使う
    void SetPhysicsParameters(const Vector3& gravity, float fixedTimeStep);

    // レイヤー a と b が当たるかどうか（対称。既定は全部 true）。例: SetLayerCollision(1, 2, false)
    void SetLayerCollision(uint32_t a, uint32_t b, bool enable);
    bool IsLayerCollisionEnabled(uint32_t a, uint32_t b) const;

    /// <summary>
    /// 一番近くで当たったコライダーを調べる。対象は最後の Update で集めた 球・AABB・OBB のみ
    /// （カプセルと2Dは対象外）。layerMask はビットで指定（例: レイヤー1と3なら (1u << 1) | (1u << 3)、全部なら 0xFFFFFFFF）。
    /// 当たれば true。outHit / outCollider は不要なら nullptr でよい。
    /// </summary>
    bool Raycast(const Ray& ray, float maxDistance, uint32_t layerMask, RaycastHit* outHit, Collider** outCollider = nullptr) const;

    // --- デバッグ表示用（最後の Update の結果） ---
    size_t GetLastColliderCount() const { return lastColliderCount; }
    size_t GetLastBroadphasePairCount() const { return lastBroadphasePairCount; }
    size_t GetLastHitPairCount() const { return lastHitPairCount; }

    // GameObjectManager が物を delete する直前に呼ぶ（消える物を指すデータを残さないため）
    void RemoveObjects(const std::unordered_set<const GameObject*>& removed);
    // 全部忘れる（GameObjectManager::Clear 用。Exit は送らない）
    void Reset();

private:
    // 集めたコライダー1つ分
    struct Entry {
        Collider* collider = nullptr;
        GameObject* owner = nullptr;
        AABB bounds{};
    };

    // 当たっている組（a < b になるよう並べて持つ）
    struct PairKey {
        Collider* a = nullptr;
        Collider* b = nullptr;
        bool operator==(const PairKey& other) const { return a == other.a && b == other.b; }
    };
    struct PairKeyHash {
        size_t operator()(const PairKey& key) const {
            size_t hashA = std::hash<const void*>()(key.a);
            size_t hashB = std::hash<const void*>()(key.b);
            return hashA ^ (hashB + 0x9e3779b9 + (hashA << 6) + (hashA >> 2));
        }
    };
    // 組の情報。normal は a を b から離す向き
    struct Contact {
        GameObject* ownerA = nullptr;
        GameObject* ownerB = nullptr;
        Vector3 normal = { 0.0f, 0.0f, 0.0f };
        float depth = 0.0f;
    };
    using ContactMap = std::unordered_map<PairKey, Contact, PairKeyHash>;
    // 相手が消えたあとで Exit を送る予定の組（消えた側は nullptr）
    struct PendingExit {
        PairKey key;
        Contact contact;
    };

    void GatherColliders(const std::vector<GameObject*>& objects);
    // 大まかな判定。entries の添字の組を返す
    std::vector<std::pair<size_t, size_t>> FindBroadphasePairs() const;
    bool ShouldTest(const Entry& a, const Entry& b) const;
    // 細かい判定と押し戻し。当たった組を返す
    ContactMap TestPairs(const std::vector<std::pair<size_t, size_t>>& pairs);
    void ResolvePenetration(Collider* a, Collider* b, const CollisionInfo& info);
    // 段差の高さが決まった Rigidbody（キャラクター）が、段差の角・急な面に横からぶつかったときの扱い。
    // 乗れる段差なら self を上に乗せて true（押し戻しは済み）。乗れなければ normalForSelf を水平にして depth を合わせる（壁として押す）
    bool ApplyStepLimit(Collider* self, const Collider& other, Vector3& normalForSelf, float& depth) const;
    // 段差の高さが決まった Rigidbody が床・坂に押し返されて得た上向きの速さを消す（坂の頂上で飛び出さないように）
    void LimitGroundLaunch(GameObject* owner, const Vector3& normalForOwner, float upSpeedBefore) const;
    // 段差の高さが決まった Rigidbody が、前のステップは地面にいたのに少し離れたとき、真下の地面へ吸い付ける（下り坂・坂の頂上用）
    void SnapToGround();
    // self 以外のコライダー（Trigger・当たらないレイヤーを除く）へのレイキャスト。地面探し用
    bool RaycastGround(const Ray& ray, float maxDistance, const Entry& self, RaycastHit* outHit) const;
    // 押し戻しで動く割合の重み（0 なら動かない）。Rigidbody があれば 1/質量、無い Dynamic は 1
    static float GetPushWeight(const Collider& collider);
    // ぶつかった2つの速度を変える（跳ね返り・摩擦）。normal は A を B から離す向き。Rigidbody が無い物は止まっている物として扱う
    void ResolveVelocity(GameObject* ownerA, GameObject* ownerB, const Vector3& normal) const;
    // normalForOwner が「上」向きに近ければ owner の Rigidbody を接地中にする
    void UpdateGrounded(GameObject* owner, const Vector3& normalForOwner) const;
    // 通知
    void SendEvents(const ContactMap& current, const std::unordered_set<const GameObject*>& alive);
    void SendPendingExits(const std::unordered_set<const GameObject*>& alive);

    std::vector<Entry> entries;
    ContactMap previousContacts;
    // このフレームの Step で当たった組（同じ組は最後の Step の値で上書き）
    ContactMap frameContacts;
    std::vector<PendingExit> pendingExits;
    // layerMasks[a] のビット b が立っていればレイヤー a と b は当たる
    std::array<uint32_t, kLayerCount> layerMasks{};

    // 物理（SetPhysicsParameters で設定）
    // 重力と反対の向き（長さ1）。重力が 0 なら 0（接地判定をしない）
    Vector3 upDirection = { 0.0f, 1.0f, 0.0f };
    // これより遅いぶつかりは跳ねさせない（床の上で小さく跳ね続けて震えるのを防ぐ）
    float restingSpeed = 0.0f;

    size_t lastColliderCount = 0;
    size_t lastBroadphasePairCount = 0;
    size_t lastHitPairCount = 0;
};
