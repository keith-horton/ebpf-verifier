#pragma once

#include "crab/bignums.hpp"
#include "crab/debug.hpp"
#include "crab/os.hpp"

#include <iosfwd>
#include <limits>
#include <memory>
#include <optional>
#include <unordered_map>

/* Basic type definitions */

namespace crab {

enum variable_type { INT_TYPE, ARR_INT_TYPE, UNK_TYPE };

using number_t = z_number;

inline crab_os& operator<<(crab_os& o, variable_type t) {
    switch (t) {
    case INT_TYPE: o << "int"; break;
    case ARR_INT_TYPE: o << "arr_int"; break;
    default: o << "unknown"; break;
    }
    return o;
}

enum binary_operation_t {
    BINOP_ADD,
    BINOP_SUB,
    BINOP_MUL,
    BINOP_SDIV,
    BINOP_UDIV,
    BINOP_SREM,
    BINOP_UREM,
    BINOP_AND,
    BINOP_OR,
    BINOP_XOR,
    BINOP_SHL,
    BINOP_LSHR,
    BINOP_ASHR,
    BINOP_FUNCTION
};

enum cast_operation_t { CAST_TRUNC, CAST_SEXT, CAST_ZEXT };

inline crab_os& operator<<(crab_os& o, binary_operation_t op) {
    switch (op) {
    case BINOP_ADD: o << "+"; break;
    case BINOP_SUB: o << "-"; break;
    case BINOP_MUL: o << "*"; break;
    case BINOP_SDIV: o << "/"; break;
    case BINOP_UDIV: o << "/_u"; break;
    case BINOP_SREM: o << "%"; break;
    case BINOP_UREM: o << "%_u"; break;
    case BINOP_AND: o << "&"; break;
    case BINOP_OR: o << "|"; break;
    case BINOP_XOR: o << "^"; break;
    case BINOP_SHL: o << "<<"; break;
    case BINOP_LSHR: o << ">>_l"; break;
    case BINOP_ASHR: o << ">>_r"; break;
    case BINOP_FUNCTION: o << "uf"; break;
    default: CRAB_ERROR("unexpected binary operation ", op);
    }
    return o;
}

inline crab_os& operator<<(crab_os& o, cast_operation_t op) {
    switch (op) {
    case CAST_TRUNC: o << "trunc"; break;
    case CAST_SEXT: o << "sext"; break;
    case CAST_ZEXT: o << "zext"; break;
    default: CRAB_ERROR("unexpected cast operation", op);
    }
    return o;
}

template <typename T>
inline std::optional<T> conv_op(binary_operation_t op);

template <typename T>
inline std::optional<T> conv_op(cast_operation_t op);

// Numerical type for indexed objects
using index_t = uint64_t;

// Interface for writeable objects
class writeable {
  public:
    virtual void write(crab_os& o) = 0;
    virtual ~writeable() {}
}; // class writeable

inline crab_os& operator<<(crab_os& o, writeable& x) {
    x.write(o);
    return o;
}

class variable_factory;

using var_key = std::string;
// FIXME: we should use some unlimited precision type to avoid overflow.
// However, this change is a bit involving since we need to change the
// algorithm api's in patricia_trees.hpp because they assume index_t.

class indexed_string {
    friend class variable_factory;

  private:
    std::optional<var_key> _s;
    index_t _id;
    std::string _name; // optional string name associated with _id
    variable_factory* _vfac;

    // NOT IMPLEMENTED
    indexed_string();

    indexed_string(index_t id, variable_factory* vfac, std::string name = "") : _id(id), _name(name), _vfac(vfac) {}

    indexed_string(var_key s, index_t id, variable_factory* vfac) : _s(s), _id(id), _name(""), _vfac(vfac) {}

  public:
    indexed_string(const indexed_string& is) : _s(is._s), _id(is._id), _name(is._name), _vfac(is._vfac) {}

    indexed_string& operator=(const indexed_string& is) {
        if (this != &is) {
            _s = is._s;
            _id = is._id;
            _name = is._name;
            _vfac = is._vfac;
        }
        return *this;
    }

    index_t index() const { return this->_id; }

    std::string str() const;

