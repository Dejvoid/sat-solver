#ifndef CDCL_MINIMIZER_HPP_
#define CDCL_MINIMIZER_HPP_

#include "tseitin/tseitin.hpp"

#include <cstddef>
#include <string_view>
#include <vector>

struct minimize_context {
    const std::vector<std::size_t>& reason_of; // antecedent clause or NO_REASON
    const std::vector<int>& level_of; // decision level where var assigned
    const std::vector<bool>& in_clause; // true if is in the learned clause
    const std::vector<clause_t>& clauses; // all clauses
    std::size_t no_reason;
};

class clause_minimizer {
public:
    virtual ~clause_minimizer() = default;

    /**
     * @brief reduce size of learnt (keep slot 0 - `learning` puts  there asserting literal)
     * 
     * @param learnt (in/out) minimized learnt. index 0 contains asserting literal
     * @param ctx context information
     */
    virtual void minimize(std::vector<literal>& learnt, const minimize_context& ctx) const = 0;

    virtual std::string_view name() const = 0;
};

class no_minimizer : public clause_minimizer {
public:
    void minimize(std::vector<literal>& /*learnt*/, const minimize_context& /*ctx*/) const override {}
    std::string_view name() const override { return "none"; }
};

/**
 * @brief doprs literals of antecedent that are already present in learnt clause or
 * or implied by literals we are keeping
 * 
 */
class local_minimizer : public clause_minimizer {
public:
    void minimize(std::vector<literal>& learnt, const minimize_context& ctx) const override {
        std::size_t j = 1;
        for (std::size_t i = 1; i < learnt.size(); ++i) {
            std::size_t var = learnt[i].get_id();
            std::size_t reason = ctx.reason_of[var];
            if (reason == ctx.no_reason) { // decision literal: cannot remove
                learnt[j++] = learnt[i];
                continue;
            }
            bool redundant = true;
            for (const literal& q : ctx.clauses[reason]) {
                std::size_t qv = q.get_id();
                if (qv == var) continue;
                // if we find literal 'qv' that is NOT covered, 
                // then 'var' is NOT redundant!
                if (!ctx.in_clause[qv] && ctx.level_of[qv] > 0) { redundant = false; break; }
            }
            if (!redundant) learnt[j++] = learnt[i];
        }
        learnt.erase(learnt.begin() + static_cast<std::ptrdiff_t>(j), learnt.end());
    }
    std::string_view name() const override { return "local"; }
};

#endif
