#include "service.h"
#include "helper/log.h"
#include "helper/err_type.h"

#define SIMPLE_OPERATION(exp, error)                        \
    try {                                                   \
        exp;                                                \
    }                                                       \
    catch (blk_manager::no_space_available &) {             \
        throw fs_error::filesystem_space_depleted("");      \
    } catch(runtime_error & e) {                            \
        throw error(e.what());                              \
    } catch (std::exception & e) {                          \
        throw fs_error::unknown_error(e.what());            \
    } catch (...) {                                         \
        throw fs_error::unknown_error("");                  \
    }

#define ACTION_START_NO_ARGS(action) block_manager->journal->push_action(action); try {
#define ACTION_START(action, ...) block_manager->journal->push_action(action, __VA_ARGS__); try {
#define ACTION_END(action)                                                                                                                  \
    }                                                                                                                                       \
    catch (fs_error::filesystem_space_depleted&) {                                                                                          \
        block_manager->journal->push_action(actions::ACTION_TRANSACTION_ABORT_ON_ERROR, action, actions::ACTION_NO_SPACE_AVAILABLE);        \
        throw;                                                                                                                              \
    }                                                                                                                                       \
    catch (...) {                                                                                                                           \
        block_manager->journal->push_action(actions::ACTION_TRANSACTION_ABORT_ON_ERROR, action);                                            \
        throw;                                                                                                                              \
    }                                                                                                                                       \
    block_manager->journal->push_action(actions::ACTION_TRANSACTION_DONE, action);

filesystem::filesystem(const char * location)
{
    SIMPLE_OPERATION(basic_io.open(location), fs_error::cannot_open_disk);
    SIMPLE_OPERATION(block_io = std::make_unique<block_io_t>(basic_io), fs_error::filesystem_block_mapping_init_error);
    SIMPLE_OPERATION(block_manager = std::make_unique<blk_manager>(*block_io), fs_error::filesystem_block_manager_init_error);
}

filesystem::~filesystem()
{
    try {
        block_manager.reset();
        block_io.reset();
    } catch (runtime_error & e) {
        error_log("Error when unmounting: ", e.what());
    } catch (std::exception & e) {
        error_log("Error when unmounting: ", e.what());
    } catch (...) {
        error_log("Unknown error when unmounting");
    }
}

uint64_t filesystem::unblocked_allocate_new_block()
{
    uint64_t new_block_id = 0;
    uint8_t hash = 0;
    ACTION_START_NO_ARGS(actions::ACTION_TRANSACTION_ALLOCATE_BLOCK);
    try {
        new_block_id = block_manager->allocate_block();
        hash = block_manager->hash_block(new_block_id);
    }
    catch (blk_manager::no_space_available &)
    {
        std::vector<std::pair<uint64_t, uint8_t>> cow_blocks; // COW blocks
        for (uint64_t i = 0; i < block_manager->blk_count; i++)
        {
            if (auto attr = block_manager->get_attr(i);
                attr.type == COW_REDUNDANCY_TYPE) // if type is COW
            {
                if (attr.cow_refresh_count > 0) {
                    attr.cow_refresh_count--; // count > 0? --
                    block_manager->set_attr(i, attr); // update attributes
                }
                cow_blocks.emplace_back(i, static_cast<uint8_t>(attr.cow_refresh_count)); // put it on the list
            }
        }

        if (cow_blocks.empty()) {
            throw fs_error::filesystem_space_depleted(""); // no COW blocks, no space then
        }

        std::ranges::sort(cow_blocks, [](const std::pair<uint64_t, uint8_t> & lhs, const std::pair<uint64_t, uint8_t> & rhs)->bool {
            return lhs.second > rhs.second;
        }); // sort it

        block_manager->free_block(cow_blocks.front().first); // free the smallest refresher
        try { // try again
            new_block_id = block_manager->allocate_block();
            hash = block_manager->hash_block(new_block_id);
        } catch (...) {
            // WTF???
            warning_log("Filesystem cannot allocate new blocks even after freeing one COW block, internal BUG?");
            throw fs_error::filesystem_space_depleted(""); // fail, still
        }
    } catch (std::exception & e) {
        error_log("Error when allocating: ", e.what());
        throw fs_error::filesystem_space_depleted(e.what());
    } catch (...) {
        error_log("Unknown error when allocating");
        throw fs_error::filesystem_space_depleted("");
    }

    block_manager->set_attr(new_block_id, cfs_blk_attr_t{
        .frozen = 0,
        .type = STORAGE_TYPE,
        .quick_hash = hash,
        .type_backup = 0,
        .cow_refresh_count = 0
    });
    ACTION_END(actions::ACTION_TRANSACTION_ALLOCATE_BLOCK);
    return new_block_id;
}

