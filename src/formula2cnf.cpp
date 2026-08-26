#include <fstream>
#include <iostream>
#include <sstream>
#include <unistd.h>

#include "tseitin/input_parser.hpp"
#include "dimacs/dimacs.hpp"

int main(int argc, char** argv) {
    bool equivalences = true;
    std::vector<const char*> args;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "-i" || arg == "--implications") {
            equivalences = false;
        } else if (arg == "-e" || arg == "--equivalences") {
            equivalences = true;
        } else {
            args.push_back(argv[i]);
        }
    }

    std::ifstream i_fs;
    std::ofstream o_fs;
    if (!args.empty()) {
        i_fs = std::ifstream{args[0]};
        if (!i_fs.is_open()) {
            std::cerr << "Error: cannot open input file '" << args[0] << "'" << std::endl;
            return 1;
        }
        if (args.size() >= 2) {
            o_fs = std::ofstream{args[1]};
            if (!o_fs.is_open()) {
                std::cerr << "Error: cannot open output file '" << args[1] << "'" << std::endl;
                return 1;
            }
        }
    } else {
        std::cerr << "Enter text (Press Ctrl+D to finish):\n";
    }

    std::istream& input  = (i_fs.is_open()) ? i_fs : std::cin;
    std::ostream& output = (o_fs.is_open()) ? o_fs : std::cout;

    std::stringstream buffer;
    buffer << input.rdbuf();
    std::string all_input = buffer.str();

    parser p{all_input};

    std::unique_ptr<i_formula> tree;
    try {
        tree = p.nnf2tree();
    } catch (...) {
        std::cerr << "Error: failed to parse formula" << std::endl;
        return 1;
    }
    if (!tree) {
        std::cerr << "Error: empty or invalid formula" << std::endl;
        return 1;
    }

    std::vector<clause_t> clauses;
    tree->get_equisat(clauses, equivalences);

    size_t var_count = p.get_highest_id(); // vars are 1..n
    size_t root_id = tree->get_id();

    std::string comments = "variable mapping (CNF index : input variable / auxiliary gate):\n";
    comments += "encoding: ";
    comments += equivalences ? "equivalences\n" : "left-to-right implications\n";
    for (size_t i = 1; i <= var_count; ++i) {
        std::string_view name = p.get_var_name(i);
        comments += std::to_string(i) + " : " +
                    (name.empty() ? std::string("(auxiliary gate)") : std::string(name));
        if (i == root_id) comments += " [root]";
        comments += "\n";
    }

    dimacs::print(var_count, clauses, comments, output);
}