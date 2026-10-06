#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace refcore {

// Small JSON value used for game data and save files. UTF-8 strings pass through unchanged,
// object member order is preserved so saved files diff cleanly.
class Json {
public:
    enum class Type : uint8_t { Null, Bool, Number, String, Array, Object };

    Json() = default;
    Json(bool b) : type_(Type::Bool), bool_(b) {}
    Json(int n) : type_(Type::Number), num_(n) {}
    Json(double n) : type_(Type::Number), num_(n) {}
    Json(const char* s) : type_(Type::String), str_(s ? s : "") {}
    Json(std::string s) : type_(Type::String), str_(std::move(s)) {}

    static Json makeArray() { Json j; j.type_ = Type::Array; return j; }
    static Json makeObject() { Json j; j.type_ = Type::Object; return j; }

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool asBool(bool def = false) const { return type_ == Type::Bool ? bool_ : def; }
    double asNumber(double def = 0.0) const { return type_ == Type::Number ? num_ : def; }
    int asInt(int def = 0) const { return type_ == Type::Number ? static_cast<int>(num_ >= 0 ? num_ + 0.5 : num_ - 0.5) : def; }
    std::string asString(const std::string& def = std::string()) const { return type_ == Type::String ? str_ : def; }

    // Arrays
    size_t size() const;
    const Json& at(size_t i) const;
    const std::vector<Json>& items() const { return arr_; }
    Json& push(Json v);

    // Objects (lookups return a shared null value when the key is missing)
    bool has(const std::string& key) const;
    const Json& operator[](const std::string& key) const;
    Json& set(const std::string& key, Json v);
    const std::vector<std::pair<std::string, Json>>& members() const { return obj_; }

    std::string dump(int indent = -1) const;
    static bool parse(const std::string& text, Json& out, std::string* error = nullptr);

private:
    void dumpTo(std::string& out, int indent, int depth) const;

    Type type_ = Type::Null;
    bool bool_ = false;
    double num_ = 0.0;
    std::string str_;
    std::vector<Json> arr_;
    std::vector<std::pair<std::string, Json>> obj_;
};

}  // namespace refcore
