#pragma once
#include <string>
#include <vector>
#include <utility>

// 自前JSONパーサ（JsonParser）が生成するJSON値。
// null / bool / number(double) / string / array / object の6種類を1つの型で表現する。
// glTFのJSON構造を読むのに必要十分な範囲に絞った最小実装であり、汎用JSONライブラリの代替ではない。
//
// object は挿入順を保持したいため vector<pair<string, JsonValue>> で表現する
// （glTFのオブジェクトはキー数が多くても数十程度のため、線形探索で十分な速度が出る）。
class JsonValue {
public:
    enum class Type {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object,
    };

    using Array = std::vector<JsonValue>;
    using Object = std::vector<std::pair<std::string, JsonValue>>;

    JsonValue() : type(Type::Null) {}
    explicit JsonValue(bool value) : type(Type::Bool), boolValue(value) {}
    explicit JsonValue(double value) : type(Type::Number), numberValue(value) {}
    explicit JsonValue(std::string value) : type(Type::String), stringValue(std::move(value)) {}
    explicit JsonValue(Array value) : type(Type::Array), arrayValue(std::move(value)) {}
    explicit JsonValue(Object value) : type(Type::Object), objectValue(std::move(value)) {}

    Type GetType() const { return type; }
    bool IsNull() const { return type == Type::Null; }
    bool IsBool() const { return type == Type::Bool; }
    bool IsNumber() const { return type == Type::Number; }
    bool IsString() const { return type == Type::String; }
    bool IsArray() const { return type == Type::Array; }
    bool IsObject() const { return type == Type::Object; }

    bool AsBool() const { return boolValue; }
    double AsNumber() const { return numberValue; }
    const std::string& AsString() const { return stringValue; }
    const Array& AsArray() const {
        static const Array kEmpty;
        return type == Type::Array ? arrayValue : kEmpty;
    }
    const Object& AsObject() const {
        static const Object kEmpty;
        return type == Type::Object ? objectValue : kEmpty;
    }

    // 配列・オブジェクトの要素数（それ以外の型は0を返す）
    size_t Size() const {
        if (type == Type::Array) return arrayValue.size();
        if (type == Type::Object) return objectValue.size();
        return 0;
    }

    // オブジェクトがkeyを持つか（オブジェクト以外はfalse）
    bool Contains(const std::string& key) const {
        if (type != Type::Object) return false;
        for (const auto& keyValue : objectValue) {
            if (keyValue.first == key) return true;
        }
        return false;
    }

    // オブジェクトのkeyに対応する値を返す。無ければ静的なnull値を返す（例外は投げない）。
    const JsonValue& operator[](const std::string& key) const {
        static const JsonValue kNull;
        if (type != Type::Object) return kNull;
        for (const auto& keyValue : objectValue) {
            if (keyValue.first == key) return keyValue.second;
        }
        return kNull;
    }

    // 配列のindex番目の値を返す。範囲外・配列以外は静的なnull値を返す。
    const JsonValue& operator[](size_t index) const {
        static const JsonValue kNull;
        if (type != Type::Array || index >= arrayValue.size()) return kNull;
        return arrayValue[index];
    }

private:
    Type type = Type::Null;
    bool boolValue = false;
    double numberValue = 0.0;
    std::string stringValue;
    Array arrayValue;
    Object objectValue;
};
