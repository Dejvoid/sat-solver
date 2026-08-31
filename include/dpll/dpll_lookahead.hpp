#ifndef DPLL_LOOKAHEAD_HPP_
#define DPLL_LOOKAHEAD_HPP_

#include "dpll/dpll_base.hpp"

#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// DPLL with look-ahead.
//
// Instead of a cheap syntactic branching rule, a look-ahead solver invests real
// work at every node to pick a good branching variable: for each free variable
// x it tentatively assigns x, runs unit propagation, and *measures how much the
// formula shrinks* (the "difference" heuristic). The variable whose two
// polarities together reduce the formula the most is branched on. The
// propagation done during look-ahead is not wasted: it also uncovers forced
// literals (failed-literal and necessary-assignment reasoning), which prune the
// search directly.
//
// This class implements the three ingredients the assignment asks for:
//
//   1. Two non-trivial difference heuristics that can be compared:
//        * CRH - Clause Reduction Heuristic: weighted count of every clause that
//          look-ahead shortens, favouring clauses reduced to small size.
//        * WBH - Weighted Binaries Heuristic: only clauses reduced to binary
//          count, each weighted by how constraining its two remaining literals
//          are (occurrence based). This measures the *quality* of the new
//          binaries rather than the raw quantity.
//
//   2. Additional reasoning performed for free during look-ahead:
//        * Failed literals: if look-ahead on l derives a conflict then l is
//          impossible, so ~l is forced.
//        * Necessary assignments: if look-ahead on x and on ~x both imply the
//          same literal k, then k holds regardless of x and is forced.
//        * Local learning: whenever look-ahead on l implies a literal k, the
//          binary clause (~l v k) is a logical consequence of the formula. It
//          is added as an eager binary implication so later propagation - at
//          this node and everywhere below it - can fire k directly from l
//          without redoing the look-ahead that discovered it.
//
//   3. Eager data structures for cheap unit propagation:
//        * Binary clauses are stored as direct implication lists (l => k), so
//          propagating a binary is O(1) with no clause scan.
//        * Longer clauses keep an eager counter of their non-false literals and
//          of their satisfying literals, so becoming unit/binary/false is
//          detected by counter updates rather than by rescanning the clause.
//          These counters are exactly what makes the difference heuristics cheap
//          to evaluate, because every reduction is observed as it happens.
class dpll_lookahead : public dpll_base {
public:
    // Which difference heuristic to score look-aheads with.
    enum class heuristic_kind { CRH, WBH };

    explicit dpll_lookahead(heuristic_kind h = heuristic_kind::CRH, bool learn = true)
        : kind(h),
          name_(h == heuristic_kind::CRH ? "lookahead-crh" : "lookahead-wbh") {
        local_learning = learn;
    }

private:
    heuristic_kind kind;
    std::string name_;


    // bin_impl[lit_index(l)] holds literals implied when `l` is true
    // (a or b) results in: bin_impl[lit_index(~a)] = b, bin_impl[lit_index(~b)] = a
    std::vector<std::vector<literal>> bin_impl;

    // occ_long[lit_index(l)] holds indices of every clause (size >= 3) containing `l`
    std::vector<std::vector<size_t>> occ_long;

    // non_false[clause index] = number of literals not yet falsified in given clause
    // non_false = 0 -> conflict, non_false = 1 -> unit clause
    std::vector<int> non_false;
    // sat_by[clause index] = number of literals currently set to true (if > 0, the clause is satisfied)
    std::vector<int> sat_by;

    // static weight for WBH - how many clauses does the given literal occurs in
    std::vector<double> occ_weight;

 
    bool measuring = false;   // true while a trial look-ahead is being scored
    double la_measure = 0.0;  // accumulated heuristic value of the current trial

    // reason marker for literals forced by a binary implication
    static constexpr size_t BIN_REASON = static_cast<size_t>(-2);


    size_t look_aheads = 0;
    size_t failed_literals = 0;
    size_t necessary_assignments = 0;
    size_t learned_binaries = 0;

    // Whether local learning is enabled (learn binary implications found during
    // look-ahead as eager binaries).
    bool local_learning = true;

