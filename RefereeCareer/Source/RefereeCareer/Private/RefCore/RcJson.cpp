#include "RefCore/RcJson.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace refcore {

namespace {

const Json& sharedNullJson() {
    static const Json kNull;
    return kNull;
}

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : s_(text) {}

    bool run(Json& out, std::string* error) {
        skipBom();
        skipWs();
        if (!parseValue(out, 0)) return fail(error);
        skipWs();
        if (pos_ != s_.size()) {
            err_ = "trailing characters";
            return fail(error);
        }
        return true;
    }

private:
    bool fail(std::string* error) {
        if (error) *error = err_ + " at byte " + std::to_string(pos_);
        return false;
    }
    void skipBom() {
        if (s_.size() >= 3 && static_cast<unsigned char>(s_[0]) == 0xEF && static_cast<unsigned char>(s_[1]) == 0xBB &&
            static_cast<unsigned char>(s_[2]) == 0xBF)
            pos_ = 3;
    }
    void skipWs() {
        while (pos_ < s_.size()) {
            const char c = s_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos_;
            else break;
        }
    }
    bool literal(const char* word) {
        size_t i = 0;
        while (word[i]) {
            if (pos_ + i >= s_.size() || s_[pos_ + i] != word[i]) return false;
            ++i;
        }
        pos_ += i;
        return true;
    }
    bool parseValue(Json& out, int depth) {
        if (depth > 64) { err_ = "nesting too deep"; return false; }
        if (pos_ >= s_.size()) { err_ = "unexpected end"; return false; }
        const char c = s_[pos_];
        if (c == '{') return parseObject(out, depth);
        if (c == '[') return parseArray(out, depth);
        if (c == '"') {
            std::string str;
            if (!parseString(str)) return false;
            out = Json(std::move(str));
            return true;
        }
        if (c == 't') { if (literal("true")) { out = Json(true); return true; } }
        else if (c == 'f') { if (literal("false")) { out = Json(false); return true; } }
        else if (c == 'n') { if (literal("null")) { out = Json(); return true; } }
        else if (c == '-' || (c >= '0' && c <= '9')) return parseNumber(out);
        err_ = "unexpected character";
        return false;
    }
    bool parseNumber(Json& out) {
        const size_t start = pos_;
        if (s_[pos_] == '-') ++pos_;
        while (pos_ < s_.size()) {
            const char c = s_[pos_];
            if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') ++pos_;
            else break;
        }
        const std::string num = s_.substr(start, pos_ - start);
        char* end = nullptr;
        const double v = std::strtod(num.c_str(), &end);
        if (!end || *end != '\0') { err_ = "bad number"; return false; }
        out = Json(v);
        return true;
    }
    static void appendUtf8(std::string& out, uint32_t cp) {
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    bool hex4(uint32_t& v) {
        if (pos_ + 4 > s_.size()) { err_ = "bad escape"; return false; }
        v = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = s_[pos_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
            else { err_ = "bad hex"; return false; }
        }
        return true;
    }
    bool parseString(std::string& out) {
        ++pos_;  // opening quote
        while (pos_ < s_.size()) {
            const char c = s_[pos_++];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (pos_ >= s_.size()) break;
            const char e = s_[pos_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t cp = 0;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF && pos_ + 6 <= s_.size() && s_[pos_] == '\\' && s_[pos_ + 1] == 'u') {
                        pos_ += 2;
                        uint32_t lo = 0;
                        if (!hex4(lo)) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: err_ = "bad escape"; return false;
            }
        }
        err_ = "unterminated string";
        return false;
    }
    bool parseArray(Json& out, int depth) {
        ++pos_;
        out = Json::makeArray();
        skipWs();
        if (pos_ < s_.size() && s_[pos_] == ']') { ++pos_; return true; }
        while (true) {
            Json item;
            skipWs();
            if (!parseValue(item, depth + 1)) return false;
            out.push(std::move(item));
            skipWs();
            if (pos_ < s_.size() && s_[pos_] == ',') { ++pos_; continue; }
            if (pos_ < s_.size() && s_[pos_] == ']') { ++pos_; return true; }
            err_ = "expected , or ]";
            return false;
        }
    }
    bool parseObject(Json& out, int depth) {
        ++pos_;
        out = Json::makeObject();
        skipWs();
        if (pos_ < s_.size() && s_[pos_] == '}') { ++pos_; return true; }
        while (true) {
            skipWs();
            if (pos_ >= s_.size() || s_[pos_] != '"') { err_ = "expected key"; return false; }
            std::string key;
            if (!parseString(key)) return false;
            skipWs();
            if (pos_ >= s_.size() || s_[pos_] != ':') { err_ = "expected :"; return false; }
            ++pos_;
            skipWs();
            Json value;
            if (!parseValue(value, depth + 1)) return false;
            out.set(key, std::move(value));
            skipWs();
            if (pos_ < s_.size() && s_[pos_] == ',') { ++pos_; continue; }
            if (pos_ < s_.size() && s_[pos_] == '}') { ++pos_; return true; }
            err_ = "expected , or }";
            return false;
        }
    }

    const std::string& s_;
    size_t pos_ = 0;
    std::string err_ = "parse error";
};

void appendJsonString(std::string& out, const std::string& s) {
    out += '"';
    for (const char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += ch;
                }
        }
    }
    out += '"';
}