    std::optional<var_key> get() const { return _s ? *_s : std::optional<var_key>(); }

    variable_factory& get_var_factory() { return *_vfac; }

    bool operator<(indexed_string s) const { return (_id < s._id); }

    bool operator==(indexed_string s) const { return (_id == s._id); }

    void write(crab_os& o) const { o << str(); }

    friend crab_os& operator<<(crab_os& o, indexed_string s) {
        o << s.str();
        return o;
    }

    friend size_t hash_value(indexed_string s) {
        std::hash<index_t> hasher;
        return hasher(s.index());
    }
};

using varname_t = indexed_string;

// This variable factory creates a new variable associated to an
// element of type var_key. It can also create variables that are not
// associated to an element of type var_key. We call them shadow variables.
//
// The factory uses a counter of type index_t to generate variable
// id's that always increases.
class variable_factory {
    using t_map_t = std::unordered_map<var_key, indexed_string>;
    using shadow_map_t = std::unordered_map<index_t, indexed_string>;

    index_t _next_id{1};
    t_map_t _map;
    shadow_map_t _shadow_map;
    std::vector<indexed_string> _shadow_vars;

    index_t get_and_increment_id();

  public:
    variable_factory(){};
    variable_factory(const variable_factory&) = delete;

    // hook for generating indexed_string's without being
    // associated with a particular var_key (w/o caching).
    // XXX: do not use it unless strictly necessary.
    indexed_string get();

    // generate a shadow indexed_string's associated to some key
    indexed_string get(index_t key, std::string name = "");

    indexed_string operator[](var_key s);
};

// Container for typed variables used by the crab abstract domains
// and linear_constraints.
class variable_t {
    // XXX: template parameter Number is required even if the class
    // does not use it.  This allows, e.g., linear_constraint to
    // deduce the kind of Number from constraints like x < y.

  public:
    using bitwidth_t = unsigned;
    using type_t = variable_type;

  private:
    varname_t _n;
    type_t _type;
    bitwidth_t _width;

  public:
    /**
     * DO NOT USE this constructor to create a CFG since all CFG
     * statements must be strongly typed.  This constructor is
     * intended to be used only abstract domains to generate temporary
     * variables.
     **/
    explicit variable_t(const varname_t& n) : _n(n), _type(UNK_TYPE), _width(0) {}

  public:
    variable_t(const varname_t& n, type_t type) : _n(n), _type(type), _width(0) {}

    variable_t(const varname_t& n, type_t type, bitwidth_t width) : _n(n), _type(type), _width(width) {}

    variable_t(const variable_t& o) : _n(o._n), _type(o._type), _width(o._width) {}

    variable_t(variable_t&& o) : _n(std::move(o._n)), _type(std::move(o._type)), _width(std::move(o._width)) {}

    variable_t& operator=(const variable_t& o) {
        if (this != &o) {
            _n = o._n;
            _type = o._type;
            _width = o._width;
        }
        return *this;
    }

    bool is_typed() const { return _type != UNK_TYPE; }

    bool is_array_type() const { return is_typed() && _type >= ARR_INT_TYPE; }

    bool is_int_type() const { return _type == INT_TYPE; }

    type_t get_type() const { return _type; }

    bool has_bitwidth() const { return _width > 0; }

    bitwidth_t get_bitwidth() const { return _width; }

    const varname_t& name() const { return _n; }

    // Cannot be const because from varname_t we might want to
    // access to its variable factory to create e.g., new
    // varname_t's.
    varname_t& name() { return _n; }

    index_t index() const { return _n.index(); }

    std::size_t hash() const { return (size_t)_n.index(); }

    bool operator==(const variable_t& o) const { return _n.index() == o._n.index(); }

    bool operator!=(const variable_t& o) const { return (!(operator==(o))); }

    void write(crab_os& o) const { o << _n; }

    friend class less;
    struct less {
        bool operator()(variable_t x, variable_t y) const { return x._n.index() < y._n.index(); }
    };
}; // class variable_t

inline size_t hash_value(const variable_t& v) { return v.hash(); }

inline crab_os& operator<<(crab_os& o, const variable_t& v) {
    v.write(o);
    return o;
}

using basic_block_label_t = std::string;

} // namespace crab
