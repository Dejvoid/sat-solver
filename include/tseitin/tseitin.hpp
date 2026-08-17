#ifndef TSEITIN_HPP_
#define TSEITIN_HPP_

#include <memory>
#include <vector>

class literal;
using clause_t = std::vector<literal>;

class i_formula {
protected:
    size_t id;
public:
    i_formula(size_t id) : id(id) {}
    virtual ~i_formula() = default;

    size_t get_id() const { return id;}
    virtual void get_equisat(std::vector<clause_t>& clauses) const = 0;
    virtual literal to_literal() const = 0;
};

using formula_ptr = std::unique_ptr<i_formula>;

class literal : public i_formula {
    bool negated;
public:
    literal(size_t id, bool negated = false) 
    : i_formula(id),
      negated(negated) {}

    void get_equisat(std::vector<clause_t>& clauses) const override {
        return;
    }

    literal to_literal() const override {
        return *this;
    }

    literal operator~() const {
        return literal(id, !negated);
    }

    bool get_negation() const { return negated; }
};

class or_formula : public i_formula {
    std::unique_ptr<i_formula> l;
    std::unique_ptr<i_formula> r;
public:
    or_formula(formula_ptr left, formula_ptr right, size_t id) 
        : i_formula(id),
          l(std::move(left)),
          r(std::move(right)) {}

    void get_equisat(std::vector<clause_t>& clauses) const override {
        l->get_equisat(clauses);
        r->get_equisat(clauses);
        clauses.push_back({~to_literal(), l->to_literal(), r->to_literal()});
        clauses.push_back({~l->to_literal(), to_literal()});
        clauses.push_back({~r->to_literal(), to_literal()});
    }

    literal to_literal() const override {
        return literal(id);
    }
};

class and_formula : public i_formula {
    std::unique_ptr<i_formula> l;
    std::unique_ptr<i_formula> r;
public:
    and_formula(formula_ptr left, formula_ptr right, size_t id) 
        : i_formula(id),
          l(std::move(left)),
          r(std::move(right)) {}

    void get_equisat(std::vector<clause_t>& clauses) const override {
        l->get_equisat(clauses);
        r->get_equisat(clauses);
        clauses.push_back({~to_literal(), r->to_literal()});
        clauses.push_back({~to_literal(), l->to_literal()});
        clauses.push_back({~r->to_literal(), ~l->to_literal(), to_literal()});
    }

    literal to_literal() const override {
        return literal(id);
    }
};


#endif