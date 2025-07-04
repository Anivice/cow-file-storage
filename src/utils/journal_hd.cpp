#include <iostream>
#include "journal_hd.h"
#include "core/journal.h"

std::string get_name_by_id(const uint64_t id)
{
    switch (id) {
        case actions::ACTION_ALLOCATE_BLOCK: return "Allocate Block";
        case actions::ACTION_MODIFY_BITMAP: return "Modify Bitmap";
        case actions::ACTION_UPDATE_BITMAP_HASH: return "Bitmap Checksum Modification";
        case actions::ACTION_MODIFY_BLOCK_ATTRIBUTES: return "Modify Block Attributes";
        case actions::ACTION_DEALLOCATE_BLOCK: return "Deallocate Block";
        default: return "";
    }
}

std::vector<std::string> decoder_jentries(const std::vector<entry_t> & journal)
{
    std::vector<std::string> result;
    for (auto entry : journal) {
        switch (entry.operation_name)
        {
            case actions::ACTION_DONE: result.emplace_back(get_name_by_id(entry.operands.done_action.action_name) + " done"); break;
            case actions::ACTION_ALLOCATE_BLOCK: result.emplace_back("Allocate Block"); break;
            case actions::ACTION_MODIFY_BITMAP: {
                std::stringstream ss;
                ss << "Modify bitmap of block "
                    << entry.operands.modify_bitmap.where
                    << " modified from " << entry.operands.modify_bitmap.bit_status_before
                    << " to " << entry.operands.modify_bitmap.bit_status_after;
                result.emplace_back(ss.str());
            }
            break;

            case actions::ACTION_UPDATE_BITMAP_HASH: {
                try {
                    std::stringstream ss;
                    ss << "Bitmap checksum modified from "
                        << std::hex << entry.operands.modify_bitmap_hash.before << " to "
                        << std::hex << entry.operands.modify_bitmap_hash.after;
                    result.emplace_back(ss.str());
                } catch (const std::exception & e) {
                    std::cerr << e.what() << std::endl;
                }
            }
            break;
                // ACTION_MODIFY_BLOCK_CONTENT,
            case actions::ACTION_MODIFY_BLOCK_ATTRIBUTES: {
                std::stringstream ss;
                ss << "Attribute of block " << entry.operands.modify_block_attributes.where
                    << " modified from "
                    << std::hex << entry.operands.modify_block_attributes.bit_status_before << " to "
                    << std::hex << entry.operands.modify_block_attributes.bit_status_after;
                result.emplace_back(ss.str());
            }
            break;

            case actions::ACTION_DEALLOCATE_BLOCK: {
                std::stringstream ss;
                ss << "Deallocate block " << entry.operands.deallocate_block.where << ", block attr = "
                    << std::hex << std::setw(4) << std::setfill('0') << entry.operands.deallocate_block.deallocated_block_status_backup;
                result.emplace_back(ss.str());
            }
            break;

            default:
                result.emplace_back("Unknown");
        }
    }

    return result;
}
