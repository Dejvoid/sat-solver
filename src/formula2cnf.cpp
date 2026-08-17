#include <fstream>
#include <iostream>
#include <sstream>

#include "tseitin/input_parser.hpp"
#include "dimacs/dimacs.hpp"

int main(int argc, char** argv) {
    std::ifstream i_fs;
    std::ofstream o_fs;
    if (argc >= 2) {
        i_fs = std::ifstream{argv[1]};
        if (argc >= 3) {
            o_fs = std::ofstream{argv[2]};
        }
    } else {
        std::cout << "Enter text (Press Ctrl+D or Ctrl+Z to finish):\n";
    }

    std::istream& input  = (i_fs.is_open()) ? i_fs : std::cin;
    std::ostream& output = (o_fs.is_open()) ? o_fs : std::cout;

    std::stringstream buffer;
    buffer << input.rdbuf();
    std::string all_input = buffer.str();

    parser p{all_input};

    auto tree = p.nnf2tree();
    std::vector<clause_t> clauses;
    tree->get_equisat(clauses);

    size_t var_count = p.get_highest_id();

    dimacs::print(var_count, clauses, std::string_view{"No comments"}, output);
}