void filesystem::unblocked_deallocate_block(const uint64_t data_field_block_id)
{
    try {
        const auto new_block_id = unblocked_allocate_new_block();
        ACTION_START(actions::ACTION_TRANSACTION_DEALLOCATE_BLOCK, data_field_block_id,
            cfs_blk_attr_t_to_uint16(block_manager->get_attr(data_field_block_id)), new_block_id);
        auto new_block = block_manager->safe_get_block(new_block_id); // get a new block
        auto old_block = block_manager->safe_get_block(data_field_block_id); // and the old block
        const cfs_blk_attr_t old_attr = block_manager->get_attr(data_field_block_id); // get attr of old block
        cfs_blk_attr_t new_attr = old_attr; // copy over
        new_attr.type = COW_REDUNDANCY_TYPE; // reset type to cow
        new_attr.type_backup = old_attr.type; // copy the old block type to the backup area
        new_attr.cow_refresh_count = 7; // 0111, allocation will seek the smallest cow_refresh_count to deallocate before allocate new blocks
        block_manager->set_attr(new_block_id, new_attr); // set the new block as a COW block in attributes
        std::vector<uint8_t> old_block_data; // name a new buffer
        old_block_data.resize(block_manager->block_size); // allocate space
        old_block->get(old_block_data.data(), block_manager->block_size, 0); // copy from old
        new_block->update(old_block_data.data(), block_manager->block_size, 0); // to new block
        block_manager->free_block(data_field_block_id); // then, delete the old one
        ACTION_END(actions::ACTION_TRANSACTION_DEALLOCATE_BLOCK);
    } catch (fs_error::filesystem_space_depleted &) {
        // cow cannot be enforced
        block_manager->free_block(data_field_block_id); // free the block
    }
}

void filesystem::revert_transaction()
{
    std::lock_guard<std::mutex> lock(mutex);
    std::vector < entry_t > logs = block_manager->journal->export_journaling();
    std::vector < entry_t > last_transaction;
    std::vector < entry_t > backup;
    std::vector < uint64_t > active_transactions;
    for (const auto & entry : logs)
    {
        if (actions::ACTION_TRANSACTION_BEGIN < entry.operation_name && entry.operation_name < actions::ACTION_TRANSACTION_END) {
            active_transactions.emplace_back(entry.operation_name);
        }

        if (entry.operation_name == actions::ACTION_TRANSACTION_DONE) {
            if (!active_transactions.empty() && active_transactions.back() == entry.operands.done_action.action_name) {
                active_transactions.pop_back();
            }
        }

        last_transaction.emplace_back(entry);

        if (active_transactions.empty()) {
            backup = last_transaction;
            last_transaction.clear();
        }
    }

    if (last_transaction.empty()) {
        last_transaction = backup;
    }

    if (last_transaction.empty()) {
        return;
    }

    switch (last_transaction.front().operation_name)
    {
        case actions::ACTION_TRANSACTION_DEALLOCATE_BLOCK: // find cow block, should have two entries
        {
            const uint64_t cow_block_id = last_transaction.front().operands.deallocate_block_tr.cow_block;
            // check bit status (should have before:0 to after:1)
            if (block_manager->get_attr(cow_block_id).type == COW_REDUNDANCY_TYPE)
            {
                debug_log("Deleted block has COW block identified as ", cow_block_id);
                // first, we copy it back to the deleted block
                const auto deleted_block_id = last_transaction.front().operands.deallocate_block.where;
                auto deleted_block_attr = block_manager->get_attr(cow_block_id);
                auto cow_block = block_manager->safe_get_block(cow_block_id);

                // verify COW block integrity
                if (const uint8_t hash = block_manager->hash_block(cow_block_id); hash != deleted_block_attr.quick_hash) {
                    error_log("Abort: COW block data corrupted");
                    return;
                }

                // recover metadata
                deleted_block_attr.type = deleted_block_attr.type_backup;
                block_manager->bitset(deleted_block_id, true);
                block_manager->set_attr(deleted_block_id, deleted_block_attr);

                auto deleted_block = block_manager->safe_get_block(deleted_block_id); // recover data
                std::vector<uint8_t> deleted_block_data;
                deleted_block_data.resize(block_manager->block_size);
                cow_block->get(deleted_block_data.data(), block_manager->block_size, 0);
                deleted_block->update(deleted_block_data.data(), block_manager->block_size, 0);

                // recalculate bitmap checksum, see if it is the second bitmap checksum entry
                std::vector < entry_t > bitmap_checksum_modifications;
                for (const auto & entry : last_transaction)
                {
                    if (entry.operation_name == actions::ACTION_UPDATE_BITMAP_HASH) {
                        bitmap_checksum_modifications.emplace_back(entry);
                    }
                }

                uint64_t old_checksum = 0;
                if (bitmap_checksum_modifications.size() == 2) {
                    old_checksum = bitmap_checksum_modifications.back().operands.modify_bitmap_hash.before;
                }

                cfs_head_t header = block_manager->get_header();
                block_manager->update_bitmap_hash(header);
                if (old_checksum != 0 && old_checksum != header.runtime_info.data_bitmap_checksum) {
                    warning_log("Suspected further data corruption");
                }

                // update the bitmap hash table
                block_manager->journal->push_action(actions::ACTION_UPDATE_BITMAP_HASH,
                    block_manager->get_header().runtime_info.data_bitmap_checksum,
                    header.runtime_info.data_bitmap_checksum);
                try {
                    block_io->update_runtime_info(header);
                } catch (...) {
                    block_manager->journal->push_action(actions::ACTION_ABORT_ON_ERROR, actions::ACTION_UPDATE_BITMAP_HASH);
                    error_log("Updating bitmap checksum failed!");
                }
                block_manager->journal->push_action(actions::ACTION_DONE, actions::ACTION_UPDATE_BITMAP_HASH);
            }

            return;
        }

        default: return;
    }
}
