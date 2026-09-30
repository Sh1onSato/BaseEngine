#include "JsonParser.h"
#include "Logger.h"
#include <stdexcept>
#include <cctype>
#include <cstdlib>

namespace {

    // JSONテキストを先頭から1文字ずつ読み進める再帰下降パーサ本体。
    // JsonParser::Parse()から使われる実装詳細のため無名namespaceに閉じ込める。
    class Parser {
    public:
        explicit Parser(const std::string& text) : text(text) {}

        JsonValue Parse() {
            SkipWhitespace();
            JsonValue value = ParseValue();
            SkipWhitespace();
            if (pos < text.size()) {
                Fail("テキストの末尾に余分な文字があります");
            }
            return value;
        }

    private:
        const std::string& text;
        size_t pos = 0;

        [[noreturn]] void Fail(const std::string& message) {
            std::string detail = "JsonParser: " + message + " (position " + std::to_string(pos) + ")";
            Logger::Log(detail);
            throw std::runtime_error(detail);
        }

        char Peek() const {
            if (pos >= text.size()) return '\0';
            return text[pos];
        }

        char Get() {
            if (pos >= text.size()) Fail("予期しない終端です");
            return text[pos++];
        }

        void SkipWhitespace() {
            while (pos < text.size()) {
                char c = text[pos];
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                    ++pos;
                } else {
                    break;
                }
            }
        }

        void Expect(char expected) {
            char c = Get();
            if (c != expected) {
                Fail(std::string("'") + expected + "' が必要です");
            }
        }

        bool Consume(const std::string& literal) {
            if (text.compare(pos, literal.size(), literal) == 0) {
                pos += literal.size();
                return true;
            }
            return false;
        }

        JsonValue ParseValue() {
            SkipWhitespace();
            char c = Peek();
            if (c == '{') return ParseObject();
            if (c == '[') return ParseArray();
            if (c == '"') return JsonValue(ParseString());
            if (c == 't') {
                if (Consume("true")) return JsonValue(true);
                Fail("'true'の解析に失敗しました");
            }
            if (c == 'f') {
                if (Consume("false")) return JsonValue(false);
                Fail("'false'の解析に失敗しました");
            }
            if (c == 'n') {
                if (Consume("null")) return JsonValue();
                Fail("'null'の解析に失敗しました");
            }
            if (c == '-' || (c >= '0' && c <= '9')) {
                return ParseNumber();
            }
            Fail("不正な値です");
        }

        JsonValue ParseObject() {
            Expect('{');
            JsonValue::Object object;
            SkipWhitespace();
            if (Peek() == '}') {
                ++pos;
                return JsonValue(std::move(object));
            }
            while (true) {
                SkipWhitespace();
                if (Peek() != '"') Fail("オブジェクトのキーは文字列である必要があります");
                std::string key = ParseString();
                SkipWhitespace();
                Expect(':');
                JsonValue value = ParseValue();
                object.emplace_back(std::move(key), std::move(value));
                SkipWhitespace();
                char c = Get();
                if (c == ',') continue;
                if (c == '}') break;
                Fail("','または'}'が必要です");
            }
            return JsonValue(std::move(object));
        }

        JsonValue ParseArray() {
            Expect('[');
            JsonValue::Array array;
            SkipWhitespace();
            if (Peek() == ']') {
                ++pos;
                return JsonValue(std::move(array));
            }
            while (true) {
                JsonValue value = ParseValue();
                array.push_back(std::move(value));
                SkipWhitespace();
                char c = Get();
                if (c == ',') continue;
                if (c == ']') break;
                Fail("','または']'が必要です");
            }
            return JsonValue(std::move(array));
        }

        std::string ParseString() {
            Expect('"');
            std::string result;
            while (true) {
                if (pos >= text.size()) Fail("文字列が閉じられていません");
                char c = text[pos++];
                if (c == '"') break;
                if (c == '\\') {
                    if (pos >= text.size()) Fail("文字列が閉じられていません");
                    char esc = text[pos++];
                    switch (esc) {
                        case '"': result += '"'; break;
                        case '\\': result += '\\'; break;
                        case '/': result += '/'; break;
                        case 'n': result += '\n'; break;
                        case 't': result += '\t'; break;
                        case 'r': result += '\r'; break;
                        case 'b': result += '\b'; break;
                        case 'f': result += '\f'; break;
                        case 'u': result += ParseUnicodeEscape(); break;
                        default: Fail("不正なエスケープシーケンスです");
                    }
                } else {
                    result += c;
                }
            }
            return result;
        }

        // \uXXXX を読み取りUTF-8にエンコードして返す（サロゲートペアの合成は非対応、BMP範囲のみ想定）
        std::string ParseUnicodeEscape() {
            if (pos + 4 > text.size()) Fail("\\uのエスケープが不正です");
            unsigned int code = 0;
            for (int i = 0; i < 4; ++i) {
                char h = text[pos++];
                code <<= 4;
                if (h >= '0' && h <= '9') code |= static_cast<unsigned int>(h - '0');
                else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned int>(h - 'a' + 10);
                else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned int>(h - 'A' + 10);
                else Fail("\\uのエスケープが不正です");
            }
            std::string utf8;
            if (code < 0x80) {
                utf8 += static_cast<char>(code);
            } else if (code < 0x800) {
                utf8 += static_cast<char>(0xC0 | (code >> 6));
                utf8 += static_cast<char>(0x80 | (code & 0x3F));
            } else {
                utf8 += static_cast<char>(0xE0 | (code >> 12));
                utf8 += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                utf8 += static_cast<char>(0x80 | (code & 0x3F));
            }
            return utf8;
        }

        JsonValue ParseNumber() {
            size_t start = pos;
            if (Peek() == '-') ++pos;
            while (std::isdigit(static_cast<unsigned char>(Peek()))) ++pos;
            if (Peek() == '.') {
                ++pos;
                while (std::isdigit(static_cast<unsigned char>(Peek()))) ++pos;
            }
            if (Peek() == 'e' || Peek() == 'E') {
                ++pos;
                if (Peek() == '+' || Peek() == '-') ++pos;
                while (std::isdigit(static_cast<unsigned char>(Peek()))) ++pos;
            }
            std::string numberText = text.substr(start, pos - start);
            if (numberText.empty() || numberText == "-") Fail("数値の解析に失敗しました");
            double value = std::strtod(numberText.c_str(), nullptr);
            return JsonValue(value);
        }
    };

} // namespace

JsonValue JsonParser::Parse(const std::string& text) {
    Parser parser(text);
    return parser.Parse();
}
