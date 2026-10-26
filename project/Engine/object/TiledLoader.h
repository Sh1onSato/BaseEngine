#pragma once
#include "TiledData.h"
#include <string>

/// <summary>
/// Tiled のマップ（.tmj。Tiled で「名前を付けて保存」→ 形式に JSON を選ぶ）を読むクラス。
/// 対応: 平らな（orthogonal）マップ、タイルレイヤー（CSV か 圧縮なしの base64）、オブジェクトレイヤー、グループレイヤー、
///       マップの中のタイルセットと外部のタイルセット（.tsj）、独自の項目（Custom Properties）、タイルの反転
/// 非対応（読み込みが失敗して理由が分かる）: 無限マップ、圧縮したタイルデータ、.tsx（XML のタイルセット）、斜めの・六角形のマップ
/// </summary>
class TiledLoader {
public:
    TiledLoader() = delete;

    // filePath を読む（失敗したら理由を出して止まる）
    static TiledMap Load(const std::string& filePath);
    // filePath を読む（失敗したら false。errorMessage に理由）。読み直しで書きかけのファイルを読んでも止まらないように
    static bool TryLoad(const std::string& filePath, TiledMap& outMap, std::string* errorMessage = nullptr);
};
