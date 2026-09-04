#ifndef DPLL_BASE_HPP_
#define DPLL_BASE_HPP_

#include "dpll/solver.hpp"
#include "dpll/heuristic.hpp"

#include <cstdint>
#include <memory>
#include <vector>

enum class assignment : int8_t {
    False = -1,
    Unassigned = 0,
    True = 1,
};

class dpll_base : public i_solver {
protected:
    size_t var_count = 0; // number of variables; ids run 1..var_count
    std::vector<clause_t> clauses;

    // current truth value of every variable, indexed by variable id (slot 0 is unused)
    std::vector<assignment> values;

    // literals assigned true, in assignment order
    std::vector<literal> trail;

    // trail[0, qhead) have already been propagated
    // literals in trail[qhead, end) are assigned but still pending propagation
    size_t qhead = 0;

    struct decision {
        literal lit;
        bool tried_both;
        size_t trail_before; // for fast backtrack
    };
    std::vector<decision> open_decisions;  // stack of decision levels, oldest first

    size_t decisions = 0;
    size_t propagations = 0;
    size_t clause_checks = 0;

    std::unique_ptr<branching_heuristic> heuristic;

    /**
     * @brief maps literal to its index
     * 
     * @param l literal (positive or negative)
     * @return size_t index of the literal based on its polarity
     */
    static size_t lit_index(const literal& l) {
        return 2 * l.get_id() + (l.get_negation() ? 1u : 0u);
    }

    /**
     * @brief gets ammount of slots for literals (both positive and negative versions)
     * 
     * @return size_t 
     */
    size_t literal_slots() const {
        return 2 * (var_count + 1);
    }

    // True when `a` and `b` are the exact same literal (same variable and sign).
    static bool is_same(const literal& a, const literal& b) {
        return a == b;
    }

    assignment lit_val(const literal& l) const {
        assignment v = values[l.get_id()];
        if (v == assignment::Unassigned) return assignment::Unassigned;
        return ((v == assignment::True) != l.get_negation()) ? assignment::True
                                                             : assignment::False;
    }

    static constexpr size_t NO_REASON = static_cast<size_t>(-1);

    size_t conflict_clause = NO_REASON;

    /**
     * @brief Called when literal is assigned. More complex versions will use this
     * 
     * @param l literal that is being assigned
     * @param reason clause that or NO_REASON
     */
    virtual void on_assign(const literal& /*l*/, size_t /*reason*/) {}

    /**
     * @brief assign l and push on trail
     * 
     * @param l literal with the decision
     * @param reason optional clause that caused the decision (used in more complex versions)
     */
    void push_assignment(const literal& l, size_t reason = NO_REASON) {
        values[l.get_id()] = l.get_negation() ? assignment::False : assignment::True;
        trail.push_back(l);
        on_assign(l, reason);
        heuristic->notify_assign(l);
    }

    /**
     * @brief Undo the assignments on trail until the `size`. (backtrack)
     * 
     * @param size size of trail to keep
     */
    void undo_to(size_t size) {
        while (trail.size() > size) {
            const literal l = trail.back();
            values[l.get_id()] = assignment::Unassigned;
            trail.pop_back();
            heuristic->notify_unassign(l);
        }
        if (qhead > trail.size()) qhead = trail.size();
    }

    /**
     * @brief Notify heuristics about the conflict
     * 
     * @param conflict clause that caused conflict
     */
    void notify_conflict(const clause_t& conflict) {
        heuristic->notify_conflict(conflict);
    }

    /**
     * @brief Does top level unit propagation (e.g. and (not (x) x))
     * 
     * @return true no conflict
     * @return false on conflict - UNSAT
     */
    virtual bool init() = 0;

    /**
     * @brief Unit propagation on all not-propagated yet. 
     * 
     * @return true on conflict - UNSAT
     * @return false no conflict
     */
    virtual bool propagate() = 0;

    void reset(const std::vector<clause_t>& formula, size_t vc) {
        var_count = vc;
        clauses = formula;
        values.assign(var_count + 1, assignment::Unassigned);
        trail.clear();
        qhead = 0;
        open_decisions.clear();
        conflict_clause = NO_REASON;
        decisions = propagations = clause_checks = 0;
        if (!heuristic) heuristic = std::make_unique<first_unassigned>();
        heuristic->start(var_count, clauses);
    }

public:
    void set_heuristic(std::unique_ptr<branching_heuristic> h) {
        heuristic = std::move(h);
    }

    bool get_sat(const std::vector<clause_t>& formula, size_t vc,
                 std::vector<bool>& model) override {
        reset(formula, vc);
        if (!init()) return false;  // trivially UNSAT

        while (true) {
            if (propagate()) { // conflict
                while (!open_decisions.empty() && open_decisions.back().tried_both) {
                    undo_to(open_decisions.back().trail_before);
                    open_decisions.pop_back();
                }
                // nothing left -> the formula is UNSAT.
                if (open_decisions.empty()) return false;

                // try other branch
                decision& d = open_decisions.back();
                undo_to(d.trail_before);
                d.tried_both = true;
                ++decisions;
                push_assignment(~d.lit);
            } else { // no conflict
                if (heuristic->all_assigned()) {  // full assignment -> SAT
                    assign_model(model);
                    return true;
                }

                // make a decision
                literal chosen = heuristic->decide();
                ++decisions;
                open_decisions.push_back({chosen, false, trail.size()});
                push_assignment(chosen);
            }
        }
    }

    size_t get_decisions() const override { return decisions; }
    size_t get_propagations() const override { return propagations; }
    size_t get_clause_checks() const override { return clause_checks; }

    void print_extra_stats(std::ostream& os) const override {
        if (heuristic) os << "c Heuristic: " << heuristic->name() << std::endl;
    }
protected:
    void assign_model(std::vector<bool>& model) {
        model.assign(var_count + 1, false);
        for (size_t i = 1; i <= var_count; ++i) {
            model[i] = (values[i] == assignment::True);
        }
    }
};

#endif
