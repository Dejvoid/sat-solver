#ifndef DPLL_SOLVER_HPP_
#define DPLL_SOLVER_HPP_

#include "tseitin/tseitin.hpp"
#include <ostream>
#include <vector>

struct i_solver {
    virtual ~i_solver() = default;

    /**
     * @brief 
     * 
     * @param formula 
     * @param var_count 
     * @param model 
     * @return true SAT
     * @return false UNSAT
     */
    virtual bool get_sat(const std::vector<clause_t>& formula, size_t var_count,
                         std::vector<bool>& model) = 0;

    virtual size_t get_decisions() const = 0;
    virtual size_t get_propagations() const = 0;
    virtual size_t get_clause_checks() const = 0;
    virtual const std::string_view name() const = 0;


    virtual void print_extra_stats(std::ostream&) const {}
};

#endif
