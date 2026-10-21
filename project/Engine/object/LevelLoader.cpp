#include "LevelLoader.h"
#include "JsonParser.h"
#include "Logger.h"
#include <fstream>
#include <numbers>
#include <sstream>
#include <stdexcept>

namespace {

constexpr float kDegToRad = std::numbers::pi_v<float> / 180.0f;

// [x, y, z] を Vector3 にする（足りない要素は fallback の値）
Vector3 ReadVector3(const JsonValue& array, const Vector3& fallback) {
    if (!array.IsArray()) {
        return fallback;
    }
    Vector3 result = fallback;
    if (array[size_t(0)].IsNumber()) { result.x = float(array[size_t(0)].AsNumber()); }
    if (array[size_t(1)].IsNumber()) { result.y = float(array[size_t(1)].AsNumber()); }
    if (array[size_t(2)].IsNumber()) { result.z = float(array[size_t(2)].AsNumber()); }
    return result;
}

// Blender（Z が上）の位置・大きさをエンジン（Y が上）に直す: y と z を入れ替える
Vector3 ToEngineAxis(const Vector3& blender) {
    return { blender.x, blender.z, blender.y };
}

// Blender の回転（度）をエンジンの回転（ラジアン）に直す: 軸を入れ替えて右手系→左手系のため符号を反転
Vector3 ToEngineRotation(const Vector3& blenderDegrees) {
    return { -blenderDegrees.x * kDegToRad, -blenderDegrees.z * kDegToRad, -blenderDegrees.y * kDegToRad };
}

std::string ReadString(const JsonValue& value) {
    return value.IsString() ? value.AsString() : std::string();
}

LevelColliderData ParseCollider(const JsonValue& json) {
    LevelColliderData collider;
    if (!json.IsObject()) {
        return collider;
    }
    const std::string type = ReadString(json["type"]);
    if (type == "BOX") {
        collider.type = LevelColliderType::kBox;
    } else if (type == "SPHERE") {
        collider.type = LevelColliderType::kSphere;
    } else {
        if (!type.empty()) {
            Logger::Log("LevelLoader: 未対応のコライダーの種類です（無視します）: " + type + "\n");
        }
        return collider;
    }
    collider.center = ToEngineAxis(ReadVector3(json["center"], { 0.0f, 0.0f, 0.0f }));
    collider.size = ToEngineAxis(ReadVector3(json["size"], { 1.0f, 1.0f, 1.0f }));
    if (json["radius"].IsNumber()) {
        collider.radius = float(json["radius"].AsNumber());
    }
    return collider;
}

LevelObjectData ParseObject(const JsonValue& json) {
    LevelObjectData object;
    object.type = ReadString(json["type"]);
    object.name = ReadString(json["name"]);
    object.fileName = ReadString(json["file_name"]);
    object.disabled = json["disabled"].IsBool() && json["disabled"].AsBool();

    const JsonValue& transform = json["transform"];
    object.transform.translate = ToEngineAxis(ReadVector3(transform["translation"], { 0.0f, 0.0f, 0.0f }));
    object.transform.rotate = ToEngineRotation(ReadVector3(transform["rotation"], { 0.0f, 0.0f, 0.0f }));
    object.transform.scale = ToEngineAxis(ReadVector3(transform["scaling"], { 1.0f, 1.0f, 1.0f }));

    object.collider = ParseCollider(json["collider"]);

    for (const JsonValue& child : json["children"].AsArray()) {
        object.children.push_back(ParseObject(child));
    }
    object.raw = json;
    return object;
}

} // namespace

namespace LevelLoader {

bool TryLoad(const std::string& filePath, LevelData& outLevel, std::string* errorMessage) {
    auto fail = [&](const std::string& message) {
        Logger::Log("LevelLoader: " + message + "\n");
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    };

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return fail("ファイルを開けません: " + filePath);
    }
    std::stringstream ss;
    ss << file.rdbuf();

    JsonValue root;
    try {
        root = JsonParser::Parse(ss.str());
    } catch (const std::runtime_error& e) {
        return fail("JSON の書き方が間違っています: " + filePath + " (" + e.what() + ")");
    }
    if (!root.IsObject() || !root["objects"].IsArray()) {
        return fail("\"objects\" 配列がありません: " + filePath);
    }

    LevelData level;
    level.name = ReadString(root["name"]);
    level.filePath = filePath;
    for (const JsonValue& object : root["objects"].AsArray()) {
        level.objects.push_back(ParseObject(object));
    }
    outLevel = std::move(level);
    return true;
}

LevelData Load(const std::string& filePath) {
    LevelData level;
    std::string error;
    if (!TryLoad(filePath, level, &error)) {
        Logger::Fatal("LevelLoader: " + error);
    }
    return level;
}

} // namespace LevelLoader
