#include "TiledLoader.h"
#include "JsonParser.h"
#include "Logger.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <sstream>
#include <stdexcept>

namespace {
    [[noreturn]] void Fail(const std::string& message) {
        throw std::runtime_error(message);
    }

    std::string ReadTextFile(const std::string& filePath) {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            Fail("ファイルを開けません: " + filePath);
        }
        std::stringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }

    JsonValue ParseJsonFile(const std::string& filePath) {
        try {
            return JsonParser::Parse(ReadTextFile(filePath));
        } catch (const std::runtime_error& e) {
            Fail("JSON の書き方が間違っています: " + filePath + " (" + e.what() + ")");
        }
    }

    // base のファイルがあるフォルダから見た relative のパス（"../" も直す）
    std::string ResolvePath(const std::string& baseFile, const std::string& relative) {
        const std::filesystem::path directory = std::filesystem::path(baseFile).parent_path();
        return (directory / relative).lexically_normal().generic_string();
    }

    float GetFloat(const JsonValue& json, const char* key, float defaultValue = 0.0f) {
        return json[key].IsNumber() ? static_cast<float>(json[key].AsNumber()) : defaultValue;
    }
    int GetInt(const JsonValue& json, const char* key, int defaultValue = 0) {
        return json[key].IsNumber() ? static_cast<int>(json[key].AsNumber()) : defaultValue;
    }
    std::string GetString(const JsonValue& json, const char* key) {
        return json[key].IsString() ? json[key].AsString() : std::string();
    }
    bool GetBool(const JsonValue& json, const char* key, bool defaultValue) {
        return json[key].IsBool() ? json[key].AsBool() : defaultValue;
    }

    // Tiled の properties（[{ "name", "type", "value" }, ...]）を { "名前": 値 } の形にする
    JsonValue ToPropertyObject(const JsonValue& properties) {
        JsonValue::Object object;
        for (const JsonValue& property : properties.AsArray()) {
            object.emplace_back(GetString(property, "name"), property["value"]);
        }
        return JsonValue(std::move(object));
    }

    // 元の JSON に独自の項目を足す（同じ名前があれば独自の項目を先に見つける）
    JsonValue MergeProperties(const JsonValue& json) {
        JsonValue::Object merged = ToPropertyObject(json["properties"]).AsObject();
        for (const auto& entry : json.AsObject()) {
            merged.push_back(entry);
        }
        return JsonValue(std::move(merged));
    }

    std::vector<uint8_t> DecodeBase64(const std::string& text) {
        auto decodeChar = [](char c) -> int {
            if (c >= 'A' && c <= 'Z') return c - 'A';
            if (c >= 'a' && c <= 'z') return c - 'a' + 26;
            if (c >= '0' && c <= '9') return c - '0' + 52;
            if (c == '+') return 62;
            if (c == '/') return 63;
            return -1;
        };
        std::vector<uint8_t> result;
        int buffer = 0;
        int bits = 0;
        for (char c : text) {
            const int value = decodeChar(c);
            if (value < 0) continue;
            buffer = ((buffer << 6) | value) & 0xFFFFFF;
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                result.push_back(static_cast<uint8_t>((buffer >> bits) & 0xFF));
            }
        }
        return result;
    }

    TiledObject ParseObject(const JsonValue& json) {
        TiledObject object;
        object.id = GetInt(json, "id");
        object.name = GetString(json, "name");
        // Tiled 1.9 以降は "class"（オブジェクトは "type" のまま書き出す版もある）
        object.className = GetString(json, "class");
        if (object.className.empty()) {
            object.className = GetString(json, "type");
        }
        object.x = GetFloat(json, "x");
        object.y = GetFloat(json, "y");
        object.width = GetFloat(json, "width");
        object.height = GetFloat(json, "height");
        object.rotation = GetFloat(json, "rotation");
        object.visible = GetBool(json, "visible", true);
        if (json["gid"].IsNumber()) {
            object.gid = static_cast<uint32_t>(json["gid"].AsNumber());
            object.shape = TiledShape::kTile;
        } else if (GetBool(json, "point", false)) {
            object.shape = TiledShape::kPoint;
        } else if (GetBool(json, "ellipse", false)) {
            object.shape = TiledShape::kEllipse;
        } else if (json["polygon"].IsArray() || json["polyline"].IsArray()) {
            const bool isPolygon = json["polygon"].IsArray();
            object.shape = isPolygon ? TiledShape::kPolygon : TiledShape::kPolyline;
            for (const JsonValue& point : json[isPolygon ? "polygon" : "polyline"].AsArray()) {
                object.points.push_back({ GetFloat(point, "x"), GetFloat(point, "y") });
            }
        }
        object.raw = MergeProperties(json);
        return object;
    }

    // tileset の JSON（マップの中の物か .tsj）を読む。baseFile は画像のパスの基準になるファイル
    void ParseTilesetBody(const JsonValue& json, const std::string& baseFile, TiledTileset& tileset) {
        tileset.name = GetString(json, "name");
        if (json["image"].IsString()) {
            tileset.imagePath = ResolvePath(baseFile, json["image"].AsString());
        } else {
            Logger::Log("TiledLoader: 画像1枚のタイルセット以外（画像の集まり）は非対応です: " + tileset.name + "\n");
        }
        tileset.imageWidth = GetInt(json, "imagewidth");
        tileset.imageHeight = GetInt(json, "imageheight");
        tileset.tileWidth = GetInt(json, "tilewidth");
        tileset.tileHeight = GetInt(json, "tileheight");
        tileset.tileCount = GetInt(json, "tilecount");
        tileset.columns = GetInt(json, "columns");
        tileset.margin = GetInt(json, "margin");
        tileset.spacing = GetInt(json, "spacing");
        tileset.properties = ToPropertyObject(json["properties"]);
        for (const JsonValue& tileJson : json["tiles"].AsArray()) {
            TiledTileInfo info;
            info.localId = static_cast<uint32_t>(GetInt(tileJson, "id"));
            info.className = GetString(tileJson, "class");
            if (info.className.empty()) {
                info.className = GetString(tileJson, "type");
            }
            info.properties = ToPropertyObject(tileJson["properties"]);
            tileset.tiles.push_back(std::move(info));
        }
    }

    TiledTileset ParseTileset(const JsonValue& json, const std::string& mapFile) {
        TiledTileset tileset;
        tileset.firstGid = static_cast<uint32_t>(GetInt(json, "firstgid", 1));
        if (json["source"].IsString()) {
            // 外部のタイルセット（複数のマップで同じタイルセットを使うとき）
            const std::string sourcePath = ResolvePath(mapFile, json["source"].AsString());
            if (std::filesystem::path(sourcePath).extension() != ".tsj" && std::filesystem::path(sourcePath).extension() != ".json") {
                Fail("タイルセットは JSON（.tsj）で保存してください（Tiled でタイルセットを開いて 名前を付けて保存 → JSON）: " + sourcePath);
            }
            ParseTilesetBody(ParseJsonFile(sourcePath), sourcePath, tileset);
        } else {
            ParseTilesetBody(json, mapFile, tileset);
        }
        return tileset;
    }

    std::vector<uint32_t> ParseTileData(const JsonValue& layerJson) {
        std::vector<uint32_t> tiles;
        const JsonValue& data = layerJson["data"];
        if (data.IsArray()) {
            tiles.reserve(data.Size());
            for (const JsonValue& value : data.AsArray()) {
                tiles.push_back(static_cast<uint32_t>(value.AsNumber()));
            }
            return tiles;
        }
        if (GetString(layerJson, "encoding") == "base64") {
            if (!GetString(layerJson, "compression").empty()) {
                Fail("圧縮したタイルデータは非対応です。Tiled の マップ > マップのプロパティ で "
                     "タイルレイヤーの形式（Tile Layer Format）を CSV にしてください: " + GetString(layerJson, "name"));
            }
            const std::vector<uint8_t> bytes = DecodeBase64(data.AsString());
            tiles.resize(bytes.size() / 4);
            std::memcpy(tiles.data(), bytes.data(), tiles.size() * 4); // リトルエンディアンの uint32
            return tiles;
        }
        Fail("タイルレイヤーのデータが読めません: " + GetString(layerJson, "name"));
    }

    // レイヤーを読んで layers に足す（グループの中身は、ずらす量・見える・濃さを親と合成して平らに並べる）
    void ParseLayers(const JsonValue& layersJson, const Vector2& parentOffset, bool parentVisible, float parentOpacity,
                     std::vector<TiledLayer>& layers) {
        for (const JsonValue& layerJson : layersJson.AsArray()) {
            const std::string type = GetString(layerJson, "type");
            const Vector2 offset = { parentOffset.x + GetFloat(layerJson, "offsetx"), parentOffset.y + GetFloat(layerJson, "offsety") };
            const bool visible = parentVisible && GetBool(layerJson, "visible", true);
            const float opacity = parentOpacity * GetFloat(layerJson, "opacity", 1.0f);
            if (type == "group") {
                ParseLayers(layerJson["layers"], offset, visible, opacity, layers);
                continue;
            }

            TiledLayer layer;
            layer.name = GetString(layerJson, "name");
            layer.className = GetString(layerJson, "class");
            layer.visible = visible;
            layer.opacity = opacity;
            layer.offset = offset;
            layer.properties = ToPropertyObject(layerJson["properties"]);
            if (type == "tilelayer") {
                layer.type = TiledLayerType::kTile;
                layer.width = GetInt(layerJson, "width");
                layer.height = GetInt(layerJson, "height");
                layer.tiles = ParseTileData(layerJson);
                if (layer.tiles.size() != static_cast<size_t>(layer.width) * static_cast<size_t>(layer.height)) {
                    Fail("タイルレイヤーのタイルの数が大きさと合いません: " + layer.name);
                }
            } else if (type == "objectgroup") {
                layer.type = TiledLayerType::kObject;
                for (const JsonValue& objectJson : layerJson["objects"].AsArray()) {
                    layer.objects.push_back(ParseObject(objectJson));
                }
            } else {
                layer.type = TiledLayerType::kImage;
            }
            layers.push_back(std::move(layer));
        }
    }

    TiledMap ParseMap(const std::string& filePath) {
        const JsonValue json = ParseJsonFile(filePath);
        if (GetBool(json, "infinite", false)) {
            Fail("無限マップは非対応です。Tiled の マップ > マップのプロパティ で 無限（Infinite）のチェックを外してください: " + filePath);
        }
        const std::string orientation = GetString(json, "orientation");
        if (!orientation.empty() && orientation != "orthogonal") {
            Fail("平らな（orthogonal）マップ以外は非対応です（" + orientation + "）: " + filePath);
        }

        TiledMap map;
        map.filePath = filePath;
        map.width = GetInt(json, "width");
        map.height = GetInt(json, "height");
        map.tileWidth = GetInt(json, "tilewidth");
        map.tileHeight = GetInt(json, "tileheight");
        map.properties = ToPropertyObject(json["properties"]);
        for (const JsonValue& tilesetJson : json["tilesets"].AsArray()) {
            map.tilesets.push_back(ParseTileset(tilesetJson, filePath));
        }
        // gid から探しやすいよう、最初の番号の小さい順にしておく
        std::sort(map.tilesets.begin(), map.tilesets.end(),
                  [](const TiledTileset& a, const TiledTileset& b) { return a.firstGid < b.firstGid; });
        ParseLayers(json["layers"], { 0.0f, 0.0f }, true, 1.0f, map.layers);
        return map;
    }
}

