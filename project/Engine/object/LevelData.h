#pragma once
#include "JsonValue.h"
#include "Structs.h"
#include <string>
#include <vector>

// レベルデータのコライダーの形（Blender アドオンの "collider" → "type"）
enum class LevelColliderType {
    kNone,   // コライダー無し
    kBox,    // "BOX"（回転しない箱）
    kSphere, // "SPHERE"
};

// レベルデータのコライダー（エンジンの座標系に変換済み）
struct LevelColliderData {
    LevelColliderType type = LevelColliderType::kNone;
    Vector3 center = { 0.0f, 0.0f, 0.0f };   // 持ち主の位置からのずれ
    Vector3 size = { 1.0f, 1.0f, 1.0f };     // 箱の幅・高さ・奥行き（半分ではなく全体の長さ）
    float radius = 0.5f;                     // 球の半径
};

/// <summary>
/// レベルデータに置かれた1つの物。
/// transform はエンジンの座標系（Y が上、回転はラジアン）に変換済み。
/// 子の transform は親から見た相対値（ワールド座標にするのは生成側の仕事）。
/// </summary>
struct LevelObjectData {
    std::string type;     // "MESH" / "CAMERA" / ゲーム独自の種類名など（Blender の "type"）
    std::string name;     // Blender 上のオブジェクト名
    std::string fileName; // モデルのファイル名（"file_name"。無ければ空）
    Transform transform{ { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };
    LevelColliderData collider;
    bool disabled = false; // "disabled": true なら生成しない
    // 元の JSON（ゲーム独自の項目を読む用。例: data.raw["hp"].AsNumber()）
    JsonValue raw;
    std::vector<LevelObjectData> children;
};

// レベルデータ全体（LevelLoader::Load で作る）
struct LevelData {
    std::string name;     // "name"（Blender のシーン名）
    std::string filePath; // 読み込んだファイル（読み直し用）
    std::vector<LevelObjectData> objects;
};
