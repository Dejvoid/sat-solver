#ifndef DPLL_ADJACENCY_HPP_
#define DPLL_ADJACENCY_HPP_

#include "dpll/dpll_base.hpp"

#include <vector>

class dpll_adjacency : public dpll_base {
    std::vector<std::vector<int>> clauses_containing;  // clauses_containing[lit_index] = clauses containing lit

    bool init() override {
        clauses_containing.assign(literal_slots(), {});
        for (size_t c = 0; c < clauses.size(); ++c) {
            const clause_t& cl = clauses[c];
            if (cl.empty()) return false;
            if (cl.size() == 1) {  // top-level unit
                assignment v = lit_val(cl[0]);
                if (v == assignment::False) return false; // if we already assigned it, this could be a contradiction e.g. (not X) and (X)
                if (v == assignment::Unassigned) push_assignment(cl[0]);
                continue;
            }
            for (const literal& l : cl) clauses_containing[lit_index(l)].push_back(static_cast<int>(c));
        }
        return true;
    }

    bool propagate() override {
        while (qhead < trail.size()) {
            literal false_lit = ~trail[qhead++];  // this literal just became false (on trail we keep the assignment value)
            auto& lst = clauses_containing[lit_index(false_lit)];
            for (size_t i = 0; i < lst.size(); ++i) {
                int c = lst[i];
                ++clause_checks;
                const clause_t& cl = clauses[static_cast<size_t>(c)];

                int unassigned = 0;
                literal last = cl[0];
                bool sat = false;
                for (const literal& l : cl) {
                    assignment v = lit_val(l);
                    if (v == assignment::True) { sat = true; break; }
                    if (v == assignment::Unassigned) { ++unassigned; last = l; }
                }
                if (sat) continue;
                if (unassigned == 0) { notify_conflict(cl); return true; }  // conflict
                if (unassigned == 1) { // if there is only 1 unassigned literal, we must assign it
                    ++propagations;
                    push_assignment(last);
                }
            }
        }
        return false;
    }

public:
    const std::string_view name() const override { return "adjacency"; }
};

#endif
