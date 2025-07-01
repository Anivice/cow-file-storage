#include <atomic>
#include <algorithm>
#include "helper/log.h"
#include "helper/arg_parser.h"
#include "helper/color.h"
#include "helper/get_env.h"

const arg_parser::parameter_vector Arguments = {
    { .name = "help",       .short_name = 'h', .arg_required = false,   .description = "Prints this help message" },
    { .name = "version",    .short_name = 'v', .arg_required = false,   .description = "Prints version" },
    { .name = "config",     .short_name = 'c', .arg_required = true,    .description = "Path to config file" },
    { .name = "verbose",    .short_name = 'V', .arg_required = false,   .description = "Enable verbose output" },
};

void print_help(const std::string & program_name)
{
    uint64_t max_name_len = 0;
    std::vector< std::pair <std::string, std::string>> output;
    const std::string required_str = " arg";
    for (const auto & [name, short_name, arg_required, description] : Arguments)
    {
        std::string name_str =
            (short_name == '\0' ? "" : "-" + std::string(1, short_name))
            += ",--" + name
            += (arg_required ? required_str : "");

        if (max_name_len < name_str.size())
        {
            max_name_len = name_str.size();
        }

        output.emplace_back(name_str, description);
    }

    std::cout << color::color(5,5,5) << program_name << color::no_color() << color::color(0,2,5) << " [options]" << color::no_color()
              << std::endl << color::color(1,2,3) << "options:" << color::no_color() << std::endl;
    for (const auto & [name, description] : output)
    {
        std::cout << "    " << color::color(1,5,4) << name << color::no_color()
                  << std::string(max_name_len + 4 - name.size(), ' ')
                  << color::color(4,5,1) << description << color::no_color() << std::endl;
    }
}

volatile std::atomic_bool running = true;
void int_signal_handler(int) {
    running = false;
}

int main(int argc, char **argv)
{
    try
    {
        arg_parser args(argc, argv, Arguments);
        auto contains = [&args](const std::string & name, std::string & val)->bool
        {
            const auto it = std::ranges::find_if(args,
                [&name](const std::pair<std::string, std::string> & p)->bool{ return p.first == name; });
            if (it != args.end())
            {
                val = it->second;
                return true;
            }

            return false;
        };

        std::string arg_val;
        if (contains("help", arg_val)) // GNU compliance, help must be processed first if it appears and ignore all other arguments
        {
            print_help(argv[0]);
            return EXIT_SUCCESS;
        }

        if (contains("version", arg_val))
        {
            std::cout << color::color(5,5,5) << argv[0] << color::no_color()
                << color::color(0,3,3) << " core version " << color::color(0,5,5) << CORE_VERSION
                << color::color(0,3,3) << " backend version " << color::color(0,5,5) << BACKEND_VERSION
                << color::no_color() << std::endl;
            return EXIT_SUCCESS;
        }

        if (contains("verbose", arg_val))
        {
            // output if and only if verbose mode is not enabled before, prevent duplicated output
            verbose_log("Verbose mode enabled\n");
            debug::verbose = true;
        }

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////

        if (!get_env("VERBOSE").empty())
        {
            const bool before = debug::verbose;
            debug::verbose = true_false_helper(get_env("VERBOSE"));
            if (before && !debug::verbose) debug_log("Verbose mode disabled by environment variable");
        }

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        // server start
        console_log("[main]");
    }
    catch (const std::exception & e)
    {
        error_log(e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
