#include <wayfire/nonstd/json.hpp>
#include "wayfire/dassert.hpp"
#include <yyjson.h>
#include <limits>

namespace wf
{
bool json_reference_t::is_array() const
{
    return yyjson_mut_is_arr(v);
}

json_reference_t json_reference_t::operator [](const size_t& idx) const
{
    wf::dassert(is_array());
    wf::dassert(size() > idx);
    return json_reference_t{doc, yyjson_mut_arr_get(v, idx)};
}

void json_reference_t::append(const json_reference_t& elem)
{
    wf::dassert(is_array());
    yyjson_mut_arr_append(v, yyjson_mut_val_mut_copy(doc, elem.v));
}

void json_reference_t::append(int value)
{
    wf::dassert(is_array());
    yyjson_mut_arr_add_int(doc, v, value);
}

void json_reference_t::append(unsigned int value)
{
    wf::dassert(is_array());
    yyjson_mut_arr_add_uint(doc, v, value);
}

void json_reference_t::append(int64_t value)
{
    wf::dassert(is_array());
    yyjson_mut_arr_add_sint(doc, v, value);
}

void json_reference_t::append(uint64_t value)
{
    wf::dassert(is_array());
    yyjson_mut_arr_add_uint(doc, v, value);
}

void json_reference_t::append(double value)
{
    wf::dassert(is_array());
    yyjson_mut_arr_add_real(doc, v, value);
}

void json_reference_t::append(bool value)
{
    wf::dassert(is_array());
    yyjson_mut_arr_add_bool(doc, v, value);
}

void json_reference_t::append(const std::string_view& value)
{
    wf::dassert(is_array());
    yyjson_mut_arr_add_strncpy(doc, v, value.data(), value.size());
}

void json_reference_t::append(const char *value)
{
    wf::dassert(is_array());
    yyjson_mut_arr_add_strcpy(doc, v, value);
}

size_t json_reference_t::size() const
{
    wf::dassert(is_array());
    return yyjson_mut_arr_size(v);
}

bool json_reference_t::has_member(const std::string_view& key) const
{
    return is_object() && (yyjson_mut_obj_getn(v, key.data(), key.size()) != NULL);
}

bool json_reference_t::is_object() const
{
    return yyjson_mut_is_obj(v);
}

bool json_reference_t::is_null() const
{
    return yyjson_mut_is_null(v);
}

json_reference_t json_reference_t::operator [](const char *key) const
{
    return this->operator [](std::string_view{key});
}

json_reference_t json_reference_t::operator [](const std::string_view& key) const
{
    wf::dassert(is_object() || is_null(), "Trying to access into JSON value that is not an object!");
    if (is_null())
    {
        yyjson_mut_set_obj(v);
    }

    auto ptr = yyjson_mut_obj_getn(v, key.data(), key.size());
    if (ptr != NULL)
    {
        return json_reference_t{doc, ptr};
    }

    auto key_yy = yyjson_mut_strncpy(doc, key.data(), key.size());
    auto value  = yyjson_mut_null(doc);

    yyjson_mut_obj_add(v, key_yy, value);
    return json_reference_t{doc, value};
}

std::vector<std::string> json_reference_t::get_member_names() const
{
    std::vector<std::string> members;
    yyjson_mut_obj_iter iter = yyjson_mut_obj_iter_with(v);
    while (yyjson_mut_obj_iter_has_next(&iter))
    {
        auto key = yyjson_mut_obj_iter_next(&iter);
        members.push_back(yyjson_mut_get_str(key));
    }

    return members;
}

static void copy_yyjson(yyjson_mut_doc *dst_doc, yyjson_mut_val *dst, yyjson_mut_val *src)
{
    if (yyjson_mut_is_null(src))
    {
        yyjson_mut_set_null(dst);
        return;
    }

    if (yyjson_mut_is_bool(src))
    {
        yyjson_mut_set_bool(dst, yyjson_mut_get_bool(src));
        return;
    }

    if (yyjson_mut_is_sint(src))
    {
        yyjson_mut_set_sint(dst, yyjson_mut_get_sint(src));
        return;
    }

    if (yyjson_mut_is_uint(src))
    {
        yyjson_mut_set_uint(dst, yyjson_mut_get_uint(src));
        return;
    }

    if (yyjson_mut_is_real(src))
    {
        yyjson_mut_set_real(dst, yyjson_mut_get_real(src));
        return;
    }

    if (yyjson_mut_is_str(src))
    {
        // copy ownership of string to dst_doc
        auto val = yyjson_mut_val_mut_copy(dst_doc, src);
        yyjson_mut_set_str(dst, yyjson_mut_get_str(val));
        return;
    }

    if (yyjson_mut_is_arr(src))
    {
        yyjson_mut_set_arr(dst);
        yyjson_mut_arr_iter iter = yyjson_mut_arr_iter_with(src);
        while (yyjson_mut_arr_iter_has_next(&iter))
        {
            auto elem = yyjson_mut_arr_iter_next(&iter);
            elem = yyjson_mut_val_mut_copy(dst_doc, elem);
            yyjson_mut_arr_append(dst, elem);
        }

        return;
    }

    if (yyjson_mut_is_obj(src))
    {
        yyjson_mut_set_obj(dst);
        yyjson_mut_obj_iter iter = yyjson_mut_obj_iter_with(src);
        while (yyjson_mut_obj_iter_has_next(&iter))
        {
            auto key   = yyjson_mut_obj_iter_next(&iter);
            auto value = yyjson_mut_obj_iter_get_val(key);
            // Copy ownership to our doc
            key   = yyjson_mut_val_mut_copy(dst_doc, key);
            value = yyjson_mut_val_mut_copy(dst_doc, value);
            yyjson_mut_obj_add(dst, key, value);
        }

        return;
    }

    wf::dassert(false, "Unsupported JSON type?");
}

json_reference_t& json_reference_t::operator =(const json_reference_t& other)
{
    copy_yyjson(doc, v, other.v);
    return *this;
}

json_reference_t& json_reference_t::operator =(const json_reference_t&& other)
{
    // FIXME: maybe we can check whether the docs are the same or something to make this faster?
    copy_yyjson(doc, v, other.v);
    return *this;
}

// --------------------------------------- Basic data types support ------------------------------------------
json_reference_t& json_reference_t::operator =(const int& v)
{
    yyjson_mut_set_sint(this->v, v);
    return *this;
}

json_reference_t& json_reference_t::operator =(const uint& v)
{
    yyjson_mut_set_uint(this->v, v);
    return *this;
}

json_reference_t& json_reference_t::operator =(const int64_t& v)
{
    yyjson_mut_set_sint(this->v, v);
    return *this;
}

json_reference_t& json_reference_t::operator =(const uint64_t& v)
{
    yyjson_mut_set_uint(this->v, v);
    return *this;
}

json_reference_t& json_reference_t::operator =(const bool& v)
{
    yyjson_mut_set_bool(this->v, v);
    return *this;
}

json_reference_t& json_reference_t::operator =(const double& v)
{
    yyjson_mut_set_real(this->v, v);
    return *this;
}

json_reference_t& json_reference_t::operator =(const std::string_view& v)
{
    // copy ownership of string to doc
    auto our_v = yyjson_mut_strncpy(doc, v.data(), v.size());
    yyjson_mut_set_str(this->v, yyjson_mut_get_str(our_v));
    return *this;
}

json_reference_t& json_reference_t::operator =(const char *v)
{
    // copy ownership of string to doc
    auto our_v = yyjson_mut_strcpy(doc, v);
    yyjson_mut_set_str(this->v, yyjson_mut_get_str(our_v));
    return *this;
}

bool json_reference_t::is_int() const
{
    if (yyjson_mut_is_sint(v))
    {
        return (std::numeric_limits<int>::min() <= yyjson_mut_get_sint(v)) &&
               (yyjson_mut_get_sint(v) <= std::numeric_limits<int>::max());
    }

    if (yyjson_mut_is_uint(v))
    {
        return (yyjson_mut_get_uint(v) <= std::numeric_limits<int>::max());
    }

    return false;
}

json_reference_t::operator int() const
{
    wf::dassert(is_int());
    return yyjson_mut_get_int(v);
}

int json_reference_t::as_int() const
{
    return (int)(*this);
}

bool json_reference_t::is_int64() const
{
    if (yyjson_mut_is_uint(v))
    {
        return yyjson_mut_get_uint(v) <= std::numeric_limits<int64_t>::max();
    }

    return yyjson_mut_is_sint(v);
}

json_reference_t::operator int64_t() const
{
    wf::dassert(is_int64());
    if (yyjson_mut_is_uint(v))
    {
        return yyjson_mut_get_uint(v);
    } else
    {
        return yyjson_mut_get_sint(v);
    }
}

int64_t json_reference_t::as_int64() const
{
    return (int64_t)(*this);
}

bool json_reference_t::is_uint() const
{
    return yyjson_mut_is_uint(v) &&
           (yyjson_mut_get_uint(v) <= std::numeric_limits<unsigned int>::max());
}

json_reference_t::operator uint() const
{
    wf::dassert(is_uint());
    return yyjson_mut_get_uint(v);
}

unsigned int json_reference_t::as_uint() const
{
    return (unsigned int)(*this);
}

bool json_reference_t::is_uint64() const
{
    return yyjson_mut_is_uint(v);
}

json_reference_t::operator uint64_t() const
{
    wf::dassert(is_uint64());
    return yyjson_mut_get_uint(v);
}

uint64_t json_reference_t::as_uint64() const
{
    return (uint64_t)(*this);
}

bool json_reference_t::is_bool() const
{
    return yyjson_mut_is_bool(v);
}

json_reference_t::operator bool() const
{
    wf::dassert(is_bool());
    return yyjson_mut_get_bool(v);
}

bool json_reference_t::as_bool() const
{
    return (bool)(*this);
}

bool json_reference_t::is_double() const
{
    return yyjson_mut_is_num(v);
}

json_reference_t::operator double() const
{
    wf::dassert(is_double());
    return yyjson_mut_get_num(v);
}

double json_reference_t::as_double() const
{
    return (double)(*this);
}

bool json_reference_t::is_string() const
{
    return yyjson_mut_is_str(v);
}

json_reference_t::operator std::string() const {
    wf::dassert(is_string());
    return std::string(yyjson_mut_get_str(v));
}

std::string json_reference_t::as_string() const
{
    return (std::string)*this;
}

void json_t::init()
{
    this->doc = yyjson_mut_doc_new(NULL);
    this->v   = yyjson_mut_null(this->doc);
    yyjson_mut_doc_set_root(doc, v);
}

json_t::json_t()
{
    init();
}

json_t::~json_t()
{
    yyjson_mut_doc_free(doc);
}

json_t::json_t(const json_reference_t& ref) : json_t()
{
    copy_yyjson(doc, this->v, ref.get_raw_value());
}

json_t::json_t(yyjson_mut_doc *doc)
{
    this->doc = doc;
    this->v   = yyjson_mut_doc_get_root(this->doc);
}

json_t::json_t(const json_t& other)
{
    *this = other;
}

json_t& json_t::operator =(const json_t& other)
{
    if (this != &other)
    {
        yyjson_mut_doc_free(doc);
        init();
        copy_yyjson(doc, this->v, other.v);
    }

    return *this;
}

json_t::json_t(json_t&& other)
{
    *this = std::move(other);
}

json_t& json_t::operator =(json_t&& other)
{
    if (this != &other)
    {
        yyjson_mut_doc_free(doc);
        this->doc = other.doc;
        this->v   = other.v;

        other.doc = NULL;
        other.v   = NULL;
    }

    return *this;
}

json_t::json_t(int v) : json_t()
{
    *this = v;
}

json_t::json_t(unsigned v) : json_t()
{
    *this = v;
}

json_t::json_t(int64_t v) : json_t()
{
    *this = v;
}

json_t::json_t(uint64_t v) : json_t()
{
    *this = v;
}

json_t::json_t(double v) : json_t()
{
    *this = v;
}

json_t::json_t(const std::string_view& v) : json_t()
{
    *this = v;
}

json_t::json_t(const char *v) : json_t()
{
    *this = v;
}

json_t::json_t(bool v) : json_t()
{
    *this = v;
}

json_t json_t::array()
{
    json_t r;
    yyjson_mut_set_arr(r.v);
    return r;
}

json_t json_t::null()
{
    json_t r;
    yyjson_mut_set_null(r.v);
    return r;
}

std::optional<std::string> json_t::parse_string(const std::string_view& source,
    json_t& result)
{
    yyjson_read_err error;
    auto doc = yyjson_read_opts((char*)source.data(), source.length(), 0, NULL, &error);

    if (!doc)
    {
        return std::string("Failed to parse JSON, error ") +
               std::to_string(error.code) + ": " + error.msg + std::string("(at offset ") +
               std::to_string(error.pos) + ")";
    }

    result = json_t{yyjson_doc_mut_copy(doc, NULL)};
    yyjson_doc_free(doc);
    return std::nullopt;
}

void json_t::map_serialized(std::function<void(const char*source, size_t length)> callback) const
{
    size_t len;
    char *result = yyjson_mut_write(this->doc, 0, &len);
    callback(result, len);
    free(result);
}

std::string json_t::serialize() const
{
    std::string result;
    map_serialized([&] (const char *source, size_t length)
    {
        result.append(source, length);
    });

    return result;
}
} // namespace wf
