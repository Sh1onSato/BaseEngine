#pragma once
#include "JsonValue.h"
#include <string>

// 最小限の自前JSONパーサ（外部ライブラリを一切使わず、glTFのJSON構造を読むのに必要十分な範囲だけ実装）。
//
// 対応範囲:
//   - object, array, string, number(整数/小数/指数表記), true/false/null
//   - 文字列エスケープ: \" \\ \/ \n \t \r \b \f \uXXXX（サロゲートペアは非対応。BMP範囲のみ）
// 非対応範囲（JSON仕様通りに絞っている）:
//   - コメント（// や /* */ はJSON仕様上そもそも不正）
//   - \uXXXXで表現されたサロゲートペア（絵文字等）の合成
//   - 数値はdoubleとして読み取る（float化は呼び出し側の責務）
namespace JsonParser {
    // textをパースしてJsonValueを返す。
    // 構文エラーの場合はLogger::Log()でエラー内容を出力したうえでstd::runtime_errorを投げる
    // （呼び出し側は握りつぶさずtry-catchで検知すること）。
    JsonValue Parse(const std::string& text);
}
