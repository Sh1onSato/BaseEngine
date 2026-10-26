#pragma once
#include "NavGrid.h"
#include "Structs.h"
#include <cstddef>
#include <vector>

/// <summary>
/// NavGrid::FindPath で見つけた道に沿って進むための手伝い。今の位置を渡すと「次に進む向き」を返す。
/// 動かし方（速さ・Rigidbody の速度に入れる等）はキャラクター側で決める。
///
/// 使い方:
///   if (grid.FindPath(position, target, path)) { follower.SetPath(path, NavPlane::kXZ); }
///   Vector3 direction = follower.Update(transform.translate);   // 着いたら { 0, 0, 0 }
///   body->velocity.x = direction.x * speed;
///   body->velocity.z = direction.z * speed;
/// 相手が動くときは、0.3〜0.5 秒ごとなど時々 FindPath し直して SetPath する（毎フレームは重い）。
/// </summary>
class PathFollower {
public:
    void SetPath(const std::vector<Vector3>& newPath, NavPlane newPlane);
    void Clear();

    // position から次に向かう点への向き（長さ 1。2D は z = 0、3D は y = 0）。
    // 点に arriveDistance より近づいたら次の点へ進む。最後の点に着いたら { 0, 0, 0 } を返し、IsFinished が true になる
    Vector3 Update(const Vector3& position);

    bool HasPath() const { return !path.empty(); }
    bool IsFinished() const { return path.empty() || currentIndex >= path.size(); }
    // 今向かっている点（終わっていたら最後の点。道が無ければ { 0, 0, 0 }）
    Vector3 GetCurrentTarget() const;
    // position から最後の点までの、道に沿った残りの距離
    float GetRemainingDistance(const Vector3& position) const;
    const std::vector<Vector3>& GetPath() const { return path; }

    // 点に着いたと見なす距離（既定 0.2。2D のピクセル座標なら 4 など大きめにする）
    void SetArriveDistance(float distance) { arriveDistance = distance; }
    float GetArriveDistance() const { return arriveDistance; }

private:
    // 平面の上の距離（3D は高さを無視する）
    float PlanarDistance(const Vector3& a, const Vector3& b) const;

    std::vector<Vector3> path;
    NavPlane plane = NavPlane::kXZ;
    size_t currentIndex = 0;
    float arriveDistance = 0.2f;
};