// ===== TiledData.h の関数 =====

Vector2 TiledObject::GetCenter() const {
    if (shape == TiledShape::kPoint) {
        return { x, y };
    }
    if (shape == TiledShape::kPolygon || shape == TiledShape::kPolyline) {
        if (points.empty()) {
            return { x, y };
        }
        Vector2 minPoint = points.front();
        Vector2 maxPoint = points.front();
        for (const Vector2& point : points) {
            minPoint = { (std::min)(minPoint.x, point.x), (std::min)(minPoint.y, point.y) };
            maxPoint = { (std::max)(maxPoint.x, point.x), (std::max)(maxPoint.y, point.y) };
        }
        return { x + (minPoint.x + maxPoint.x) * 0.5f, y + (minPoint.y + maxPoint.y) * 0.5f };
    }
    // 四角・楕円は左上、タイルの物は左下が x, y で、そこを中心に時計回りに回っている
    const float localX = width * 0.5f;
    const float localY = shape == TiledShape::kTile ? -height * 0.5f : height * 0.5f;
    const float radian = rotation * std::numbers::pi_v<float> / 180.0f;
    const float c = std::cos(radian);
    const float s = std::sin(radian);
    return { x + localX * c - localY * s, y + localX * s + localY * c };
}

Vector2 TiledTileset::GetTileOrigin(uint32_t localId) const {
    const int perRow = (std::max)(columns, 1);
    const int column = static_cast<int>(localId) % perRow;
    const int row = static_cast<int>(localId) / perRow;
    return { static_cast<float>(margin + column * (tileWidth + spacing)), static_cast<float>(margin + row * (tileHeight + spacing)) };
}

