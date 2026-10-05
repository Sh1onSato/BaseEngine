#pragma once
#include "JsonValue.h"
#include <string>

// JsonValueをJSONテキストに書き出す（JsonParserの逆方向。セーブデータ・設定ファイルの保存用）。
// 文字列はUTF-8のまま出力し、制御文字と " \ のみエスケープする。
namespace JsonWriter {
    // pretty=trueならインデント(2スペース)と改行を入れて人が読める形にする
    std::string Stringify(const JsonValue& value, bool pretty = true);
}
