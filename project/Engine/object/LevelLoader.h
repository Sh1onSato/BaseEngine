#pragma once
#include "LevelData.h"
#include <string>

/// <summary>
/// Blender のレベルエディタアドオンが書き出した JSON を読み込んで LevelData にする。
///
/// 対応する形式:
///   { "name": "scene", "objects": [
///       { "type": "MESH", "name": "Cube", "file_name": "cube",
///         "transform": { "translation": [x,y,z], "rotation": [x,y,z](度), "scaling": [x,y,z] },
///         "collider": { "type": "BOX", "center": [x,y,z], "size": [x,y,z] },   // "SPHERE" なら "radius"
///         "disabled": false,
///         "children": [ ... ] } ] }
///   上に無い項目も LevelObjectData::raw から読める。
///
/// 座標変換: Blender（Z が上・右手系）→ エンジン（Y が上・左手系）。y と z を入れ替え、回転は符号を反転して度→ラジアン。
///
/// 使い方:
///   LevelData level = LevelLoader::Load("Resources/levels/stage1.json"); // 失敗したら止まる
///   LevelData level; if (!LevelLoader::TryLoad(path, level, &error)) { ... } // 失敗しても続けたいとき（読み直しなど）
/// </summary>
namespace LevelLoader {
    // 読み込みに失敗したら Logger::Fatal で止まる
    LevelData Load(const std::string& filePath);
    // 読み込みに失敗したら false（outLevel は変更しない）。errorMessage があれば理由を入れる
    bool TryLoad(const std::string& filePath, LevelData& outLevel, std::string* errorMessage = nullptr);
}
