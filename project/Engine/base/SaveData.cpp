#include "SaveData.h"
#include "JsonParser.h"
#include "JsonWriter.h"
#include "Logger.h"
#include "StringUtility.h"
#include <Windows.h>
#include <ShlObj.h>
#include <charconv>
#include <format>
#include <fstream>
#include <sstream>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace {
    std::filesystem::path GetExecutablePath() {
        wchar_t buffer[MAX_PATH]{};
        DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
        return std::filesystem::path(std::wstring(buffer, length));
    }
}

std::filesystem::path SaveData::GetSaveDirectory() {
    std::filesystem::path exePath = GetExecutablePath();

    // 開発環境から起動した場合（exeが <Engine>\generated\outputs\<構成>\ にある）は <Engine>\generated\save\ に保存する。
    // 開発中に見つけやすく、generated は Git 管理外なので誤ってコミットされない。
    // 配布したexe（フォルダ構成が違う）は書き込み禁止の場所に置かれても困らないよう %LOCALAPPDATA%\<exe名>\ に保存する
    std::filesystem::path outputsDirectory = exePath.parent_path().parent_path();
    std::filesystem::path generatedDirectory = outputsDirectory.parent_path();
    if (_wcsicmp(outputsDirectory.filename().c_str(), L"outputs") == 0 &&
        _wcsicmp(generatedDirectory.filename().c_str(), L"generated") == 0) {
        return generatedDirectory / L"save";
    }

    PWSTR localAppData = nullptr;
    std::filesystem::path directory;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData))) {
        directory = std::filesystem::path(localAppData) / exePath.stem();
    } else {
        directory = exePath.parent_path();
    }
    CoTaskMemFree(localAppData);
    return directory;
}

std::filesystem::path SaveData::ResolvePath(const std::string& fileName) {
    std::filesystem::path path(StringUtility::ConvertString(fileName));
    if (path.is_absolute()) {
        return path;
    }
    return GetSaveDirectory() / path;
}

bool SaveData::Load(const std::string& fileName) {
    values.clear();
    filePath = ResolvePath(fileName);

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return false; // 初回起動などでファイルが無いのは正常
    }
    std::stringstream buffer;
    buffer << file.rdbuf();

    JsonValue root;
    try {
        root = JsonParser::Parse(buffer.str());
    } catch (const std::exception&) {
        Logger::Log(std::format("SaveData: 壊れたセーブデータを読み込めませんでした \"{}\"\n", StringUtility::ConvertString(filePath.wstring())));
        return false;
    }

    for (const auto& [key, value] : root.AsObject()) {
        values[key] = value;
    }
    return true;
}

bool SaveData::Save() const {
    if (filePath.empty()) {
        Logger::Log("SaveData: 保存先が未指定です（先にLoadかSave(fileName)を呼んでください）\n");
        return false;
    }

    JsonValue::Object object;
    object.reserve(values.size());
    for (const auto& [key, value] : values) {
        object.emplace_back(key, value);
    }
    std::string text = JsonWriter::Stringify(JsonValue(std::move(object)));

    std::error_code ec;
    std::filesystem::create_directories(filePath.parent_path(), ec);

    std::filesystem::path tempPath = filePath;
    tempPath += L".tmp";
    {
        std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            Logger::Log(std::format("SaveData: 保存に失敗しました \"{}\"\n", StringUtility::ConvertString(filePath.wstring())));
            return false;
        }
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!file.good()) {
            return false;
        }
    }

    if (!MoveFileExW(tempPath.c_str(), filePath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        Logger::Log(std::format("SaveData: 保存ファイルの置き換えに失敗しました \"{}\"\n", StringUtility::ConvertString(filePath.wstring())));
        return false;
    }
    return true;
}

bool SaveData::Save(const std::string& fileName) {
    filePath = ResolvePath(fileName);
    return Save();
}

void SaveData::SetFloat(const std::string& key, float value) {
    char buf[32];
    auto result = std::to_chars(buf, buf + sizeof(buf), value);
    double shortest = static_cast<double>(value);
    std::from_chars(buf, result.ptr, shortest);
    values[key] = JsonValue(shortest);
}

float SaveData::GetFloat(const std::string& key, float defaultValue) const {
    const JsonValue& value = GetValue(key);
    return value.IsNumber() ? static_cast<float>(value.AsNumber()) : defaultValue;
}

int SaveData::GetInt(const std::string& key, int defaultValue) const {
    const JsonValue& value = GetValue(key);
    return value.IsNumber() ? static_cast<int>(value.AsNumber()) : defaultValue;
}

bool SaveData::GetBool(const std::string& key, bool defaultValue) const {
    const JsonValue& value = GetValue(key);
    return value.IsBool() ? value.AsBool() : defaultValue;
}

std::string SaveData::GetString(const std::string& key, const std::string& defaultValue) const {
    const JsonValue& value = GetValue(key);
    return value.IsString() ? value.AsString() : defaultValue;
}

const JsonValue& SaveData::GetValue(const std::string& key) const {
    static const JsonValue kNull;
    auto it = values.find(key);
    return it != values.end() ? it->second : kNull;
}
