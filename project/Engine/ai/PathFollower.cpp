#include "PathFollower.h"
#include <cmath>

void PathFollower::SetPath(const std::vector<Vector3>& newPath, NavPlane newPlane) {
    path = newPath;
    plane = newPlane;
    // 最初の点は出発した位置なので、次の点から向かう
    currentIndex = path.size() > 1 ? 1 : path.size();
}

void PathFollower::Clear() {
    path.clear();
    currentIndex = 0;
}

float PathFollower::PlanarDistance(const Vector3& a, const Vector3& b) const {
    const float dx = a.x - b.x;
    const float db = plane == NavPlane::kXY ? a.y - b.y : a.z - b.z;
    return std::sqrt(dx * dx + db * db);
}

Vector3 PathFollower::Update(const Vector3& position) {
    while (currentIndex < path.size() && PlanarDistance(position, path[currentIndex]) <= arriveDistance) {
        ++currentIndex;
    }
    if (IsFinished()) {
        return { 0.0f, 0.0f, 0.0f };
    }
    const Vector3& target = path[currentIndex];
    const float distance = PlanarDistance(position, target);
    if (distance <= 0.0f) {
        return { 0.0f, 0.0f, 0.0f };
    }
    if (plane == NavPlane::kXY) {
        return { (target.x - position.x) / distance, (target.y - position.y) / distance, 0.0f };
    }
    return { (target.x - position.x) / distance, 0.0f, (target.z - position.z) / distance };
}

Vector3 PathFollower::GetCurrentTarget() const {
    if (path.empty()) {
        return { 0.0f, 0.0f, 0.0f };
    }
    return path[currentIndex < path.size() ? currentIndex : path.size() - 1];
}

float PathFollower::GetRemainingDistance(const Vector3& position) const {
    if (IsFinished()) {
        return 0.0f;
    }
    float distance = PlanarDistance(position, path[currentIndex]);
    for (size_t i = currentIndex; i + 1 < path.size(); ++i) {
        distance += PlanarDistance(path[i], path[i + 1]);
    }
    return distance;
}
