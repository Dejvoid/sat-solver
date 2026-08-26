#ifndef TSEITIN_HPP_
#define TSEITIN_HPP_

#include <memory>
#include <vector>

class literal;
using clause_t = std::vector<literal>;

class i_formula {
protected:
    size_t id_;
public:
    i_formula(size_t id) : id_(id) {}
    virtual ~i_formula() = default;

    size_t get_id() const { return id_;}
    void get_equisat(std::vector<clause_t>& clauses, bool equivalences = true) const;
    virtual void get_equisat_impl(std::vector<clause_t>& clauses, bool equivalences) const = 0;
    virtual literal to_literal() const = 0;
};

using formula_ptr = std::unique_ptr<i_formula>;

class literal : public i_formula {
    bool negated_;
public:
    literal(size_t id, bool negated = false) 
    : i_formula(id),
      negated_(negated) {}

    void get_equisat_impl(std::vector<clause_t>&, bool) const override {}

    literal to_literal() const override {
        return *this;
    }

    literal operator~() const {
        return literal(id_, !negated_);
    }

    bool get_negation() const { return negated_; }
};

template <typename T>
concept binary_formula = requires() {
    { T::op_name } -> std::convertible_to<std::string_view>;
};

class or_formula : public i_formula {
    std::unique_ptr<i_formula> l;
    std::unique_ptr<i_formula> r;
public:
    static constexpr std::string_view op_name = "or";
    or_formula(formula_ptr left, formula_ptr right, size_t id) 
        : i_formula(id),
          l(std::move(left)),
          r(std::move(right)) {}

    void get_equisat_impl(std::vector<clause_t>& clauses, bool equivalences) const override {
        l->get_equisat_impl(clauses, equivalences);
        r->get_equisat_impl(clauses, equivalences);
        clauses.push_back({~to_literal(), l->to_literal(), r->to_literal()});
        if (equivalences) {
            clauses.push_back({~l->to_literal(), to_literal()});
            clauses.push_back({~r->to_literal(), to_literal()});
        }
    }

    literal to_literal() const override {
        return literal(id_);
    }
};

class and_formula : public i_formula {
    std::unique_ptr<i_formula> l;
    std::unique_ptr<i_formula> r;
public:
    static constexpr std::string_view op_name = "and";
    and_formula(formula_ptr left, formula_ptr right, size_t node_id) 
        : i_formula(node_id),
          l(std::move(left)),
          r(std::move(right)) {}

    void get_equisat_impl(std::vector<clause_t>& clauses, bool equivalences) const override {
        l->get_equisat_impl(clauses, equivalences);
        r->get_equisat_impl(clauses, equivalences);
        clauses.push_back({~to_literal(), r->to_literal()});
        clauses.push_back({~to_literal(), l->to_literal()});
        if (equivalences) {
            clauses.push_back({~r->to_literal(), ~l->to_literal(), to_literal()});
        }
    }

    literal to_literal() const override {
        return literal(id_);
    }
};

inline void i_formula::get_equisat(std::vector<clause_t>& clauses, bool equivalences) const {
    get_equisat_impl(clauses, equivalences);
    clauses.push_back({to_literal()});
}

#endif