#ifndef CDCL_HPP_
#define CDCL_HPP_

#include "dpll/dpll_watched.hpp"
#include "dpll/heuristic.hpp"
#include "cdcl/learning.hpp"
#include "cdcl/minimizer.hpp"
#include "cdcl/deletion.hpp"
#include "cdcl/restart.hpp"

#include <cstdint>
#include <memory>
#include <ostream>
#include <string_view>
#include <utility>
#include <vector>

class cdcl_vsids : public vsids {
protected:
    void on_conflict(const clause_t&) override {} // the conflict solving is calling heuristic itself
};

class cdcl : public dpll_watched {
    // --- Implication graph (per variable, indexed by id) ---
    // TODO: rework into vector of custom structs
    // decision level at which the var was assigned
    std::vector<int> level_of;
    // antecedent (how we got here) clause index, or NO_REASON
    std::vector<size_t> reason_of;
    // trail size at the start of decision level
    std::vector<size_t> trail_lim;

    // --- Scratch buffers --- // TODO: move to conflict analyzer
    std::vector<bool> seen;       // per-var marks during conflict analysis
    std::vector<size_t> to_clear;    // vars whose `seen` mark must be reset

    // --- Heuristics ---
    std::unique_ptr<clause_learning> learning = std::make_unique<first_uip_learning>();
    std::unique_ptr<clause_minimizer> minimizer = std::make_unique<local_minimizer>();
    std::unique_ptr<clause_deletion> deletion = std::make_unique<activity_lbd_deletion>();
    std::unique_ptr<restart_policy> restarter = std::make_unique<luby_restart>();

    // --- Statistics ---
    size_t conflicts = 0;
    size_t restarts = 0;
    size_t deleted = 0;

    /**
     * @brief tells current decision level
     * 
     * @return int current decision level
     */
    int decision_level() const { return static_cast<int>(trail_lim.size()); }
    
    /**
     * @brief Store metadata of the assignment.
     * 
     * @param l literal that was assigned
     * @param reason index of the clause that caused the assignment or NO_REASON
     */
    void on_assign(const literal& l, size_t reason) override {
        size_t v = l.get_id();
        level_of[v] = decision_level();
        reason_of[v] = reason;
    }

    /**
     * @brief Undo assignments made after the given decision level.
     * Clears the trail and unassignes the values. Also notifies the heuristics about the changes
     * 
     * @param level decision level to undo to
     */
    void cancel_until(int level) {
        if (decision_level() <= level) return;
        size_t target = trail_lim[static_cast<size_t>(level)];
        while (trail.size() > target) {
            literal l = trail.back();
            size_t v = l.get_id();
            values[v] = assignment::Unassigned;
            reason_of[v] = NO_REASON;
            trail.pop_back();
            heuristic->notify_unassign(l);  // keep the heuristic's phase/assigned state in sync
        }
        trail_lim.resize(static_cast<size_t>(level));
        if (qhead > trail.size()) qhead = trail.size();
    }

    /**
     * @brief Conflict analysis with clause learning. Forwarded to the analyzer (learning) module
     * 
     * @param confl_cl_id id of clause that caused conflict
     * @param learnt_cl (out) learnt clauses
     * @return int backjump level
     */
    int analyze(size_t confl_cl_id, std::vector<literal>& learnt_cl) {
        analyze_context ctx{
            clauses, level_of, reason_of, trail,
            decision_level(), NO_REASON, seen, to_clear, *minimizer,
            [this](size_t v) { heuristic->notify_bump(v); },
            [this](size_t c) { deletion->on_used(c); },
        };
        return learning->analyze(confl_cl_id, learnt_cl, ctx);
    }

    /**
     * @brief Start watching the given clause.
     * Both index params must be valid
     * @param c index of clause to watch
     * @param p0 first literal index to watch
     * @param p1 second literal index to watch
     */
    void watch_clause(size_t c, size_t p0, size_t p1) {
        watched_positions[c] = {p0, p1};
        clauses_watching[lit_index(clauses[c][p0])].push_back(c);
        clauses_watching[lit_index(clauses[c][p1])].push_back(c);
    }

    /**
     * @brief Rebuild the watch lists after restart (some clauses may have been deleted etc)
     * 
     */
    void rebuild_watches() {
        clauses_watching.assign(literal_slots(), {});
        watched_positions.assign(clauses.size(), {0, 0});

        for (size_t c = 0; c < clauses.size(); ++c) {
            const clause_t& cl = clauses[c];
            if (cl.size() >= 2) {  // a unit clause has no second literal to watch
                watch_clause(c, 0, 1);
            }
        }
    }

    /**
     * @brief Add learnt clause and register it within the algorithm (e.g. watching)
     * 
     * @param learnt_cl learnt clause to be added
     * @return size_t index of added clause
     */
    size_t add_learnt(const std::vector<literal>& learnt_cl) {
        size_t c = clauses.size();
        clauses.emplace_back(learnt_cl.begin(), learnt_cl.end());
        deletion->on_learn(c, clauses[c], level_of);
        watched_positions.resize(clauses.size(), {0, 0});
        if (learnt_cl.size() >= 2) watch_clause(c, 0, 1);
        return c;
    }

