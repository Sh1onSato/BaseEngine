#pragma once
#include "JsonValue.h"
#include "Structs.h"
#include <cstdint>
#include <string>
#include <vector>

// Tiled（2D のマップエディタ）の .tmj（JSON 形式のマップ）を TiledLoader で読んだデータ。
// 座標は Tiled のピクセルのまま（左上が 0,0、y は下向き）。エンジンの 2D のゲーム座標と同じ向きなので、そのまま使える。

// タイル番号（GID）に付いている反転の印（Tiled で タイルを反転・回転して置くと付く）
namespace TiledGid {
    constexpr uint32_t kFlipHorizontal = 0x80000000u;
    constexpr uint32_t kFlipVertical = 0x40000000u;
    constexpr uint32_t kFlipDiagonal = 0x20000000u;
    constexpr uint32_t kRotatedHex = 0x10000000u;
    // 印を外した本当のタイル番号（0 なら空き）
    inline uint32_t GetId(uint32_t gid) { return gid & ~(kFlipHorizontal | kFlipVertical | kFlipDiagonal | kRotatedHex); }
}

// オブジェクトの形
enum class TiledShape {
    kRectangle, // 四角（width / height）
    kEllipse,   // 楕円（四角に内接。エンジンでは円として扱う）
    kPoint,     // 点（出現位置など）
    kPolygon,   // 多角形（points。当たり判定は外枠の四角）
    kPolyline,  // 折れ線（points）
    kTile,      // タイルを置いた物（gid。x, y はタイルの左下）
};

// オブジェクトレイヤーに置いた物1つ
struct TiledObject {
    int id = 0;
    std::string name;
    // Tiled の Class（古い Tiled では Type）
    std::string className;
    TiledShape shape = TiledShape::kRectangle;
    // Tiled の x, y, width, height（ピクセル）。タイルの物は x, y が左下
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    // 時計回りの度
    float rotation = 0.0f;
    bool visible = true;
    // タイルの物のタイル番号（反転の印付き。タイルの物でなければ 0）
    uint32_t gid = 0;
    // 多角形・折れ線の頂点（x, y からの相対）
    std::vector<Vector2> points;
    // 元の JSON に、独自の項目（properties）を "名前": 値 として足した物（例: raw["score"].AsNumber()）
    JsonValue raw;

    // 物の中心（四角・楕円・タイルの物。点は x, y、多角形は頂点の外枠の中心）
    Vector2 GetCenter() const;
};

// タイルセットの中の、1つのタイルの情報（Tiled のタイルセットエディタで設定した物だけ）
struct TiledTileInfo {
    uint32_t localId = 0;
    std::string className;
    JsonValue properties; // "名前": 値 のオブジェクト
};

// タイルセット（タイルを並べた1枚の画像）
struct TiledTileset {
    uint32_t firstGid = 1;
    std::string name;
    // 画像のパス（読み込みにそのまま使える形。マップか .tsj の場所からの相対を直してある）
    std::string imagePath;
    int imageWidth = 0;
    int imageHeight = 0;
    int tileWidth = 0;
    int tileHeight = 0;
    int tileCount = 0;
    int columns = 0;
    int margin = 0;
    int spacing = 0;
    std::vector<TiledTileInfo> tiles;
    JsonValue properties;

    // localId のタイルの、画像の中での左上（ピクセル）
    Vector2 GetTileOrigin(uint32_t localId) const;
    // localId のタイルの情報（設定されていなければ nullptr）
    const TiledTileInfo* FindTileInfo(uint32_t localId) const;
};

enum class TiledLayerType {
    kTile,   // タイルを敷き詰めたレイヤー
    kObject, // 物を置いたレイヤー
    kImage,  // 画像1枚のレイヤー（今は読むだけで描かない）
};

// レイヤー1つ（グループレイヤーの中身は、ずらす量を足して平らに並べてある）
struct TiledLayer {
    TiledLayerType type = TiledLayerType::kTile;
    std::string name;
    std::string className;
    bool visible = true;
    float opacity = 1.0f;
    // レイヤーのずれ（ピクセル。グループの分も足してある）
    Vector2 offset = { 0.0f, 0.0f };
    // タイルレイヤー: width x height 個のタイル番号（左上から行ごと。0 は空き、反転の印付き）
    int width = 0;
    int height = 0;
    std::vector<uint32_t> tiles;
    // オブジェクトレイヤー
    std::vector<TiledObject> objects;
    JsonValue properties;
};

// マップ全体
struct TiledMap {
    std::string filePath;
    // タイルの数（横・縦）と1枚の大きさ（ピクセル）
    int width = 0;
    int height = 0;
    int tileWidth = 0;
    int tileHeight = 0;
    std::vector<TiledLayer> layers;
    std::vector<TiledTileset> tilesets;
    JsonValue properties;

    // gid（反転の印付きでもよい）が入っているタイルセット（無ければ nullptr）
    const TiledTileset* FindTileset(uint32_t gid) const;
    // 名前のレイヤー（無ければ nullptr）
    const TiledLayer* FindLayer(const std::string& name) const;
    // マップ全体の大きさ（ピクセル）
    Vector2 GetPixelSize() const { return { static_cast<float>(width * tileWidth), static_cast<float>(height * tileHeight) }; }
};
