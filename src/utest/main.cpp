#if DEBUG
#include <iostream>
#include <iomanip>
#include <thread>
#include <sstream>
#include <cstring>
#include <ranges>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <vector>
#include <algorithm>
#include "test/test.h"
#include "helper/color.h"
#include "helper/cpp_assert.h"

extern void show();

int main(int argc, char *argv[])
{
    auto list_all_tests = []()
    {
        std::cout << color::color(5,2,0) << "Available unit tests are:" << color::no_color() << std::endl;
        int display_len = 0;
        for (const auto & id: test::unit_tests | std::views::keys) {
            if (display_len < static_cast<int>(id.length())) {
                display_len = static_cast<int>(id.length());
            }
        }

        display_len += 4;

        for (const auto & [id, base] : test::unit_tests)
        {
            std::string case_insensitive_test_name = id;
            std::ranges::transform(case_insensitive_test_name, case_insensitive_test_name.begin(), ::tolower);
            std::cout << color::color(5,1,1) << "   " << id
                << std::string(std::max(display_len - static_cast<int>(id.length()), 4), ' ')
                << color::color(5,2,2) << static_cast<test::unit_t *>(base)->name() << color::no_color() << std::endl;
        }
    };

    if (argc == 2 && std::string(argv[1]) == "list")
    {
        list_all_tests();
        return EXIT_SUCCESS;
    }

    int current_test = 1;
    const auto all_tests = test::unit_tests.size();
    int success = 0;
    int failed = 0;
    bool selective_test = argc > 1;
    int selective_count = 0;
    bool is_terminal = false;
    struct stat st{};
    assert_short(fstat(STDOUT_FILENO, &st) != -1);
    if (isatty(STDOUT_FILENO))
    {
        is_terminal = true;
    }

    if (is_terminal) std::cout << "\x1b[?25l"; // hide cursor
    for (const auto & [test_id, unit_address] : test::unit_tests)
    {
        if (argc > 1)
        {
            bool found = false;
            for (int i = 1; i < argc; ++i)
            {
                std::string test_name = argv[i];
                std::string current_test_name = test_id;
                std::ranges::transform(test_name, test_name.begin(), ::tolower);
                std::ranges::transform(current_test_name, current_test_name.begin(), ::tolower);
                if (current_test_name == test_name) {
                    found = true;
                    selective_count++;
                    break;
                }
            }

            if (!found) {
                continue;
            }
        }

        auto * base_unit = static_cast<test::unit_t *>(unit_address);
        int length = 48;

        if (is_terminal)
        {
            winsize w{};
            // If redirecting STDOUT to one file ( col or row == 0, or the previous
            // ioctl call's failed )
            if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) != 0 ||
                (w.ws_col | w.ws_row) == 0)
            {
                std::cerr << "Warning: failed to determine a reasonable terminal size: " << strerror(errno) << std::endl;
            } else {
                length = static_cast<int>(w.ws_col / 2.5);
                // std::cout << length << std::endl;
            }
        }

        std::stringstream output_head;
        output_head << (is_terminal ? "\x1b[2K\r" : "") << color::color(1,5,4) << "TEST\t[" << current_test++ << "/"
                    << (selective_test ? argc - 1 : all_tests) << "]\t"
                    << base_unit->name() << std::string(std::max(length - static_cast<signed int>(base_unit->name().length()), 4), ' ');
        const auto output_str = output_head.str();
        std::cout << output_str << std::flush;

        auto test = [&base_unit](bool & result, std::atomic_bool & finished)->void {
            result = base_unit->run();
            finished = true;
        };

        bool result;
        std::atomic_bool finished = false;
        std::thread worker(test, std::ref(result), std::ref(finished));
        int counter = 0;
        int seconds = 0;
        for (;;)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            counter++;
            if (counter == 100 && is_terminal) {
                std::cout << output_str << "  " << color::color(2,3,4) << ++seconds << " s" << color::no_color() << std::flush;
                counter = 0;
            }

            if (finished)
            {
                if (worker.joinable()) {
                    worker.join();
                }

                break;
            }
        }

        if (result) {
            std::cout << (is_terminal ? output_str : std::string()) << color::color(0,5,0)
                << "  PASSED: " << color::color(0,2,2) << base_unit->success()
                << (is_terminal ? "\r" : "\n") << std::flush;
            success++;
        } else {
            std::cout << output_str << "  " << color::color(5,0,0) << color::bg_color(0,0,1)
                      << "FAILED: " << base_unit->failure() << color::no_color() << "\n";
            failed++;
        }

        std::cout.flush();
    }

    std::cout << "\n\r\n"
              << color::color(1,5,4) << (selective_test ? argc - 1 : all_tests) << " UNIT TESTS\n"
              << color::color(0,5,0) << "    " << success << " PASSED\n"
              << ((!selective_test && failed == 2) || (selective_test && failed == 0)
                  ? color::color(2,0,0) : color::color(5,0,0)) << "    "
              << failed << (selective_test ? " FAILED\n" : " FAILED (2 DESIGNED TO FAIL)\n") << color::no_color();
    std::cout << (is_terminal ? "\x1b[?25h" : "") << std::endl; // show cursor

    if (failed == 2 || (selective_test && failed == 0 && selective_count == (argc - 1)))
    {
        std::cout << color::color(0,5,0)
                  << "Congratulations, all unit tests passed" << (selective_test ? "" : " (with 2 designed to fail)")
                  << color::no_color() << std::endl << std::endl << std::flush;

        if (is_terminal)
        {
            show();
            std::cout << std::flush << std::endl;
        }

        return EXIT_SUCCESS;
    }

    if (selective_test && selective_count != (argc - 1))
    {
        std::cout << color::color(5,0,0)
                  << "ERROR: You intended to run "
                  << argc - 1 << " test(s), but only " << selective_count << " test(s) found\n"
                  << "Attempted argument analyze as follows:"
                  << color::no_color() << std::endl;

        std::vector<std::string> intended_tests_in_lower_cases;
        for (int i =  1; i < argc; ++i)
        {
            std::string test_name = argv[i];
            std::ranges::transform(test_name, test_name.begin(), ::tolower);
            intended_tests_in_lower_cases.push_back(test_name);
        }

        std::vector<std::string> no_duplications;

        // 1. identify duplications
        std::ranges::sort(intended_tests_in_lower_cases);
        std::string last_test_name;
        int duplications = 0;

        auto show_dup = [&]()->void
        {
            if (duplications > 0) {
                std::cout << color::color(5,0,2)
                    << "[DUPLICATIONS]: Test name " << last_test_name << " repeatedly appeared " << duplications + 1 << " times"
                    << color::no_color() << std::endl;
                duplications = 0;
            }
        };

        for (const auto & test_name : intended_tests_in_lower_cases)
        {
            if (last_test_name.empty()) {
                no_duplications.push_back(test_name);
                last_test_name = test_name;
                continue;
            }

            if (last_test_name == test_name) {
                duplications++;
                continue;
            }

            if (last_test_name != test_name)
            {
                if (duplications > 0) {
                    show_dup();
                }

                no_duplications.push_back(test_name);
                last_test_name = test_name;
            }
        }

        show_dup();

        // 2. show not found
        auto can_find = [&](const std::string & test_name)->bool
        {
            for (const auto & id : test::unit_tests | std::views::keys)
            {
                std::string case_insensitive_test_name = id;
                std::ranges::transform(case_insensitive_test_name, case_insensitive_test_name.begin(), ::tolower);
                if (case_insensitive_test_name == test_name) {
                    return true;
                }
            }

            return false;
        };

        for (const auto & test_name : no_duplications)
        {
            if (!can_find(test_name)) {
                std::cout << color::color(5,2,0)
                        << "[NOT FOUND]: Test name " << test_name << " not found in unit tests"
                        << color::no_color() << std::endl;
            }
        }

        list_all_tests();
    }

    return EXIT_FAILURE;
}

#endif