    /**
     * @brief Delete some clauses based on the deletion heuristics
     * 
     */
    void reduce_db() {
        std::vector<bool> locked(clauses.size(), false);
        for (const literal& l : trail) {
            size_t r = reason_of[l.get_id()];
            if (r != NO_REASON) locked[r] = true;
        }

        // ask the deletion policy which learned clauses to drop
        std::vector<uint8_t> remove(clauses.size(), 0);
        reduce_context ctx{clauses, locked};
        deletion->select(ctx, remove);

        size_t n_remove = 0;
        for (uint8_t r : remove) n_remove += r;
        if (n_remove == 0) return;
        deleted += n_remove;

        // actually remove the clauses
        std::vector<size_t> remap(clauses.size(), NO_REASON);
        std::vector<clause_t> new_clauses;
        new_clauses.reserve(clauses.size() - n_remove);
        for (size_t c = 0; c < clauses.size(); ++c) {
            if (remove[c]) continue;
            remap[c] = new_clauses.size();
            new_clauses.push_back(std::move(clauses[c]));
        }
        clauses.swap(new_clauses);
        deletion->on_compact(remove);

        // restore reasons for the literals
        for (const literal& l : trail) {
            size_t v = l.get_id();
            if (reason_of[v] != NO_REASON) reason_of[v] = remap[reason_of[v]];
        }

        rebuild_watches();
    }

    void cdcl_reset() {
        level_of.assign(var_count + 1, 0);
        reason_of.assign(var_count + 1, NO_REASON);
        seen.assign(var_count + 1, false);
        trail_lim.clear();
        conflicts = restarts = deleted = 0;
    }

    // assumption literals: forced true for the current run and used as the first decisions (before the heuristic)
    std::vector<literal> assumptions;

public:
    void set_minimizer(std::unique_ptr<clause_minimizer> m) { minimizer = std::move(m); }
    void set_learning(std::unique_ptr<clause_learning> l) { learning = std::move(l); }
    void set_deletion(std::unique_ptr<clause_deletion> d) { deletion = std::move(d); }
    void set_restart(std::unique_ptr<restart_policy> r) { restarter = std::move(r); }

    bool get_sat(const std::vector<clause_t>& formula, size_t vc,
                 std::vector<bool>& model) override {
        return get_sat(formula, vc, model, {});
    }

    bool get_sat(const std::vector<clause_t>& formula, size_t vc,
                 std::vector<bool>& model,
                 const std::vector<literal>& assumps) override {
        assumptions = assumps;
        //set_heuristic(std::make_unique<cdcl_vsids>());
        reset(formula, vc); 
        cdcl_reset();
        deletion->start(clauses.size());
        restarter->start();
        if (!init()) return false;

        std::vector<literal> learnt;

        while (true) {
            if (propagate()) { // conflict
                ++conflicts;
                if (decision_level() == 0) return false;   // conflict at root -> UNSAT

                int bt = analyze(conflict_clause, learnt);
                cancel_until(bt);
                size_t cref = add_learnt(learnt);
                push_assignment(learnt[0], learnt.size() == 1 ? NO_REASON : cref);

                heuristic->notify_decay();
                deletion->on_conflict();
                restarter->on_conflict();
            } else { // no conflict
                if (restarter->should_restart()) {
                    cancel_until(0);
                    if (deletion->full()) {
                        reduce_db();
                        deletion->on_reduced();
                    }
                    ++restarts;
                    restarter->on_restart();
                    continue;
                }

                // assumptions first
                literal next(0);
                bool forced = false;
                while (decision_level() < static_cast<int>(assumptions.size())) {
                    const literal p = assumptions[static_cast<size_t>(decision_level())];
                    const assignment v = lit_val(p);
                    if (v == assignment::True) { trail_lim.push_back(trail.size()); continue; }
                    if (v == assignment::False) return false;  // UNSAT under the assumptions
                    next = p; forced = true; break;
                }
                if (!forced) {
                    if (heuristic->all_assigned()) {  // full model -> SAT
                        assign_model(model);
                        return true;
                    }
                    next = heuristic->decide();
                }
                ++decisions;
                trail_lim.push_back(trail.size());
                push_assignment(next);  // also notifies the heuristic
            }
        }
    }

    const std::string_view name() const override { return "cdcl"; }

    void print_extra_stats(std::ostream& os) const override {
        os << "c Conflicts: " << conflicts << std::endl;
        os << "c Restarts: " << restarts << std::endl;
        os << "c Learned clauses (final): " << deletion->num_learnts() << std::endl;
        os << "c Deleted learned clauses: " << deleted << std::endl;
    }
};

#endif
