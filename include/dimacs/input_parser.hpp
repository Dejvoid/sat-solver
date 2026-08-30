#ifndef DIMACS_INPUT_PARSER_HPP_
#define DIMACS_INPUT_PARSER_HPP_

#include "tseitin/tseitin.hpp"

#include <cctype>
#include <cstdlib>
#include <istream>
#include <sstream>
#include <string>
#include <vector>

class dimacs_parser {
    std::vector<clause_t> clauses;
    size_t var_count = 0;

public:
    void parse(std::istream& in) {
        clause_t current;
        std::string line;
        while (std::getline(in, line)) {
            size_t i = 0;
            while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) {
                ++i;
            }
            if (i >= line.size()) continue; // blank line
            if (line[i] == 'c') continue; // comment
            if (line[i] == 'p') { // header: p cnf <vars> <clauses>
                std::istringstream header(line);
                std::string p_tok, fmt_tok;
                header >> p_tok >> fmt_tok >> var_count;
                continue;
            }

            std::istringstream values(line);
            long long lit;
            while (values >> lit) {
                if (lit == 0) {
                    clauses.push_back(std::move(current));
                    current.clear();
                } else {
                    size_t id = static_cast<size_t>(std::llabs(lit));
                    current.emplace_back(id, lit < 0);
                    if (id > var_count) var_count = id;
                }
            }
        }
        if (!current.empty()) { // last clause without a trailing 0
            clauses.push_back(std::move(current));
        }
    }

    const std::vector<clause_t>& get_clauses() const { return clauses; }
    size_t get_var_count() const { return var_count; }
};

#endif