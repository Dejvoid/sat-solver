#include "tseitin/input_parser.hpp"
#include "dimacs/input_parser.hpp"
#include "dpll/dpll.hpp"
#include "dpll/dpll_adjacency.hpp"
#include "dpll/dpll_watched.hpp"
#include "dpll/dpll_lookahead.hpp"
#include "dpll/dpll_base.hpp"
#include "dpll/heuristic.hpp"
#include "cdcl/cdcl.hpp"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

bool ends_with(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() &&
           s.substr(s.size() - suffix.size()) == suffix;
}

std::unique_ptr<i_solver> make_solver(std::string_view variant) {
    if (variant == "naive") return std::make_unique<dpll>();
    if (variant == "adjacency" || variant == "adj") return std::make_unique<dpll_adjacency>();
    if (variant == "cdcl") return std::make_unique<cdcl>();
    if (variant == "lookahead" || variant == "la" || variant == "lookahead-crh")
        return std::make_unique<dpll_lookahead>(dpll_lookahead::heuristic_kind::CRH);
    if (variant == "lookahead-wbh" || variant == "la-wbh")
        return std::make_unique<dpll_lookahead>(dpll_lookahead::heuristic_kind::WBH);
    return std::make_unique<dpll_watched>();
}

std::unique_ptr<branching_heuristic> make_heuristic(std::string_view name) {
    if (name == "first") return std::make_unique<first_unassigned>();
    if (name == "phase-saving" || name == "phase") return std::make_unique<phase_saving>();
    if (name == "jeroslow-wang" || name == "jw") return std::make_unique<jeroslow_wang>();
    if (name == "vsids") return std::make_unique<vsids>();
    return nullptr;
}

int main(int argc, char** argv) {
    std::string path;
    std::string variant = "watched";
    std::string heuristic_name;

    // Parse arguments: [--variant NAME | -v NAME] [--heuristic NAME | -H NAME] [file]
    for (int a = 1; a < argc; ++a) {
        std::string_view arg = argv[a];
        if ((arg == "--variant" || arg == "-v") && a + 1 < argc) {
            variant = argv[++a];
        } else if (arg.rfind("--variant=", 0) == 0) {
            variant = std::string(arg.substr(std::string_view("--variant=").size()));
        } else if ((arg == "--heuristic" || arg == "-H") && a + 1 < argc) {
            heuristic_name = argv[++a];
        } else if (arg.rfind("--heuristic=", 0) == 0) {
            heuristic_name = std::string(arg.substr(std::string_view("--heuristic=").size()));
        } else if (path.empty()) {
            path = std::string(arg);
        }
    }

    std::ifstream i_fs;
    if (!path.empty()) {
        i_fs = std::ifstream{path};
        if (!i_fs.is_open()) {
            std::cerr << "Could not open input file: " << path << std::endl;
            return 1;
        }
    }

    const bool is_dimacs = ends_with(path, ".cnf");
    std::istream& input = (i_fs.is_open()) ? i_fs : std::cin;

    std::vector<clause_t> clauses;
    size_t var_count = 0;
    std::vector<bool> model;
    std::vector<std::pair<size_t, std::string>> input_vars; // (id, name) for SMT-LIB

    if (is_dimacs) {
        dimacs_parser dp;
        dp.parse(input);
        clauses = dp.get_clauses();
        var_count = dp.get_var_count();
        model.assign(var_count + 1, false);
    } else {
        std::stringstream buffer;
        buffer << input.rdbuf();
        std::string all_input = buffer.str();

        parser p{all_input};
        auto tree = p.nnf2tree();
        if (!tree) {
            std::cerr << "Empty or invalid input formula" << std::endl;
            return 1;
        }
        tree->get_equisat(clauses);
        var_count = p.get_highest_id(); // highest variable id (1-based)
        model.assign(var_count + 1, false);

        for (const auto& [name, id] : p.get_var_ids()) {
            input_vars.emplace_back(id, std::string(name));
        }
        std::sort(input_vars.begin(), input_vars.end());
    }

    std::unique_ptr<i_solver> alg = make_solver(variant);

    if (!heuristic_name.empty()) {
        if (auto* base = dynamic_cast<dpll_base*>(alg.get())) {
            auto h = make_heuristic(heuristic_name);
            if (!h) {
                std::cerr << "Unknown heuristic: " << heuristic_name << std::endl;
                return 1;
            }
            base->set_heuristic(std::move(h));
        } else {
            std::cerr << "Heuristic selection is not supported for variant: "
                      << variant << std::endl;
            return 1;
        }
    }

    const std::clock_t start = std::clock();
    const bool sat = alg->get_sat(clauses, var_count, model);
    const std::clock_t end = std::clock();
    const double cpu_ms =
        1000.0 * static_cast<double>(end - start) / static_cast<double>(CLOCKS_PER_SEC);

    if (sat) {
        std::cout << "SAT" << std::endl;
        std::cout << "v ";
        if (is_dimacs) {
            for (size_t i = 1; i <= var_count; ++i) {
                std::cout << (model[i] ? "" : "-") << i << " ";
            }
            std::cout << "0";
        } else {
            for (const auto& [id, name] : input_vars) {
                std::cout << (model[id] ? "" : "-") << name << " ";
            }
        }
        std::cout << std::endl;
    } else {
        std::cout << "UNSAT" << std::endl;
    }

    std::cout << "c Variant: " << alg->name() << std::endl;
    std::cout << "c Total CPU time: " << cpu_ms << " ms" << std::endl;
    std::cout << "c Decisions: " << alg->get_decisions() << std::endl;
    std::cout << "c Unit propagation steps: " << alg->get_propagations() << std::endl;
    std::cout << "c Clause checks: " << alg->get_clause_checks() << std::endl;
    alg->print_extra_stats(std::cout);

    return 0;
}