    // tells if l => k is learnt. keyed by (lit_index(l), lit_index(k))
    std::set<std::pair<size_t, size_t>> learnt_seen;

    // learning (a or b) fills `learned_seen` and `bin_impl`. This class holds that information with trail_depth
    struct learned_binary {
        // when we learnt
        size_t trail_depth;
        // bin_impl[slot_a].back() == b is the implication we learnt from this
        size_t slot_a;
        // bin_impl[slot_b].back() == a is the implication we learnt from this
        size_t slot_b;
        // `learned_seen` entry
        std::pair<size_t, size_t> key;
    };
    std::vector<learned_binary> learnt_log;

    // keyed by lit_index, used for marking necessary assignment
    std::vector<bool> na_mark;

    /**
     * @brief CRH weight calculation for clause of size k > 2.
     * The larger the clause, the less important
     * @param k size of the clause
     * @return double calculated weight
     */
    static double crh_weight(int k) {
        double w = 1.0;
        for (int i = 2; i < k; ++i) w *= 0.2;
        return w;
    }

    /**
     * @brief Locate non-false literal in long clause containing 1 non-false literal (long clause that is a unit)
     * 
     * @param c index of long clause
     * @return literal non-false literal
     */
    literal find_non_false(size_t c) const {
        const clause_t& cl = clauses[c];
        for (const literal& l : cl)
            if (lit_val(l) != assignment::False) return l;
        return cl[0];  // unreachable when the caller's precondition holds
    }

    /**
     * @brief Apply `l`, update counters, enqueue newly derived lietrals
     * 
     * @param l literal that became true
     * @return true if clause became false (unsat)
     * @return false else
     */
    bool apply_assign(const literal& l) {
        bool conflict = false;
        const size_t li = lit_index(l);
        const size_t nli = lit_index(~l);

        // long clauses that contain l are now satisfied.
        for (size_t c : occ_long[li]) {
            ++clause_checks;
            ++sat_by[c];
        }

        // long clauses that contain ~l lose one non-false literal.
        for (size_t c : occ_long[nli]) {
            ++clause_checks;
            --non_false[c];
            if (sat_by[c] > 0) continue; // already satisfied
            int k = non_false[c];
            if (measuring) accumulate_measure(c, k);
            if (k == 0) {
                conflict = true;  // all literals false
            } else if (k == 1) {
                literal u = find_non_false(c);
                assignment uv = lit_val(u);
                if (uv == assignment::False) {
                    conflict = true;
                } else if (uv == assignment::Unassigned && !conflict) {
                    ++propagations;
                    push_assignment(u, c);
                }
            }
            // k >= 2: nothing to force.
        }

        // binary implications of l
        for (const literal& k : bin_impl[li]) {
            ++clause_checks;
            assignment kv = lit_val(k);
            if (kv == assignment::True) continue;
            if (kv == assignment::False) {
                conflict = true;
                continue;
            }
            if (!conflict) {
                ++propagations;
                push_assignment(k, BIN_REASON);
            }
        }
        return conflict;
    }

    // reverse what apply_assign did
    void revert_assign(const literal& l) {
        for (size_t c : occ_long[lit_index(l)]) --sat_by[c];
        for (size_t c : occ_long[lit_index(~l)]) ++non_false[c];
    }

    // Fold a clause reduced to size k (unsatisfied) into the current trial score.
    void accumulate_measure(size_t c, int k) {
        if (k < 2) return;  // units are captured by the propagation they trigger
        if (kind == heuristic_kind::CRH) {
            la_measure += crh_weight(k);
        } else {  // WBH: only brand-new binaries, weighted by their two literals
            if (k == 2) {
                const clause_t& cl = clauses[c];
                double w = 0.0;
                for (const literal& lit : cl)
                    if (lit_val(lit) != assignment::False) w += occ_weight[lit_index(lit)];
                la_measure += w;
            }
        }
    }

