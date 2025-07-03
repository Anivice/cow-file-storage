#include <iostream>
#include "helper/cpp_assert.h"
#include "journal_hd.h"
#include "core/journal.h"

template <typename Type>
requires (std::is_integral_v<Type>)
Type read_from(std::vector<uint8_t> & vec)
{
    Type result = 0;
    assert_short(sizeof(Type) <= vec.size());
    std::memcpy(&result, vec.data(), sizeof(Type));
    vec.erase(vec.begin(), vec.begin() + sizeof(Type));
    return result;
}

std::vector<std::string> decoder_jentries(const std::vector<std::vector<uint8_t>> & journal)
{
    std::vector<std::string> result;
    for (auto entry : journal) {
        if (entry.empty()) continue;
        const auto action_code = entry[0];
        entry.erase(entry.begin());
        switch (action_code)
        {
            case actions::ACTION_DONE: result.emplace_back("Done"); break;
            case actions::ACTION_ALLOCATE_BLOCK: result.emplace_back("Allocate Block"); break;
            case actions::ACTION_MODIFY_BITMAP: {
                auto id = read_from<uint64_t>(entry);
                auto before = read_from<uint8_t>(entry);
                auto after = read_from<uint8_t>(entry);
                std::stringstream ss;
                ss << "Allocation bitmap of block " << id << " modified from " << (int)before << " to " << (int)after;
                result.emplace_back(ss.str());
            }
            break;

            case actions::ACTION_UPDATE_BITMAP_HASH: {
                try {
                    auto before = read_from<uint64_t>(entry);
                    auto after = read_from<uint64_t>(entry);
                    std::stringstream ss;
                    ss << "Bitmap checksum modified from " << std::hex << before << " to " << std::hex << after;
                    result.emplace_back(ss.str());
                } catch (const std::exception & e) {
                    std::cerr << e.what() << std::endl;
                }
            }
            break;
                // ACTION_MODIFY_BLOCK_CONTENT,
            case actions::ACTION_MODIFY_BLOCK_ATTRIBUTES: {
                auto id = read_from<uint64_t>(entry);
                auto before = read_from<uint16_t>(entry);
                auto after = read_from<uint16_t>(entry);
                std::stringstream ss;
                ss << "Attribute of block " << id << " modified from " << std::hex << before << " to " << std::hex << after;
                result.emplace_back(ss.str());
            }
            break;

            case actions::ACTION_DEALLOCATE_BLOCK: {
                auto id = read_from<uint64_t>(entry);
                std::stringstream ss;
                ss << "Deallocate block " << id;
                result.emplace_back(ss.str());
            }
            break;

            default:
                result.emplace_back("Unknown");
        }
    }

    return result;
}
