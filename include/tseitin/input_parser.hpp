#ifndef INPUT_PARSER_HPP_
#define INPUT_PARSER_HPP_

#include "tseitin/tseitin.hpp"
#include <cctype>
#include <iostream>
#include <string_view>
#include <unordered_map>

/*
<formula> ::= `(' `and' <formula> <formula> `)'
          | `(' `or' <formula> <formula> `)'
          | `(' `not' <variable> `)' 
          | <variable>
*/

constexpr char L_PAR = '(';
constexpr char R_PAR = ')';

constexpr std::string_view AND = "and";
constexpr std::string_view OR = "or";
constexpr std::string_view NOT = "not";

class parser {
    std::string_view src;
    size_t cursor = 0;
    size_t id_counter = 0;
    std::unordered_map<std::string_view, size_t> var_ids;

    size_t next_id() { return id_counter++; }

    void skip_whitespace() {
        while(cursor < src.length() && std::isspace(src[cursor])) {
            ++cursor;
        }
    }

    char peek() {
        skip_whitespace();
        if (cursor >= src.length()) return '\0';
        return src[cursor];
    }

    char consume() {
        char c = peek();
        if (c != '\0') cursor ++;
        return c;
    }

    void match(char expected) {
        char c = consume();
        if (c != expected) {
            throw "ASDF";
        }
    }

    std::string_view consume_word() {
        skip_whitespace();
        size_t start = cursor;

        if (cursor < src.length() && std::isalpha(src[cursor])) {
            cursor++;
            while (cursor < src.length() && std::isalnum(src[cursor])) {
                cursor++;
            }
            return src.substr(start, cursor - start);
        }
        throw "ASDF";
    }

    size_t get_or_create_var(std::string_view name) {
        if (var_ids.contains(name)) {
            return var_ids.at(name);
        }

        auto id = next_id();
        var_ids.emplace(name, id);
        return id;
    }

    std::unique_ptr<i_formula> parse_formula() {
        skip_whitespace();

        if (peek() == L_PAR) {
            match(L_PAR);

            auto parse_binary = [&]<typename T>() {
                auto left = parse_formula();
                auto right = parse_formula();
                match(R_PAR);
                return std::make_unique<T>(std::move(left), std::move(right), next_id());
            };

            auto parse_not = [&]() {
                std::string_view var_name = consume_word();
                match(')');
                auto id = get_or_create_var(var_name);
                return std::make_unique<literal>(id, true);
            };

            std::string_view op = consume_word();

            if (op == AND) {
                return parse_binary.template operator()<and_formula>();
            } else if (op == OR) {
                return parse_binary.template operator()<or_formula>();
            } else if (op == NOT) {
                return parse_not();
            } else {
                throw "ASDF";
            }
        } else {
            std::string_view var_name = consume_word();
            auto id = get_or_create_var(var_name);
            return std::make_unique<literal>(id);
        }
    }
public:
    parser(std::string_view text) : src(text) {}
    std::unique_ptr<i_formula> nnf2tree() {
        skip_whitespace();
        if (src.empty()) return {};

        auto root = parse_formula();

        skip_whitespace();

        if (cursor < src.length()) {
            std::cerr << "Unexpected after formula" << std::endl;
        }
        return root;
    }

    const std::unordered_map<std::string_view, size_t>& get_var_ids() const { return var_ids; };
    size_t get_highest_id() const { return id_counter; }
};

#endif