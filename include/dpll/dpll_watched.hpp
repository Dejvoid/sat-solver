#ifndef DPLL_WATCHED_HPP_
#define DPLL_WATCHED_HPP_

#include "dpll/dpll_base.hpp"

#include <array>
#include <vector>

class dpll_watched : public dpll_base {
protected:
    // clauses_watching[lit_index(p)] = indices of clauses currently watching literal p.
    std::vector<std::vector<size_t>> clauses_watching;
    // watched[c] = the two positions inside clause c that are currently watched.
    std::vector<std::array<size_t, 2>> watched_positions;

    bool init() override {
        clauses_watching.assign(literal_slots(), {});
        watched_positions.assign(clauses.size(), {0, 0});
        for (size_t c = 0; c < clauses.size(); ++c) {
            const clause_t& cl = clauses[c];
            if (cl.empty()) return false;      // empty clause -> UNSAT
            if (cl.size() == 1) {  // a unit clause has no second literal to watch
                assignment v = lit_val(cl[0]);
                if (v == assignment::False) return false;  // contradicts an earlier unit -> UNSAT
                if (v == assignment::Unassigned) push_assignment(cl[0], c);  // force its only literal true
                continue;
            }

            // TODO: rework into watch_clause() - implemented in cdcl
            watched_positions[c] = {0, 1};
            clauses_watching[lit_index(cl[0])].push_back(c);
            clauses_watching[lit_index(cl[1])].push_back(c);
        }
        return true;
    }

    /**
     * @brief Unit propagation - we have a queue of literals - the trail that has not yet been propagated
     * 
     * @return true on conflict
     * @return false no conflict
     */
    bool propagate() override {
        while (qhead < trail.size()) {
            literal f_lit = ~trail[qhead++];  // this literal just became false
            auto& watching_f_lit = clauses_watching[lit_index(f_lit)];

            size_t i = 0, j = 0;
            while (i < watching_f_lit.size()) {
                size_t c = watching_f_lit[i++];
                ++clause_checks;
                const clause_t& cl = clauses[c];
                std::array<size_t, 2>& w = watched_positions[c];

                size_t mine = is_same(cl[w[0]], f_lit) ? 0u : 1u; // the watch that is `f_lit`
                const literal& other = cl[w[1 - mine]];

                // case 1: the other watch is already true -> clause satisfied.
                // keep watching `f_lit`.
                if (lit_val(other) == assignment::True) {
                    watching_f_lit[j++] = c;
                    continue;
                }

                // case 2: look for a replacement watch - any literal that is not false and is not `other`
                bool found = false;
                for (size_t k = 0; k < cl.size(); ++k) {
                    if (k == w[0] || k == w[1]) continue;  // skip both current watches
                    if (lit_val(cl[k]) != assignment::False) {  // non-false candidate
                        w[mine] = k;                       // repoint this watch
                        clauses_watching[lit_index(cl[k])].push_back(c);  // move c to its list
                        found = true;
                        break;
                    }
                }
                if (found) continue;  // clause left this list; do not write it back

                // case 3: no replacement, so `other` is the last non-false hope.
                // keep watching `f_lit` (nothing better to watch).
                watching_f_lit[j++] = c;
                if (lit_val(other) == assignment::False) { // both watches false
                    conflict_clause = c;
                    notify_conflict(cl);
                    while (i < watching_f_lit.size()) watching_f_lit[j++] = watching_f_lit[i++];
                    watching_f_lit.resize(j);
                    return true;  // conflicting clause
                }
                // `other` is unassigned -> clause is unit; force it true.
                ++propagations;
                push_assignment(other, c);
            }
            watching_f_lit.resize(j);
        }
        return false;  // queue drained with no conflict
    }

public:
    const std::string_view name() const override { return "watched"; }
};

#endif
