#pragma once
#include "JsonValue.h"
#include <filesystem>
#include <map>
#include <string>

// キーと値の組をJSONファイルに保存・読み込みする汎用セーブデータ。
// 例:
//   SaveData save;
//   save.Load("save1.json");            // 無ければ空のまま（初回起動）
//   int stage = save.GetInt("stage", 1);
//   save.SetInt("stage", stage + 1);
//   save.Save();
//
// 相対パスは GetSaveDirectory() 基準で解決する（開発環境からの起動なら <Engine>\generated\save\、配布したexeなら %LOCALAPPDATA%\<exe名>\）。
// 作業ディレクトリ（VSから起動するとプロジェクトフォルダ）に書くと、ソースの横にファイルが増えてしまうため。
class SaveData {
public:
    // ファイルから読み込む。ファイルが無い・壊れている場合はfalseを返し、中身は空になる
    bool Load(const std::string& fileName);
    // 直前にLoad/Saveしたファイルへ保存する
    bool Save() const;
    // 指定ファイルへ保存する（書き込み途中で落ちても元ファイルが壊れないよう、一時ファイル経由で置き換える）
    bool Save(const std::string& fileName);

    bool Has(const std::string& key) const { return values.find(key) != values.end(); }
    void Remove(const std::string& key) { values.erase(key); }
    void Clear() { values.clear(); }

    // 値の取得（キーが無い・型が違う場合はdefaultValueを返す）
    float GetFloat(const std::string& key, float defaultValue = 0.0f) const;
    int GetInt(const std::string& key, int defaultValue = 0) const;
    bool GetBool(const std::string& key, bool defaultValue = false) const;
    std::string GetString(const std::string& key, const std::string& defaultValue = "") const;
    // 配列やネストしたオブジェクトを扱いたい場合用（キーが無ければnull）
    const JsonValue& GetValue(const std::string& key) const;

    // floatの最短表記(0.8f → "0.8")に相当するdoubleで保持する（そのままdouble化すると0.800000011920929になるため）
    void SetFloat(const std::string& key, float value);
    void SetInt(const std::string& key, int value) { values[key] = JsonValue(static_cast<double>(value)); }
    void SetBool(const std::string& key, bool value) { values[key] = JsonValue(value); }
    void SetString(const std::string& key, const std::string& value) { values[key] = JsonValue(value); }
    void SetValue(const std::string& key, const JsonValue& value) { values[key] = value; }

    // セーブファイルの置き場所。exeが <Engine>\generated\outputs\<構成>\ にあれば <Engine>\generated\save\、
    // それ以外（配布したexe）は %LOCALAPPDATA%\<exe名>\。取得できなければexeと同じフォルダ
    static std::filesystem::path GetSaveDirectory();
    static std::filesystem::path ResolvePath(const std::string& fileName);

private:
    std::map<std::string, JsonValue> values;
    std::filesystem::path filePath;
};