const TiledTileInfo* TiledTileset::FindTileInfo(uint32_t localId) const {
    for (const TiledTileInfo& info : tiles) {
        if (info.localId == localId) {
            return &info;
        }
    }
    return nullptr;
}

const TiledTileset* TiledMap::FindTileset(uint32_t gid) const {
    const uint32_t id = TiledGid::GetId(gid);
    const TiledTileset* found = nullptr;
    for (const TiledTileset& tileset : tilesets) {
        if (tileset.firstGid <= id) {
            found = &tileset;
        }
    }
    return id == 0 ? nullptr : found;
}

const TiledLayer* TiledMap::FindLayer(const std::string& name) const {
    for (const TiledLayer& layer : layers) {
        if (layer.name == name) {
            return &layer;
        }
    }
    return nullptr;
}

// ===== TiledLoader =====

TiledMap TiledLoader::Load(const std::string& filePath) {
    TiledMap map;
    std::string error;
    if (!TryLoad(filePath, map, &error)) {
        Logger::Fatal("TiledLoader: " + error);
    }
    return map;
}

bool TiledLoader::TryLoad(const std::string& filePath, TiledMap& outMap, std::string* errorMessage) {
    try {
        outMap = ParseMap(filePath);
        return true;
    } catch (const std::runtime_error& e) {
        Logger::Log(std::string("TiledLoader: ") + e.what() + "\n");
        if (errorMessage) {
            *errorMessage = e.what();
        }
        return false;
    }
}
