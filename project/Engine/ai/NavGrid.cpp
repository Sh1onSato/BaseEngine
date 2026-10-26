#include "NavGrid.h"
#include "Calculation.h"
#include "Collider.h"
#include "Collision.h"
#include "DebugDraw.h"
#include "GameObject.h"
#include "GameObjectManager.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <utility>

namespace {
    constexpr float kSqrt2 = 1.41421356f;
    // マスの大きさに対して、ぴったり接しているだけの物を「重なっている」と見なさないための縮め幅
    constexpr float kTouchEpsilon = 1.0e-3f;
    constexpr float kMinCost = 1.0f;

    struct OpenEntry {
        float f;
        int index;
        bool operator>(const OpenEntry& other) const { return f > other.f; }
    };

    // 斜めありならマス目の上の「8方向の最短距離」、無しなら縦横の距離（どちらも実際の距離以下なので A* が最短を返す）
    float Heuristic(int ax, int ay, int bx, int by, bool allowDiagonal) {
        const float dx = static_cast<float>(std::abs(ax - bx));
        const float dy = static_cast<float>(std::abs(ay - by));
        if (!allowDiagonal) {
            return dx + dy;
        }
        return (std::max)(dx, dy) + (kSqrt2 - 1.0f) * (std::min)(dx, dy);
    }
}

void NavGrid::Initialize(const Vector3& newOrigin, int newWidth, int newHeight, float newCellSize, NavPlane newPlane) {
    origin = newOrigin;
    width = (std::max)(newWidth, 1);
    height = (std::max)(newHeight, 1);
    cellSize = (std::max)(newCellSize, 1.0e-4f);
    plane = newPlane;
    const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
    walkable.assign(count, 1);
    costs.assign(count, 1.0f);
    nodes.assign(count, SearchNode{});
    searchStamp = 0;
}

bool NavGrid::IsWalkable(int x, int y) const {
    return IsInside(x, y) && walkable[ToIndex(x, y)] != 0;
}

void NavGrid::SetWalkable(int x, int y, bool isWalkable) {
    if (IsInside(x, y)) {
        walkable[ToIndex(x, y)] = isWalkable ? 1 : 0;
    }
}

float NavGrid::GetCost(int x, int y) const {
    return IsInside(x, y) ? costs[ToIndex(x, y)] : kMinCost;
}

void NavGrid::SetCost(int x, int y, float cost) {
    if (IsInside(x, y)) {
        // 1 より小さいと「近道」の見積もりが狂って最短でない道を返すので、1 以上にする
        costs[ToIndex(x, y)] = (std::max)(cost, kMinCost);
    }
}

void NavGrid::ClearBlocked() {
    std::fill(walkable.begin(), walkable.end(), static_cast<uint8_t>(1));
    std::fill(costs.begin(), costs.end(), 1.0f);
}

void NavGrid::GetCellBounds(int x, int y, float& minA, float& minB, float& maxA, float& maxB) const {
    const float originB = plane == NavPlane::kXY ? origin.y : origin.z;
    minA = origin.x + static_cast<float>(x) * cellSize;
    minB = originB + static_cast<float>(y) * cellSize;
    maxA = minA + cellSize;
    maxB = minB + cellSize;
}

bool NavGrid::WorldToCell(const Vector3& position, int& outX, int& outY) const {
    const float a = position.x - origin.x;
    const float b = plane == NavPlane::kXY ? position.y - origin.y : position.z - origin.z;
    outX = static_cast<int>(std::floor(a / cellSize));
    outY = static_cast<int>(std::floor(b / cellSize));
    return IsInside(outX, outY);
}

Vector3 NavGrid::CellToWorld(int x, int y) const {
    const float a = origin.x + (static_cast<float>(x) + 0.5f) * cellSize;
    if (plane == NavPlane::kXY) {
        return { a, origin.y + (static_cast<float>(y) + 0.5f) * cellSize, 0.0f };
    }
    return { a, origin.y, origin.z + (static_cast<float>(y) + 0.5f) * cellSize };
}