void appendIndent(std::string& out, int indent, int depth) {
    if (indent < 0) return;
    out += '\n';
    out.append(static_cast<size_t>(indent * depth), ' ');
}

}  // namespace

size_t Json::size() const {
    if (type_ == Type::Array) return arr_.size();
    if (type_ == Type::Object) return obj_.size();
    return 0;
}

const Json& Json::at(size_t i) const {
    if (type_ != Type::Array || i >= arr_.size()) return sharedNullJson();
    return arr_[i];
}

Json& Json::push(Json v) {
    if (type_ != Type::Array) {
        *this = makeArray();
    }
    arr_.push_back(std::move(v));
    return arr_.back();
}

bool Json::has(const std::string& key) const {
    if (type_ != Type::Object) return false;
    for (const auto& kv : obj_)
        if (kv.first == key) return true;
    return false;
}

const Json& Json::operator[](const std::string& key) const {
    if (type_ == Type::Object) {
        for (const auto& kv : obj_)
            if (kv.first == key) return kv.second;
    }
    return sharedNullJson();
}

Json& Json::set(const std::string& key, Json v) {
    if (type_ != Type::Object) {
        *this = makeObject();
    }
    for (auto& kv : obj_) {
        if (kv.first == key) {
            kv.second = std::move(v);
            return kv.second;
        }
    }
    obj_.emplace_back(key, std::move(v));
    return obj_.back().second;
}

void Json::dumpTo(std::string& out, int indent, int depth) const {
    switch (type_) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += bool_ ? "true" : "false"; break;
        case Type::Number: {
            char buf[40];
            if (std::isfinite(num_) && std::floor(num_) == num_ && std::fabs(num_) < 1e15)
                std::snprintf(buf, sizeof(buf), "%.0f", num_);
            else if (std::isfinite(num_))
                std::snprintf(buf, sizeof(buf), "%.10g", num_);
            else
                std::snprintf(buf, sizeof(buf), "0");
            out += buf;
            break;
        }
        case Type::String: appendJsonString(out, str_); break;
        case Type::Array: {
            out += '[';
            for (size_t i = 0; i < arr_.size(); ++i) {
                if (i) out += ',';
                appendIndent(out, indent, depth + 1);
                arr_[i].dumpTo(out, indent, depth + 1);
            }
            if (!arr_.empty()) appendIndent(out, indent, depth);
            out += ']';
            break;
        }
        case Type::Object: {
            out += '{';
            for (size_t i = 0; i < obj_.size(); ++i) {
                if (i) out += ',';
                appendIndent(out, indent, depth + 1);
                appendJsonString(out, obj_[i].first);
                out += indent >= 0 ? ": " : ":";
                obj_[i].second.dumpTo(out, indent, depth + 1);
            }
            if (!obj_.empty()) appendIndent(out, indent, depth);
            out += '}';
            break;
        }
    }
}

std::string Json::dump(int indent) const {
    std::string out;
    dumpTo(out, indent, 0);
    return out;
}

bool Json::parse(const std::string& text, Json& out, std::string* error) {
    JsonParser p(text);
    return p.run(out, error);
}

}  // namespace refcore