    bool init() override {
        bin_impl.assign(literal_slots(), {});
        occ_long.assign(literal_slots(), {});
        occ_weight.assign(literal_slots(), 0.0);
        non_false.assign(clauses.size(), 0);
        sat_by.assign(clauses.size(), 0);
        na_mark.assign(literal_slots(), 0);

        for (size_t c = 0; c < clauses.size(); ++c) {
            const clause_t& cl = clauses[c];
            if (cl.empty()) return false;  // empty clause -> UNSAT

            for (const literal& l : cl) occ_weight[lit_index(l)] += 1.0;

            if (cl.size() == 1) {  // top-level unit
                assignment v = lit_val(cl[0]);
                if (v == assignment::False) return false;
                if (v == assignment::Unassigned) push_assignment(cl[0], c);
                continue;
            }
            if (cl.size() == 2) {  // eager binary implication both ways
                bin_impl[lit_index(~cl[0])].push_back(cl[1]);
                bin_impl[lit_index(~cl[1])].push_back(cl[0]);
                continue;
            }
            non_false[c] = static_cast<int>(cl.size());  // long clause: eager counter
            for (const literal& l : cl) occ_long[lit_index(l)].push_back(c);
        }
        return true;
    }

    /**
     * @brief propagate the unassigned trail
     * 
     * @return true conflict (UNSAT)
     * @return false no conflict
     */
    bool propagate() override {
        while (qhead < trail.size()) {
            literal l = trail[qhead];
            bool conflict = apply_assign(l);
            ++qhead;
            if (conflict) {
                conflict_clause = NO_REASON;
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Undo the assignments on trail until the `size`. (backtrack) - effect is the same as dpll_base::undo_to
     * 
     * @param target size of trail to keep
     */
     // TODO: Unify naming of dpll_base::undo_to
    void backtrack_to(size_t target) {
        // remove the learnt
        while (!learnt_log.empty() && learnt_log.back().trail_depth > target) {
            const learned_binary& lb = learnt_log.back();
            bin_impl[lb.slot_a].pop_back();
            bin_impl[lb.slot_b].pop_back();
            learnt_seen.erase(lb.key);
            learnt_log.pop_back();
        }

        size_t p = trail.size();
        while (p > target) {
            --p;
            const literal l = trail[p];
            if (p < qhead) revert_assign(l);
            values[l.get_id()] = assignment::Unassigned;
            heuristic->notify_unassign(l);
        }
        trail.erase(trail.begin() + static_cast<std::ptrdiff_t>(target), trail.end());
        if (qhead > target) qhead = target;
    }

    struct la_result {
        bool conflict; // did propagation derive a conflict?
        double measure; // difference-heuristic value (if no conflict)
        std::vector<literal> implied; // literals forced by l (if no conflict)
    };

    // try assigning `p` (negation is value), score and undo, return result
    la_result look_ahead(const literal& p) {
        ++look_aheads;
        const size_t base = trail.size();
        la_measure = 0.0;
        measuring = true;
        push_assignment(p);
        bool conflict = propagate();
        measuring = false;

        la_result r;
        r.conflict = conflict;
        r.measure = la_measure;
        if (!conflict) {
            for (size_t i = base + 1; i < trail.size(); ++i) r.implied.push_back(trail[i]);
            if (local_learning) learn_binaries(p, r.implied, base);
        }
        backtrack_to(base);
        return r;
    }

    /**
     * @brief local learning - fills bin_impl, learnt_seen and learnt_log
     * 
     * @param p 
     * @param implied 
     * @param depth 
     */
    void learn_binaries(const literal& p, const std::vector<literal>& implied, size_t depth) {
        for (const literal& k : implied) {
            if (is_same(k, p)) continue;  // p => p is trivial
            auto key = std::make_pair(lit_index(p), lit_index(k));
            if (!learnt_seen.insert(key).second) continue;  // already learned
            bin_impl[lit_index(p)].push_back(k);    // p  => k
            bin_impl[lit_index(~k)].push_back(~p);  // ~k => ~p (contrapositive)
            learnt_log.push_back({depth, lit_index(p), lit_index(~k), key});
            ++learned_binaries;
        }
    }

    // outcome of lookahead
    enum class step { sat, conflict, forced, decision };

    // Run look-ahead over all free variables. Either forces a literal (failed
    // literal or necessary assignment), reports a conflict, reports SAT when no
    // free variable remains, or selects a branching literal.
    /**
     * @brief select literal using look ahead (pick from all free variables)
     * 
     * @param chosen (out) selected literal (forced or decision)
     * @return step `conflict` if conflict, `sat` if no free variable, `forced` or `decision` when `chosen` literal selected
     */
    step look_ahead_decision(literal& chosen) {
        double best_score = -1.0;
        size_t best_var = 0;
        literal best_lit{1};

        for (size_t x = 1; x <= var_count; ++x) {
            if (values[x] != assignment::Unassigned) continue;

            la_result pos = look_ahead(literal{x, false});  // x = true
            la_result neg = look_ahead(literal{x, true});   // x = false

            if (pos.conflict && neg.conflict) return step::conflict;  // both fail
            if (pos.conflict) {  // x impossible -> force ~x
                ++failed_literals;
                push_assignment(literal{x, true});
                return step::forced;
            }
            if (neg.conflict) {  // ~x impossible -> force x
                ++failed_literals;
                push_assignment(literal{x, false});
                return step::forced;
            }

            // Necessary assignments: literals implied by BOTH polarities.
            if (force_common(pos.implied, neg.implied)) return step::forced;

            double score = 1024.0 * pos.measure * neg.measure + pos.measure + neg.measure;
            if (score > best_score) {
                best_score = score;
                best_var = x;
                // Branch first on the polarity that reduced the formula more.
                best_lit = (pos.measure >= neg.measure) ? literal{x, false} : literal{x, true};
            }
        }

        if (best_var == 0) return step::sat;  // every variable assigned
        chosen = best_lit;
        return step::decision;
    }

    // force every literal that occurs in both implied sets (a necessary assignment). 
    // returns true if at least one literal was forced.
    bool force_common(const std::vector<literal>& a, const std::vector<literal>& b) {
        bool forced = false;
        for (const literal& l : a) na_mark[lit_index(l)] = 1;
        for (const literal& l : b) {
            if (na_mark[lit_index(l)] && lit_val(l) == assignment::Unassigned) {
                push_assignment(l);
                ++necessary_assignments;
                forced = true;
            }
        }
        for (const literal& l : a) na_mark[lit_index(l)] = 0;  // clear marks
        return forced;
    }

    // false when no more possible decisions (UNSAT).
    bool backtrack_decision() {
        while (!open_decisions.empty() && open_decisions.back().tried_both) {
            backtrack_to(open_decisions.back().trail_before);
            open_decisions.pop_back();
        }
        if (open_decisions.empty()) return false;
        decision& d = open_decisions.back();
        backtrack_to(d.trail_before);
        d.tried_both = true;
        ++decisions;
        push_assignment(~d.lit);
        return true;
    }

public:
    bool get_sat(const std::vector<clause_t>& formula, size_t vc,
                 std::vector<bool>& model) override {
        reset(formula, vc);
        look_aheads = failed_literals = necessary_assignments = 0;
        learned_binaries = 0;
        learnt_seen.clear();
        learnt_log.clear();
        if (!init()) return false;  // trivially UNSAT

        while (true) {
            if (propagate()) { // conflict from real propagation
                if (!backtrack_decision()) return false;
                continue;
            }

            literal chosen{1};
            switch (look_ahead_decision(chosen)) {
                case step::sat:
                    model.assign(var_count + 1, false);
                    for (size_t i = 1; i <= var_count; ++i)
                        model[i] = (values[i] == assignment::True);
                    return true;
                case step::conflict:
                    if (!backtrack_decision()) return false;
                    break;
                case step::forced:
                    break;  // forced literals are pending; loop re-propagates
                case step::decision:
                    ++decisions;
                    open_decisions.push_back({chosen, false, trail.size()});
                    push_assignment(chosen);
                    break;
            }
        }
    }

    const std::string_view name() const override { return name_; }

    void print_extra_stats(std::ostream& os) const override {
        os << "c Look-aheads: " << look_aheads << "\n";
        os << "c Failed literals: " << failed_literals << "\n";
        os << "c Necessary assignments: " << necessary_assignments << "\n";
        os << "c Learned binaries: " << learned_binaries << "\n";
    }
};

#endif
