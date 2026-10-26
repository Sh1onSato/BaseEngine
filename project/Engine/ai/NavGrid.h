#pragma once
#include "Structs.h"
#include <cstdint>
#include <vector>

class GameObjectManager;

// 格子を敷く平面
enum class NavPlane {
    kXY, // 2D（ゲーム座標。y は下向き）
    kXZ, // 3D（地面。高さ y は origin.y で一定）
};

// FindPath の細かい設定
struct NavPathOptions {
    bool allowDiagonal = true; // 斜めにも進む（壁の角はすり抜けない）
    bool smooth = true;        // 見通せる所は真っすぐつなぎ、格子のカクカクを減らす
};

/// <summary>
/// 経路探索用の格子（マス目）。マスごとに「歩けるか」と「通りにくさ（コスト）」を持ち、A* で道を探す。
/// 2D（Tiled のマップなど）でも 3D（平らな地面）でも使える。高さの違う床（坂・2階）はまだ扱えない。
///
/// 使い方（3D）:
///   NavGrid grid;
///   grid.Initialize({ -12.0f, 0.0f, -9.0f }, 48, 36, 0.5f, NavPlane::kXZ); // 左手前の角・マスの数・1マスの大きさ
///   grid.BlockColliders(objects, 0.5f);         // 動かない物のコライダーのある所を歩けなくする（0.5 = キャラクターの半径）
///   std::vector<Vector3> path;
///   if (grid.FindPath(enemyPosition, playerPosition, path)) { follower.SetPath(path); }
/// 使い方（2D・Tiled）:
///   grid.Initialize({ 0.0f, 0.0f, 0.0f }, map.width, map.height, static_cast<float>(map.tileWidth), NavPlane::kXY);
///   grid.BlockColliders(objects, 12.0f);        // Tiled の壁（"collision"）も Static のコライダーなので同じ書き方
///
/// 地形が変わったら（扉が開いた等）SetWalkable で直すか、ClearBlocked → BlockColliders で作り直す。
/// 横スクロール（ジャンプで移動する）の道探しには向かない（歩いて行ける所をつなぐ物）。
/// </summary>
class NavGrid {
public:
    // origin: マス (0, 0) の角（2D は左上、3D は x・z が一番小さい角。3D の y は道の高さ）
    void Initialize(const Vector3& origin, int width, int height, float cellSize, NavPlane plane);

    int GetWidth() const { return width; }
    int GetHeight() const { return height; }
    float GetCellSize() const { return cellSize; }
    NavPlane GetPlane() const { return plane; }
    bool IsInside(int x, int y) const { return x >= 0 && y >= 0 && x < width && y < height; }

    // マスの歩けるか（範囲の外は歩けない扱い）
    bool IsWalkable(int x, int y) const;
    void SetWalkable(int x, int y, bool walkable);
    // マスの通りにくさ（1 が普通。2 なら 2 倍遠回りでも避ける。沼・草むらなど）
    float GetCost(int x, int y) const;
    void SetCost(int x, int y, float cost);
    // 全部を歩ける・コスト 1 に戻す
    void ClearBlocked();

    /// <summary>
    /// objects の中の、動かない（Static）コライダーと重なるマスを歩けなくする。
    /// agentRadius: 歩くキャラクターの半径。壁からこの距離より近いマスも歩けなくなる（道が壁に擦らない）。
    /// 3D は高さ origin.y + kFloorClearance 〜 + agentHeight の範囲に物があるマスだけ（床そのものは邪魔にしない）。
    /// layerMask: 対象にするコライダーのレイヤー（ビット）。includeDynamic = true なら Dynamic の物（箱など）も邪魔にする
    /// </summary>
    void BlockColliders(const GameObjectManager& objects, float agentRadius, float agentHeight = 2.0f,
                        uint32_t layerMask = 0xFFFFFFFF, bool includeDynamic = false);

    // ワールド座標 → マス（範囲の外なら false）
    bool WorldToCell(const Vector3& position, int& outX, int& outY) const;
    // マスの中心のワールド座標（3D の y は origin.y）
    Vector3 CellToWorld(int x, int y) const;
    // position に一番近い歩けるマス（見つからなければ false）。目的地が壁の中のときなどに使う
    bool FindNearestWalkable(const Vector3& position, int& outX, int& outY) const;

    /// <summary>
    /// start から goal までの道を探す。見つかれば outPath に通る点（最初は start、最後は goal）を入れて true。
    /// start・goal が歩けないマスなら、近くの歩けるマスから・までにする。届かなければ false（outPath は空）。
    /// </summary>
    bool FindPath(const Vector3& start, const Vector3& goal, std::vector<Vector3>& outPath,
                  const NavPathOptions& options = {}) const;
    // 2マスの間をまっすぐ歩けるか（間のマスが全部歩けるか）
    bool HasLineOfSight(int fromX, int fromY, int toX, int toY) const;

    // 歩けないマスを DebugDraw で描く（3D は道の高さに四角、2D は Rect2D）
    void DrawDebug(bool showWalkable = false) const;
    // 道を DebugDraw で描く
    static void DrawPath(const std::vector<Vector3>& path, NavPlane plane, const Vector4& color);

    // 3D の BlockColliders で、床として無視する高さ（道の高さからこれより上にある物だけ邪魔にする）
    static constexpr float kFloorClearance = 0.05f;

private:
    int ToIndex(int x, int y) const { return y * width + x; }
    // マスの範囲（3D はワールドの x・z、2D は x・y）
    void GetCellBounds(int x, int y, float& minA, float& minB, float& maxA, float& maxB) const;
    // A* 本体。見つかれば outCells にマスの番号を goal → start の逆順で入れる
    bool SearchCells(int startIndex, int goalIndex, bool allowDiagonal, std::vector<int>& outCells) const;

    Vector3 origin = { 0.0f, 0.0f, 0.0f };
    int width = 0;
    int height = 0;
    float cellSize = 1.0f;
    NavPlane plane = NavPlane::kXZ;
    std::vector<uint8_t> walkable; // 1 = 歩ける
    std::vector<float> costs;

    // A* の作業場所（毎回確保し直さないように持っておく。FindPath は const だが中身は使い回す）
    struct SearchNode {
        float g = 0.0f;
        int parent = -1;
        uint32_t visitedStamp = 0; // この値が今回の探索の番号なら g・parent が有効
        uint32_t closedStamp = 0;  // この値が今回の探索の番号なら確定済み
    };
    mutable std::vector<SearchNode> nodes;
    mutable uint32_t searchStamp = 0;
};
