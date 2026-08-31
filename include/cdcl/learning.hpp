#ifndef CDCL_LEARNING_HPP_
#define CDCL_LEARNING_HPP_

#include "tseitin/tseitin.hpp"
#include "cdcl/minimizer.hpp"

#include <cstddef>
#include <functional>
#include <string_view>
#include <utility>
#include <vector>

/**
 * @brief many things are in cdcl class itself, see there for information
 * 
 */
struct analyze_context {
    const std::vector<clause_t>& clauses;
    const std::vector<int>& level_of;
    const std::vector<std::size_t>& reason_of;
    const std::vector<literal>& trail;
    int decision_level;
    std::size_t no_reason;

    std::vector<bool>& seen;
    std::vector<std::size_t>& to_clear;

    clause_minimizer& minimizer;

    std::function<void(std::size_t)> bump_var;
    std::function<void(std::size_t)> bump_clause;
};

class clause_learning {
public:
    virtual ~clause_learning() = default;

    /**
     * @brief analyze conflict and learn from it. learnt clause will have asserting literal at index 0
     * 
     * @param conflict_clause index of clause causing the conflict
     * @param learnt (out) learnt clause
     * @param ctx context
     * @return int level to backtrack to
     */
    virtual int analyze(std::size_t conflict_clause, std::vector<literal>& learnt,
                        const analyze_context& ctx) const = 0;

    virtual std::string_view name() const = 0;
};


/**
 * @brief stop at first uip
 * 
 */
class first_uip_learning : public clause_learning {
public:
    /**
     * @brief analyze, learn, minimize, return where to backtrack to
     * 
     * @param confl_cl_id 
     * @param learnt_cl 
     * @param ctx 
     * @return int 
     */
    int analyze(std::size_t confl_cl_id, std::vector<literal>& learnt_cl,
                const analyze_context& ctx) const override {
        learnt_cl.assign(1, literal{0});   // slot 0 reserved for the asserting literal
        ctx.to_clear.clear();

        int path = 0;
        literal uip_cand{0};
        std::size_t idx = ctx.trail.size();

        do {
            const clause_t& c = ctx.clauses[confl_cl_id];
            ctx.bump_clause(confl_cl_id);

            for (const literal& q : c) {
                if (q == uip_cand) continue; // skip the literal resolved on
                std::size_t v = q.get_id();
                if (!ctx.seen[v] && ctx.level_of[v] > 0) {
                    ctx.seen[v] = true;
                    ctx.to_clear.push_back(v);

                    ctx.bump_var(v);

                    if (ctx.level_of[v] >= ctx.decision_level) ++path;
                    else learnt_cl.push_back(q); // literal from a lower level: keep
                }
            }

            // take most recently assigned still-marked literal off the trail.
            while (!ctx.seen[ctx.trail[--idx].get_id()]) {}
            uip_cand = ctx.trail[idx];
            ctx.seen[uip_cand.get_id()] = false;
            confl_cl_id = ctx.reason_of[uip_cand.get_id()];
            --path;
        } while (path > 0);

        learnt_cl[0] = ~uip_cand; // first UIP -> asserting literal

        minimize_context mctx{ctx.reason_of, ctx.level_of, ctx.seen, ctx.clauses, ctx.no_reason};
        ctx.minimizer.minimize(learnt_cl, mctx);

        int bt_level;
        if (learnt_cl.size() == 1) {
            bt_level = 0;
        } else {
            std::size_t max_i = 1;
            for (std::size_t k = 2; k < learnt_cl.size(); ++k) {
                if (ctx.level_of[learnt_cl[k].get_id()] > ctx.level_of[learnt_cl[max_i].get_id()])
                    max_i = k;
            }
            std::swap(learnt_cl[1], learnt_cl[max_i]); // second watch = second-highest level
            bt_level = ctx.level_of[learnt_cl[1].get_id()];
        }

        for (std::size_t v : ctx.to_clear) ctx.seen[v] = false;
        return bt_level;
    }

    std::string_view name() const override { return "first-uip"; }
};

#endif
