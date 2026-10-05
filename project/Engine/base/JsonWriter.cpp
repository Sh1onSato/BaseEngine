#include "JsonWriter.h"
#include <charconv>
#include <cmath>
#include <cstdio>

namespace {

    void WriteString(std::string& out, const std::string& text) {
        out += '"';
        for (unsigned char c : text) {
            switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
                break;
            }
        }
        out += '"';
    }

    void WriteNumber(std::string& out, double value) {
        // JSONはNaN/Infを表現できないので0として書く
        if (!std::isfinite(value)) {
            out += '0';
            return;
        }
        // 整数値は小数点なしで書く（セーブデータのスコア等が "100.0" にならないように）
        if (value == std::floor(value) && std::fabs(value) < 1e15) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(value));
            out += buf;
            return;
        }
        // 読み戻して同じ値になる最短の表記で書く
        char buf[32];
        auto result = std::to_chars(buf, buf + sizeof(buf), value);
        out.append(buf, result.ptr);
    }

    void Indent(std::string& out, bool pretty, int depth) {
        if (!pretty) return;
        out += '\n';
        out.append(static_cast<size_t>(depth) * 2, ' ');
    }

    void WriteValue(std::string& out, const JsonValue& value, bool pretty, int depth) {
        switch (value.GetType()) {
        case JsonValue::Type::Null:
            out += "null";
            break;
        case JsonValue::Type::Bool:
            out += value.AsBool() ? "true" : "false";
            break;
        case JsonValue::Type::Number:
            WriteNumber(out, value.AsNumber());
            break;
        case JsonValue::Type::String:
            WriteString(out, value.AsString());
            break;
        case JsonValue::Type::Array: {
            const JsonValue::Array& array = value.AsArray();
            out += '[';
            for (size_t i = 0; i < array.size(); ++i) {
                if (i > 0) out += ',';
                Indent(out, pretty, depth + 1);
                WriteValue(out, array[i], pretty, depth + 1);
            }
            if (!array.empty()) Indent(out, pretty, depth);
            out += ']';
            break;
        }
        case JsonValue::Type::Object: {
            const JsonValue::Object& object = value.AsObject();
            out += '{';
            for (size_t i = 0; i < object.size(); ++i) {
                if (i > 0) out += ',';
                Indent(out, pretty, depth + 1);
                WriteString(out, object[i].first);
                out += pretty ? ": " : ":";
                WriteValue(out, object[i].second, pretty, depth + 1);
            }
            if (!object.empty()) Indent(out, pretty, depth);
            out += '}';
            break;
        }
        }
    }
}

std::string JsonWriter::Stringify(const JsonValue& value, bool pretty) {
    std::string out;
    WriteValue(out, value, pretty, 0);
    if (pretty) out += '\n';
    return out;
}
