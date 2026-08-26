#ifndef DPLL_HPP_
#define DPLL_HPP_

#include "dpll/solver.hpp"
#include "tseitin/tseitin.hpp"
#include <vector>

class dpll : public i_solver {
size_t decisions = 0;
size_t propagations = 0;
size_t clause_checks = 0;

bool unit_propagation(std::vector<clause_t>& formula, std::vector<bool>& model) {
    // TODO: Check if it would be worth it sorting the formula by clause size first
    if (has_empty_clause(formula)) return false; // not satisfiable

    size_t unit_index;
    while (get_unit_clause(formula, unit_index)) {
        // add to model
        auto literal = formula[unit_index][0];
        update_model(model, literal);
        ++propagations;

        // assign in formula
        formula = apply_assignment(formula, literal);

        if (has_empty_clause(formula)) return false; // not satisfiable
    }
    return true; // satisfiable
}

// TODO(perf): merge has_empty_clause and get_unit_clause to 1 loop together with apply_assignment

bool has_empty_clause(const std::vector<clause_t>& formula) {
    for(auto&& c : formula) {
        ++clause_checks;
        if (c.empty()) return true;
    }
    return false;
}

bool get_unit_clause(std::vector<clause_t>& formula, size_t& index) {
    for (size_t i = 0; i < formula.size(); ++i) {
        ++clause_checks;
        if (formula[i].size() == 1) {
            index = i;
            return true;
        }
    }
    return false;
}

// Constructs new formula with assigned literal

std::vector<clause_t> apply_assignment(const std::vector<clause_t>& formula, const literal& literal) {
    std::vector<clause_t> new_formula;
    new_formula.reserve(formula.size() - 1);

    for (auto c : formula) {
        ++clause_checks;
        bool keep_clause = true;
        for (auto it = c.begin(); it != c.end(); ++it) {
            const auto& l = *it;
            if (l.get_id() == literal.get_id()) {
                if (l.get_negation() == literal.get_negation()) {
                    // clause satisfied -> remove
                    keep_clause = false;
                }
                else {
                    // remove literal from clause
                    c.erase(it);
                }
                break;
            }
        }
        if (keep_clause) {
            new_formula.emplace_back(std::move(c));
        }
    }
    return new_formula;
}

literal pick_literal(const std::vector<clause_t>& formula) {
    return formula[0][0];
}

void update_model(std::vector<bool>& model, const literal& literal) {
    model[literal.get_id()] = !literal.get_negation();
}

struct state {
    std::vector<clause_t> formula;
    std::vector<bool> model;
    literal lit;
    bool backtracked;
};

bool solve(std::vector<clause_t> formula, std::vector<bool>& model) {
    bool sat = unit_propagation(formula, model);
    if (!sat) return false;
    if (formula.empty()) return true;

    std::vector<state> stack;
    
    auto tmp_formula = formula;
    auto tmp_model = model;

    while (true) {
        auto lit = pick_literal(tmp_formula);
        ++decisions;

        stack.push_back({
            .formula = tmp_formula,
            .model = tmp_model,
            .lit = lit,
            .backtracked = false
        });

        tmp_formula = apply_assignment(tmp_formula,lit);
        update_model(tmp_model, lit);
        
        sat = unit_propagation(tmp_formula, tmp_model);

        while (!sat) {
            if (stack.empty())
                return false;

            auto& state = stack.back();

            if (!state.backtracked) {
                state.backtracked = true;

                tmp_formula = state.formula;
                tmp_model = state.model;

                auto neg_lit = ~state.lit;
                ++decisions;

                tmp_formula = apply_assignment(tmp_formula, neg_lit);
                update_model(tmp_model,neg_lit);

                sat = unit_propagation(tmp_formula, tmp_model);
            } else {
                stack.pop_back();
            }
        }
        if (tmp_formula.empty()) {
            model = tmp_model;
            return true;
        }
    }
}

public:
bool get_sat(const std::vector<clause_t>& formula, size_t var_count, std::vector<bool>& model) override {
    decisions = propagations = clause_checks = 0;
    if (model.size() < var_count + 1) model.assign(var_count + 1, false);
    return solve(formula, model);
}

size_t get_decisions() const override { return decisions; }
size_t get_propagations() const override { return propagations; }
size_t get_clause_checks() const override { return clause_checks; }
const char* name() const override { return "naive"; }

};

#endif