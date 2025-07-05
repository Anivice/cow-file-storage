#include <iostream>
#include <ctime>
#include "journal_hd.h"
#include "helper/color.h"
#include "core/journal.h"

std::string get_name_by_id(const uint64_t id)
{
    switch (id) {
        case actions::ACTION_NO_REASON_AVAILABLE: return "No Reason Available";
        case actions::ACTION_NO_SPACE_AVAILABLE: return "Space Depleted";

        case actions::ACTION_TRANSACTION_ALLOCATE_BLOCK: return color::color(4,0,5) + "Transaction Allocate Block" + color::no_color();
        case actions::ACTION_TRANSACTION_DEALLOCATE_BLOCK: return color::color(0,4,5) + "Transaction Deallocate Block" + color::no_color();
        case actions::ACTION_REVERT_LAST_TRANSACTION: return color::color(5,2,4) + "Revert Last Transaction" + color::no_color();
        case actions::ACTION_TRANSACTION_MODIFY_BLOCK_ATTRIBUTES: return color::color(5,3,1) + "Transaction Modify Block Attributes" + color::no_color();
        case actions::ACTION_TRANSACTION_ABORT_ON_ERROR: return color::color(5,0,0) + "Transaction Abort On Error" + color::no_color();
        case actions::ACTION_TRANSACTION_DONE: return color::color(0,5,0) + "Transaction Done" + color::no_color();

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
            case actions::ACTION_TRANSACTION_MODIFY_BLOCK_ATTRIBUTES:
            {
                std::stringstream ss;
                ss << time_to_hdtime(entry.timestamp) << ": " << get_name_by_id(entry.operation_name)
                    << " at " << entry.operands.modify_block_attributes.where
                    << " from "
                    << std::hex << std::setw(4) << std::setfill('0') << entry.operands.modify_block_attributes.bit_status_before << " to "
                    << std::hex << std::setw(4) << std::setfill('0') << entry.operands.modify_block_attributes.bit_status_after;
                result.emplace_back(ss.str());
            }
            break;
            case actions::ACTION_TRANSACTION_DEALLOCATE_BLOCK:
            {
                std::stringstream ss;
                ss << time_to_hdtime(entry.timestamp) << ": " << get_name_by_id(entry.operation_name)
                    << " " << entry.operands.deallocate_block_tr.where << ", block attr = "
                    << std::hex << std::setw(4) << std::setfill('0') << entry.operands.deallocate_block_tr.deallocated_block_status_backup
                    << ", COW Block: " << std::dec << entry.operands.deallocate_block_tr.cow_block
                    << ", CRC64: " << std::hex << std::setw(16) << std::setfill('0') << entry.operands.deallocate_block_tr.crc64;
                result.emplace_back(ss.str());
            }
            break;

            case actions::ACTION_TRANSACTION_ABORT_ON_ERROR:
            {
                std::stringstream ss;
                ss << time_to_hdtime(entry.timestamp) << ": " << get_name_by_id(entry.operation_name) << ", failed action is "
                    << get_name_by_id(entry.operands.failed_action.failed_action_name)
                    << ", reason = " << get_name_by_id(entry.operands.failed_action.reason);
                result.emplace_back(ss.str());
            }
            break;

            case actions::ACTION_TRANSACTION_ALLOCATE_BLOCK: {
                std::stringstream ss;
                ss << time_to_hdtime(entry.timestamp) << ": " << get_name_by_id(entry.operation_name) << " at " << entry.operands.operands.operand1;
                result.emplace_back(ss.str());
            }
            break;

            case actions::ACTION_REVERT_LAST_TRANSACTION:
            {
                std::stringstream ss;
                ss << time_to_hdtime(entry.timestamp) << ": " << get_name_by_id(entry.operation_name);
                result.emplace_back(ss.str());
            }
            break;

            case actions::ACTION_TRANSACTION_DONE:
            {
                std::stringstream ss;
                ss << time_to_hdtime(entry.timestamp) << ": " << get_name_by_id(entry.operation_name) << ", action = " << get_name_by_id(entry.operands.done_action.action_name);
                result.emplace_back(ss.str());
            }
            break;

            default: result.emplace_back("Unknown");
        }
    }

    return result;
}
