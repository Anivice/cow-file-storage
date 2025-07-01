#ifndef ARG_PARSER_H
#define ARG_PARSER_H

#include <string>
#include <vector>

class arg_parser
{
private:
    using arg_vector = std::vector < std::pair <std::string, std::string> >;
    arg_vector args;

public:
    struct parameter_t
    {
        std::string name;
        char short_name;
        bool arg_required;
        std::string description;
    };

    using parameter_vector = std::vector < parameter_t >;

    arg_parser(int argc, char ** argv, const parameter_vector & parameters);

    using iterator = arg_vector::iterator;
    using const_iterator = arg_vector::const_iterator;
    iterator begin() { return args.begin(); }
    iterator end() { return args.end(); }
    [[nodiscard]] const_iterator begin() const { return args.begin(); }
    [[nodiscard]] const_iterator end() const { return args.end(); }
};

#endif //ARG_PARSER_H
