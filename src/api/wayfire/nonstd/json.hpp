#pragma once
#include <optional>
#include <vector>
#include <cstdint>
#include <string_view>
#include <functional>

extern "C" {
    struct yyjson_mut_val;
    struct yyjson_mut_doc;
};

namespace wf
{
/**
 * A temporary non-owning reference to a JSON value.
 */
class json_t;
class json_reference_t
{
  public:
    // ------------------------------------------- Array support ---------------------------------------------
    bool is_array() const;
    json_reference_t operator [](const size_t& idx) const;
    void append(const json_reference_t& elem);
    void append(int value);
    void append(unsigned int value);
    void append(int64_t value);
    void append(uint64_t value);
    void append(double value);
    void append(bool value);
    void append(const std::string_view& value);
    void append(const char *value);
    size_t size() const;

    // ------------------------------------------- Object support --------------------------------------------
    bool has_member(const std::string_view& key) const;
    bool is_object() const;
    bool is_null() const;
    json_reference_t operator [](const char *key) const;
    json_reference_t operator [](const std::string_view& key) const;
    std::vector<std::string> get_member_names() const;
    json_reference_t& operator =(const json_reference_t& v);
    json_reference_t& operator =(const json_reference_t&& other);

    // ------------------------------------- Basic data types support ----------------------------------------
    json_reference_t& operator =(const int& v);
    json_reference_t& operator =(const unsigned int& v);
    json_reference_t& operator =(const int64_t& v);
    json_reference_t& operator =(const uint64_t& v);
    json_reference_t& operator =(const bool& v);
    json_reference_t& operator =(const double& v);
    json_reference_t& operator =(const std::string_view& v);
    json_reference_t& operator =(const char *v);

    bool is_int() const;
    operator int() const;
    int as_int() const;
    bool is_int64() const;
    operator int64_t() const;
    int64_t as_int64() const;
    bool is_uint() const;
    operator unsigned int() const;
    unsigned int as_uint() const;
    bool is_uint64() const;
    operator uint64_t() const;
    uint64_t as_uint64() const;
    bool is_bool() const;
    operator bool() const;
    bool as_bool() const;
    bool is_double() const;
    operator double() const;
    double as_double() const;
    bool is_string() const;
    operator std::string() const;
    std::string as_string() const;

    yyjson_mut_val *get_raw_value() const
    {
        return v;
    }

    yyjson_mut_doc *get_raw_doc() const
    {
        return doc;
    }

  private:
    friend class json_t;
    json_reference_t()
    {}
    json_reference_t(yyjson_mut_doc *doc, yyjson_mut_val *val) : doc(doc), v(val)
    {}

    yyjson_mut_doc *doc = NULL;
    yyjson_mut_val *v   = NULL;

    // Non-copyable, non-moveable.
    json_reference_t(const json_reference_t& other)  = delete;
    json_reference_t(const json_reference_t&& other) = delete;
};

class json_t final : public json_reference_t
{
  public:
    json_t();
    json_t(const json_reference_t& ref);
    json_t(const json_t& other);
    json_t(yyjson_mut_doc *doc);
    ~json_t();

    json_t& operator =(const json_t& other);
    json_t(json_t&& other);
    json_t& operator =(json_t&& other);

    json_t(int v);
    json_t(unsigned int v);
    json_t(int64_t v);
    json_t(uint64_t v);
    json_t(double v);
    json_t(const std::string_view& v);
    json_t(const char *v);
    json_t(bool v);

    static json_t array();
    static json_t null();

    /**
     * Parse the given source as a JSON document.
     *
     * @result Where to store the parsed document on success.
     * @return On failure, an error message describing the problem with the JSON source will be returned.
     *   Otherwise, the function will return std::nullopt.
     */
    static std::optional<std::string> parse_string(const std::string_view& source,
        json_t& result);

    /**
     * Serialize the JSON document and handle it using the provided callback.
     *
     * Note that the source string will be freed afterwards, so if the caller needs the data for longer,
     * they need to make a copy of it.
     */
    void map_serialized(std::function<void(const char*source, size_t length)> callback) const;

    /**
     * Get a JSON string representation of the document.
     */
    std::string serialize() const;

  private:
    void init();
};
}