void NavGrid::BlockColliders(const GameObjectManager& objects, float agentRadius, float agentHeight,
                             uint32_t layerMask, bool includeDynamic) {
    const bool is2D = plane == NavPlane::kXY;
    const float shrink = cellSize * kTouchEpsilon;
    for (const GameObject* object : objects.FindAllByType<GameObject>()) {
        if (!object->IsActive() || object->IsDestroyed()) {
            continue;
        }
        for (const std::unique_ptr<Collider>& collider : object->GetColliders()) {
            const ColliderType type = collider->GetType();
            const bool isObstacle = type == ColliderType::kStatic || (includeDynamic && type == ColliderType::kDynamic);
            if (!collider->IsEnabled() || !isObstacle || collider->Is2D() != is2D ||
                (layerMask & (1u << collider->GetCollisionLayer())) == 0) {
                continue;
            }
            // 調べるマスを、コライダーを囲む箱（キャラクターの半径だけ広げた範囲）の中に絞る
            const AABB bounds = collider->GetWorldBounds();
            const float boundsMinB = is2D ? bounds.min.y : bounds.min.z;
            const float boundsMaxB = is2D ? bounds.max.y : bounds.max.z;
            const float originB = is2D ? origin.y : origin.z;
            const int minX = (std::max)(0, static_cast<int>(std::floor((bounds.min.x - agentRadius - origin.x) / cellSize)));
            const int minY = (std::max)(0, static_cast<int>(std::floor((boundsMinB - agentRadius - originB) / cellSize)));
            const int maxX = (std::min)(width - 1, static_cast<int>(std::floor((bounds.max.x + agentRadius - origin.x) / cellSize)));
            const int maxY = (std::min)(height - 1, static_cast<int>(std::floor((boundsMaxB + agentRadius - originB) / cellSize)));

            for (int y = minY; y <= maxY; ++y) {
                for (int x = minX; x <= maxX; ++x) {
                    if (!walkable[ToIndex(x, y)]) {
                        continue;
                    }
                    float minA = 0.0f;
                    float minB = 0.0f;
                    float maxA = 0.0f;
                    float maxB = 0.0f;
                    GetCellBounds(x, y, minA, minB, maxA, maxB);
                    // マスをキャラクターの半径だけ広げた範囲に物があれば、そのマスの中心にキャラクターは立てない
                    minA -= agentRadius - shrink;
                    minB -= agentRadius - shrink;
                    maxA += agentRadius - shrink;
                    maxB += agentRadius - shrink;
                    bool isHit = false;
                    if (is2D) {
                        const Rect cell = { { minA, minB }, { maxA, maxB } };
                        if (collider->GetShape() == ColliderShape::kRect) {
                            isHit = Collision::RectRect(cell, collider->GetWorldRect());
                        } else {
                            isHit = Collision::CircleRect(collider->GetWorldCircle(), cell);
                        }
                    } else {
                        // 高さはキャラクターの足元の少し上から頭まで（床そのものは邪魔にしない）
                        const AABB cell = { { minA, origin.y + kFloorClearance, minB }, { maxA, origin.y + agentHeight, maxB } };
                        switch (collider->GetShape()) {
                        case ColliderShape::kSphere: isHit = Collision::SphereAABB(collider->GetWorldSphere(), cell); break;
                        case ColliderShape::kAABB: isHit = Collision::AABBAABB(cell, collider->GetWorldAABB()); break;
                        case ColliderShape::kCapsule: isHit = Collision::CapsuleAABB(collider->GetWorldCapsule(), cell); break;
                        case ColliderShape::kOBB: {
                            const Vector3 center = (cell.min + cell.max) * 0.5f;
                            const OBB cellBox = Collision::MakeOBB(center, Calculation::IdentityQuaternion(), (cell.max - cell.min) * 0.5f);
                            isHit = Collision::OBBOBB(cellBox, collider->GetWorldOBB());
                            break;
                        }
                        default: break;
                        }
                    }
                    if (isHit) {
                        walkable[ToIndex(x, y)] = 0;
                    }
                }
            }
        }
    }
}

bool NavGrid::FindNearestWalkable(const Vector3& position, int& outX, int& outY) const {
    int centerX = 0;
    int centerY = 0;
    WorldToCell(position, centerX, centerY);
    centerX = std::clamp(centerX, 0, width - 1);
    centerY = std::clamp(centerY, 0, height - 1);
    // 中心のマスから1周ずつ外へ探し、最初に見つかった周の中で一番近いマスにする
    const int maxRadius = (std::max)(width, height);
    for (int radius = 0; radius <= maxRadius; ++radius) {
        float bestDistance = (std::numeric_limits<float>::max)();
        bool found = false;
        for (int y = centerY - radius; y <= centerY + radius; ++y) {
            for (int x = centerX - radius; x <= centerX + radius; ++x) {
                const bool isOnRing = std::abs(x - centerX) == radius || std::abs(y - centerY) == radius;
                if (!isOnRing || !IsWalkable(x, y)) {
                    continue;
                }
                const Vector3 cellCenter = CellToWorld(x, y);
                const float da = cellCenter.x - position.x;
                const float db = plane == NavPlane::kXY ? cellCenter.y - position.y : cellCenter.z - position.z;
                const float distance = da * da + db * db;
                if (distance < bestDistance) {
                    bestDistance = distance;
                    outX = x;
                    outY = y;
                    found = true;
                }
            }
        }
        if (found) {
            return true;
        }
    }
    return false;
}

