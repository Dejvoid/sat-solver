#include "tseitin/input_parser.hpp"
#include "dpll/dpll.hpp"
#include <sstream>
#include <fstream>
#include <iostream>


int main(int argc, char** argv) {
    std::ifstream i_fs;
    if (argc >= 2) {
        i_fs = std::ifstream{argv[1]};
    }
    else {
        std::cout << "No input file selected" << std::endl;
    }

    std::istream& input = (i_fs.is_open()) ? i_fs : std::cin;

    std::stringstream buffer;
    buffer << input.rdbuf();
    std::string all_input = buffer.str();

    parser p{all_input};

    std::vector<clause_t> clauses;
    p.nnf2tree()->get_equisat(clauses);

    dpll alg{};
    std::vector<bool> model(p.get_highest_id());
    alg.get_sat(clauses, model);
}