#ifndef DIMACS_HPP_
#define DIMACS_HPP_

#include "tseitin/tseitin.hpp"
#include <iostream>
#include <vector>

inline std::ostream& operator<<(std::ostream& os, const literal& l) {
    os << (l.get_negation() ? "-" : "") << l.get_id();
    return os;
}

class dimacs {
public:
    static void print(size_t var_count, const std::vector<clause_t>& clauses, std::string_view comments, std::ostream& output) {
        output
        << "c" << std::endl
        << "c " << comments << std::endl
        << "c" << std::endl
        << "c" << std::endl
        << "p cnf " << var_count << " " << clauses.size() << std::endl;
        for (auto&& clause : clauses) {
            for (auto&& literal : clause) {
                output << literal << " ";
            }
            output << "0 " << std::endl;
        }
    }
};

#endif