bool NavGrid::SearchCells(int startIndex, int goalIndex, bool allowDiagonal, std::vector<int>& outCells) const {
    // 探索の番号を進める（前の探索の値は番号が違うので見なくなる。毎回全部を消さなくてよい）
    if (++searchStamp == 0) {
        for (SearchNode& node : nodes) {
            node.visitedStamp = 0;
            node.closedStamp = 0;
        }
        searchStamp = 1;
    }
    const int goalX = goalIndex % width;
    const int goalY = goalIndex / width;

    std::priority_queue<OpenEntry, std::vector<OpenEntry>, std::greater<OpenEntry>> open;
    nodes[startIndex] = { 0.0f, -1, searchStamp, 0 };
    open.push({ Heuristic(startIndex % width, startIndex / width, goalX, goalY, allowDiagonal), startIndex });

    static constexpr int kOffsets[8][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 } };
    const int neighborCount = allowDiagonal ? 8 : 4;
    while (!open.empty()) {
        const int current = open.top().index;
        open.pop();
        SearchNode& currentNode = nodes[current];
        if (currentNode.closedStamp == searchStamp) {
            continue; // 同じマスを後から安く入れ直した古い分
        }
        currentNode.closedStamp = searchStamp;
        if (current == goalIndex) {
            outCells.clear();
            for (int index = goalIndex; index != -1; index = nodes[index].parent) {
                outCells.push_back(index);
            }
            return true;
        }
        const int x = current % width;
        const int y = current / width;
        for (int i = 0; i < neighborCount; ++i) {
            const int nx = x + kOffsets[i][0];
            const int ny = y + kOffsets[i][1];
            if (!IsWalkable(nx, ny)) {
                continue;
            }
            const bool isDiagonal = i >= 4;
            // 斜めは、横と縦の両方のマスが歩けるときだけ（壁の角をすり抜けない）
            if (isDiagonal && (!IsWalkable(nx, y) || !IsWalkable(x, ny))) {
                continue;
            }
            const int next = ToIndex(nx, ny);
            SearchNode& nextNode = nodes[next];
            if (nextNode.closedStamp == searchStamp) {
                continue;
            }
            const float g = currentNode.g + (isDiagonal ? kSqrt2 : 1.0f) * costs[next];
            if (nextNode.visitedStamp == searchStamp && g >= nextNode.g) {
                continue;
            }
            nextNode.g = g;
            nextNode.parent = current;
            nextNode.visitedStamp = searchStamp;
            open.push({ g + Heuristic(nx, ny, goalX, goalY, allowDiagonal), next });
        }
    }
    return false;
}

bool NavGrid::HasLineOfSight(int fromX, int fromY, int toX, int toY) const {
    // マスの中心どうしを結ぶ線が通るマスを、角をかすめるマスも含めて全部調べる
    const int dx = toX - fromX;
    const int dy = toY - fromY;
    const int nx = std::abs(dx);
    const int ny = std::abs(dy);
    const int stepX = dx > 0 ? 1 : -1;
    const int stepY = dy > 0 ? 1 : -1;
    int x = fromX;
    int y = fromY;
    if (!IsWalkable(x, y)) {
        return false;
    }
    for (int ix = 0, iy = 0; ix < nx || iy < ny;) {
        const int decision = (1 + 2 * ix) * ny - (1 + 2 * iy) * nx;
        if (decision == 0) {
            // ちょうど角を通る: 両側のマスも歩けないと通れない
            if (!IsWalkable(x + stepX, y) || !IsWalkable(x, y + stepY)) {
                return false;
            }
            x += stepX;
            y += stepY;
            ++ix;
            ++iy;
        } else if (decision < 0) {
            x += stepX;
            ++ix;
        } else {
            y += stepY;
            ++iy;
        }
        if (!IsWalkable(x, y)) {
            return false;
        }
    }
    return true;
}

