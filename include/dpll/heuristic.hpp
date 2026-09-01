#ifndef DPLL_HEURISTIC_HPP_
#define DPLL_HEURISTIC_HPP_

#include "tseitin/tseitin.hpp"

#include <cmath>
#include <cstdint>
#include <random>
#include <string_view>
#include <vector>

/**
 * @brief tracks assigned variables, remembers last assignment (phase)
 * 
 */
class branching_heuristic {
public:
    virtual ~branching_heuristic() = default;

    void start(size_t var_count, const std::vector<clause_t>& clauses) {
        assigned_.assign(var_count + 1, false);
        saved_phase_.assign(var_count + 1, true);  // default decision phase: true
        n_unassigned_ = var_count;
        on_start(var_count, clauses);
    }

    void notify_assign(const literal& l) {
        assigned_[l.get_id()] = true;
        saved_phase_[l.get_id()] = !l.get_negation();  // remember polarity
        --n_unassigned_;
        on_assign(l);
    }

    void notify_unassign(const literal& l) {
        assigned_[l.get_id()] = false;
        ++n_unassigned_;
        on_unassign(l);
    }

    void notify_conflict(const clause_t& conflict) { on_conflict(conflict); }

    void notify_bump(size_t var) { on_bump(var); }
    void notify_decay() { on_decay(); }

    // we have full model
    bool all_assigned() const { return n_unassigned_ == 0; }

    virtual literal decide() = 0;

    virtual std::string_view name() const = 0;

protected:
    size_t var_count() const { return assigned_.empty() ? 0 : assigned_.size() - 1; }
    bool is_assigned(size_t var) const { return assigned_[var]; }

    literal saved_phase_literal(size_t var) const {
        return literal(var, !saved_phase_[var]);
    }

    virtual void on_start(size_t /*var_count*/, const std::vector<clause_t>& /*clauses*/) {}
    virtual void on_assign(const literal& /*l*/) {}
    virtual void on_unassign(const literal& /*l*/) {}
    virtual void on_conflict(const clause_t& /*conflict*/) {}
    virtual void on_bump(size_t /*var*/) {}
    virtual void on_decay() {}

private:
    std::vector<bool> assigned_; // true if variable v is set
    std::vector<bool> saved_phase_; // last assignment
    size_t n_unassigned_ = 0;
};

class first_unassigned : public branching_heuristic {
public:
    literal decide() override {
        for (size_t v = 1; v <= var_count(); ++v) {
            if (!is_assigned(v)) return literal(v);  // positive
        }
        return literal(0); // should not happen
    }
    std::string_view name() const override { return "first"; }
};

/**
 * @brief picks a uniformly random unassigned variable and a random polarity
 *
 */
class random_heuristic : public branching_heuristic {
    std::mt19937 rng_;

public:
    explicit random_heuristic(uint32_t seed = 0x1234abcd) : rng_(seed) {}

    literal decide() override {
        // Reservoir sampling: pick a uniformly random unassigned variable in one pass.
        size_t chosen = 0;
        size_t seen = 0;
        for (size_t v = 1; v <= var_count(); ++v) {
            if (is_assigned(v)) continue;
            ++seen;
            if (std::uniform_int_distribution<size_t>(1, seen)(rng_) == 1) chosen = v;
        }
        if (chosen == 0) return literal(0); // should not happen
        const bool negate = std::uniform_int_distribution<int>(0, 1)(rng_) == 1;
        return literal(chosen, negate);
    }
    std::string_view name() const override { return "random"; }
};

// Lowest-id unassigned variable, but branch on its saved phase
class phase_saving : public branching_heuristic {
public:
    literal decide() override {
        for (size_t v = 1; v <= var_count(); ++v) {
            if (!is_assigned(v)) return saved_phase_literal(v);
        }
        return literal(0);
    }
    std::string_view name() const override { return "phase-saving"; }
};

/**
 * @brief static JeroslowWang
 * 
 */
class jeroslow_wang : public branching_heuristic {
    std::vector<double> pos_; // for the positive v
    std::vector<double> neg_; // for the negative v

protected:
    void on_start(size_t var_count, const std::vector<clause_t>& clauses) override {
        pos_.assign(var_count + 1, 0.0);
        neg_.assign(var_count + 1, 0.0);
        for (const clause_t& cl : clauses) {
            const double w = std::ldexp(1.0, -static_cast<int>(cl.size())); // 2^-|cl|
            for (const literal& l : cl) {
                (l.get_negation() ? neg_ : pos_)[l.get_id()] += w;
            }
        }
    }

public:
    literal decide() override {
        double best = -1.0;
        size_t best_var = 0;
        bool best_pos = true;
        for (size_t v = 1; v <= var_count(); ++v) {
            if (is_assigned(v)) continue;
            if (pos_[v] > best) { best = pos_[v]; best_var = v; best_pos = true; }
            if (neg_[v] > best) { best = neg_[v]; best_var = v; best_pos = false; }
        }
        return literal(best_var, !best_pos);
    }
    std::string_view name() const override { return "jeroslow-wang"; }
};

/**
 * @brief activity is bumped when appears in conflict clause, decays over time
 * picks most active free variable
 * 
 */
class vsids : public branching_heuristic {
    std::vector<double> activity_;
    double bump_ = 1.0;
    static constexpr double decay_ = 0.95; // activity retained per conflict

protected:
    void on_start(size_t var_count, const std::vector<clause_t>&) override {
        activity_.assign(var_count + 1, 0.0);
        bump_ = 1.0;
    }

    void on_bump(size_t var) override {
        activity_[var] += bump_;
        if (activity_[var] > 1e100) rescale();
    }

    void on_decay() override {
        // Instead of scaling every activity down each conflict, grow the bump.
        bump_ /= decay_;
        if (bump_ > 1e100) rescale();
    }

    void on_conflict(const clause_t& conflict) override {
        for (const literal& l : conflict) on_bump(l.get_id());
        on_decay();
    }

private:
    void rescale() {
        for (double& a : activity_) a *= 1e-100;
        bump_ *= 1e-100;
    }

public:
    literal decide() override {
        double best = -1.0;
        size_t best_var = 0;
        for (size_t v = 1; v <= var_count(); ++v) {
            if (is_assigned(v)) continue;
            if (activity_[v] > best) { best = activity_[v]; best_var = v; }
        }
        return saved_phase_literal(best_var);
    }
    std::string_view name() const override { return "vsids"; }
};

#endif
