#include <iostream>
#include <ctime>
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

        case actions::ACTION_NO_REASON_AVAILABLE: return "No Reason Available";
        case actions::ACTION_NO_SPACE_AVAILABLE: return "Space Depleted";

        default: return "";
    }
}

std::string time_to_hdtime(const time_t unix_timestamp)
{
    char buffer[128]{};
    const tm *local_time_info = localtime(&unix_timestamp);
    if (!local_time_info) return "[NO TIMESTAMP]";
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S %Z", local_time_info);
    return buffer;
}

std::vector<std::string> decoder_jentries(const std::vector<entry_t> & journal)
{
    std::vector<std::string> result;
    for (auto entry : journal) {
        switch (entry.operation_name)
        {
            case actions::ACTION_DONE: result.emplace_back(time_to_hdtime(entry.timestamp) + " " + get_name_by_id(entry.operands.done_action.action_name) + " done"); break;
            case actions::ACTION_ALLOCATE_BLOCK: result.emplace_back(time_to_hdtime(entry.timestamp) + " " + "Allocate Block"); break;
            case actions::ACTION_MODIFY_BITMAP: {
                std::stringstream ss;
                ss << time_to_hdtime(entry.timestamp) << " " << "Modify bitmap of block "
                    << entry.operands.modify_bitmap.where
                    << " from " << entry.operands.modify_bitmap.bit_status_before
                    << " to " << entry.operands.modify_bitmap.bit_status_after;
                result.emplace_back(ss.str());
            }
            break;

            case actions::ACTION_UPDATE_BITMAP_HASH: {
                try {
                    std::stringstream ss;
                    ss << time_to_hdtime(entry.timestamp) << " " << "Bitmap checksum modified from "
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
                ss << time_to_hdtime(entry.timestamp) << " " << "Modify attributes of block " << entry.operands.modify_block_attributes.where
                    << " from "
                    << std::hex << std::setw(4) << std::setfill('0') << entry.operands.modify_block_attributes.bit_status_before << " to "
                    << std::hex << std::setw(4) << std::setfill('0') << entry.operands.modify_block_attributes.bit_status_after;
                result.emplace_back(ss.str());
            }
            break;

            case actions::ACTION_DEALLOCATE_BLOCK: {
                std::stringstream ss;
                ss << time_to_hdtime(entry.timestamp) << " " << "Deallocate block " << entry.operands.deallocate_block.where << ", block attr = "
                    << std::hex << std::setw(4) << std::setfill('0') << entry.operands.deallocate_block.deallocated_block_status_backup;
                result.emplace_back(ss.str());
            }
            break;

            case actions::ACTION_ABORT_ON_ERROR: {
                std::stringstream ss;
                ss << time_to_hdtime(entry.timestamp) << " " << "Action failed, failed action is "
                    << get_name_by_id(entry.operands.failed_action.failed_action_name)
                    << ", reason = " << get_name_by_id(entry.operands.failed_action.reason);
                result.emplace_back(ss.str());
            }
                break;

            default:
                result.emplace_back("Unknown");
        }
    }

    return result;
}