bool NavGrid::FindPath(const Vector3& start, const Vector3& goal, std::vector<Vector3>& outPath,
                       const NavPathOptions& options) const {
    outPath.clear();
    if (walkable.empty()) {
        return false;
    }
    // start・goal が範囲の外や壁の中なら、近くの歩けるマスにする
    int startX = 0;
    int startY = 0;
    int goalX = 0;
    int goalY = 0;
    if (!(WorldToCell(start, startX, startY) && IsWalkable(startX, startY)) && !FindNearestWalkable(start, startX, startY)) {
        return false;
    }
    const bool isGoalReachable = WorldToCell(goal, goalX, goalY) && IsWalkable(goalX, goalY);
    if (!isGoalReachable && !FindNearestWalkable(goal, goalX, goalY)) {
        return false;
    }
    // 壁の中の目的地には行けないので、最後は近くの歩けるマスの中心で止まる
    const Vector3 endPoint = isGoalReachable ? goal : CellToWorld(goalX, goalY);

    std::vector<int> cells;
    if (!SearchCells(ToIndex(startX, startY), ToIndex(goalX, goalY), options.allowDiagonal, cells)) {
        return false;
    }
    std::reverse(cells.begin(), cells.end()); // start → goal の順にする

    // 見通せるマスは飛ばして、曲がり角だけ残す（格子のカクカクを減らす）
    std::vector<int> corners;
    if (options.smooth && cells.size() > 2) {
        size_t anchor = 0;
        while (anchor + 1 < cells.size()) {
            size_t farthest = anchor + 1;
            for (size_t i = cells.size() - 1; i > anchor + 1; --i) {
                if (HasLineOfSight(cells[anchor] % width, cells[anchor] / width, cells[i] % width, cells[i] / width)) {
                    farthest = i;
                    break;
                }
            }
            if (farthest + 1 < cells.size()) {
                corners.push_back(cells[farthest]);
            }
            anchor = farthest;
        }
    } else if (cells.size() > 2) {
        corners.assign(cells.begin() + 1, cells.end() - 1);
    }

    outPath.push_back(start);
    for (int index : corners) {
        outPath.push_back(CellToWorld(index % width, index / width));
    }
    outPath.push_back(endPoint);
    return true;
}

void NavGrid::DrawDebug(bool showWalkable) const {
    if (!DebugDraw::IsEnabled()) {
        return;
    }
    const Vector4 blockedColor = { 1.0f, 0.3f, 0.3f, 0.6f };
    const Vector4 walkableColor = { 0.3f, 1.0f, 0.4f, 0.25f };
    // マスの線が重ならないよう少し内側に描く
    const float inset = cellSize * 0.1f;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const bool isWalkableCell = walkable[ToIndex(x, y)] != 0;
            if (isWalkableCell && !showWalkable) {
                continue;
            }
            const Vector4& color = isWalkableCell ? walkableColor : blockedColor;
            float minA = 0.0f;
            float minB = 0.0f;
            float maxA = 0.0f;
            float maxB = 0.0f;
            GetCellBounds(x, y, minA, minB, maxA, maxB);
            minA += inset;
            minB += inset;
            maxA -= inset;
            maxB -= inset;
            if (plane == NavPlane::kXY) {
                DebugDraw::Rect2D({ { minA, minB }, { maxA, maxB } }, color);
            } else {
                // 床と重ならないよう少しだけ上に、隠れない線で描く
                const float lineY = origin.y + kFloorClearance;
                const Vector3 corners[4] = { { minA, lineY, minB }, { maxA, lineY, minB }, { maxA, lineY, maxB }, { minA, lineY, maxB } };
                for (int i = 0; i < 4; ++i) {
                    DebugDraw::Line(corners[i], corners[(i + 1) % 4], color, 0.0f, false);
                }
            }
        }
    }
}

void NavGrid::DrawPath(const std::vector<Vector3>& path, NavPlane plane, const Vector4& color) {
    for (size_t i = 0; i + 1 < path.size(); ++i) {
        if (plane == NavPlane::kXY) {
            DebugDraw::Line2D({ path[i].x, path[i].y }, { path[i + 1].x, path[i + 1].y }, color);
        } else {
            DebugDraw::Line(path[i], path[i + 1], color, 0.0f, false);
        }
    }
    for (const Vector3& point : path) {
        if (plane == NavPlane::kXY) {
            DebugDraw::Cross2D({ point.x, point.y }, 8.0f, color);
        } else {
            DebugDraw::Cross(point, 0.3f, color, 0.0f, false);
        }
    }
}
