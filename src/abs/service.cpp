#include "service.h"
#include "helper/log.h"
#include "helper/err_type.h"
#include "helper/cpp_assert.h"

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
    uint64_t new_block_id = UINT64_MAX;
    try {
        new_block_id = block_manager->allocate_block();
    } catch (...) { }

    if (new_block_id != UINT64_MAX) {
        block_manager->journal->push_action(actions::ACTION_TRANSACTION_ALLOCATE_BLOCK, new_block_id);
    }
    else
    {
        try {
            std::vector<std::pair<uint64_t, uint8_t>> cow_blocks; // COW blocks
            for (uint64_t i = 0; i < block_manager->blk_count; i++)
            {
                if (!block_manager->block_allocated(i)) {
                    continue;
                }

                if (auto attr = block_manager->get_attr(i);
                    attr.type == COW_REDUNDANCY_TYPE && !attr.frozen) // if type is COW
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
                return lhs.second < rhs.second;
            }); // sort it

            int lowest_refresh_count = INT_MAX;
            for (const auto & refresh : cow_blocks | std::views::values) {
                if (lowest_refresh_count > refresh) {
                    lowest_refresh_count = refresh;
                }
            }

            for (const auto & [id, refresh] : cow_blocks) {
                if (refresh <= lowest_refresh_count) {
                    block_manager->free_block(id);
                }
            }

            try { // try again
                new_block_id = block_manager->allocate_block();
                block_manager->journal->push_action(actions::ACTION_TRANSACTION_ALLOCATE_BLOCK, new_block_id);
            } catch (...) {
                // WTF???
                warning_log("Filesystem cannot allocate new blocks even after freeing one COW block, internal BUG?");
                throw fs_error::filesystem_space_depleted(""); // fail, still
            }
        } catch (std::exception & e) {
            // error_log("Error when allocating: ", e.what());
            throw fs_error::filesystem_space_depleted(e.what());
        } catch (...) {
            // error_log("Unknown error when allocating");
            throw fs_error::filesystem_space_depleted("");
        }
    }

    block_manager->set_attr(new_block_id, cfs_blk_attr_t{
        .frozen = 0,
        .type = COW_REDUNDANCY_TYPE,
        .type_backup = 0,
        .cow_refresh_count = 0,
        .newly_allocated_thus_no_cow = 1,
        .links = 0
    });

    return new_block_id;
}

void filesystem::unblocked_deallocate_block(const uint64_t data_field_block_id)
{
    assert_short(data_field_block_id != 0);
    // check frozen status
    const auto attr = block_manager->get_attr(data_field_block_id);
    if (attr.frozen) {
        return;
    }

    if (attr.type == COW_REDUNDANCY_TYPE) {
        block_manager->free_block(data_field_block_id); // request COW block free, just free the block
        return;
    }

    auto old_block = block_manager->safe_get_block(data_field_block_id); // get the old block
    try {
        const auto new_block_id = unblocked_allocate_new_block();
        auto new_block = block_manager->safe_get_block(new_block_id); // get a new block

        std::vector<uint8_t> old_block_data; // name a new buffer
        old_block_data.resize(block_manager->block_size); // allocate space
        old_block->get(old_block_data.data(), block_manager->block_size, 0); // copy from old
        ACTION_START(actions::ACTION_TRANSACTION_DEALLOCATE_BLOCK,
            data_field_block_id, cfs_blk_attr_t_to_uint16(attr),
            new_block_id, old_block->crc64());
        const cfs_blk_attr_t old_attr = attr; // get attr of old block
        cfs_blk_attr_t new_attr = old_attr; // copy over
        new_attr.type = COW_REDUNDANCY_TYPE; // reset type to cow
        new_attr.type_backup = old_attr.type; // copy the old block type to the backup area
        new_attr.cow_refresh_count = 3; // 0011, allocation will seek the smallest cow_refresh_count to deallocate before allocate new blocks
        block_manager->set_attr(new_block_id, new_attr); // set the new block as a COW block in attributes
        new_block->update(old_block_data.data(), block_manager->block_size, 0); // to new block
        block_manager->free_block(data_field_block_id); // then, delete the old one
        ACTION_END(actions::ACTION_TRANSACTION_DEALLOCATE_BLOCK);
    } catch (fs_error::filesystem_space_depleted &) {
        // cow cannot be enforced
        ACTION_START(actions::ACTION_TRANSACTION_DEALLOCATE_BLOCK,
            data_field_block_id, cfs_blk_attr_t_to_uint16(block_manager->get_attr(data_field_block_id)), 0, old_block->crc64());
        block_manager->free_block(data_field_block_id); // free the block
        ACTION_END(actions::ACTION_TRANSACTION_DEALLOCATE_BLOCK);
    }
}

uint64_t filesystem::unblocked_read_block(const uint64_t data_field_block_id, void * buff, uint64_t size, const uint64_t offset)
{
    if (offset > block_manager->block_size) return 0;
    if (offset + size > block_manager->block_size) size = block_manager->block_size - offset;
    auto data_block = block_manager->safe_get_block(data_field_block_id);
    data_block->get((uint8_t*)buff, size, offset);
    return size;
}

uint64_t filesystem::unblocked_write_block(const uint64_t data_field_block_id, const void * buff, uint64_t size, const uint64_t offset, bool cow_active)
{
    if (offset > block_manager->block_size) return 0;
    if (offset + size > block_manager->block_size) size = block_manager->block_size - offset;
    auto data_block = block_manager->safe_get_block(data_field_block_id);
    uint64_t new_block = UINT64_MAX;
    auto attr = block_manager->get_attr(data_field_block_id);

    if (attr.frozen)
    {
        error_log("filesystem_frozen_block_protection: ", data_field_block_id);
        throw fs_error::filesystem_frozen_block_protection("");
    }

    // disable COW on new blocks, if COW is being enforced
    if (cow_active && attr.newly_allocated_thus_no_cow) {
        cow_active = false;
        attr.newly_allocated_thus_no_cow = 0;
        block_manager->set_attr(data_field_block_id, attr);
    }

    if (cow_active)
    {
        try {
            new_block = unblocked_allocate_new_block();
        } catch (fs_error::filesystem_space_depleted &) {
            data_block->update(static_cast<const uint8_t *>(buff), size, offset);
            return size;
        }

        ACTION_START(actions::ACTION_TRANSACTION_MODIFY_DATA_FIELD_BLOCK_CONTENT, data_field_block_id, new_block, data_block->crc64(), 0);
        auto new_cow = block_manager->safe_get_block(new_block);
        std::vector<uint8_t> old_block_data;
        old_block_data.resize(block_manager->block_size);
        data_block->get(old_block_data.data(), block_manager->block_size, 0);
        new_cow->update(old_block_data.data(), block_manager->block_size, 0);
        auto old_attr = block_manager->get_attr(data_field_block_id);
        old_attr.type_backup = old_attr.type;
        old_attr.cow_refresh_count = 3;
        old_attr.type = COW_REDUNDANCY_TYPE;
        block_manager->set_attr(new_block, old_attr);
        data_block->update((uint8_t*)buff, size, offset);
        ACTION_END(actions::ACTION_TRANSACTION_MODIFY_DATA_FIELD_BLOCK_CONTENT);
        return size;
    }

    ACTION_START(actions::ACTION_TRANSACTION_MODIFY_DATA_FIELD_BLOCK_CONTENT, data_field_block_id, UINT64_MAX, 0, 0);
    data_block->update((uint8_t*)buff, size, offset);
    ACTION_END(actions::ACTION_TRANSACTION_MODIFY_DATA_FIELD_BLOCK_CONTENT);
    return size;
}

void filesystem::unblocked_delink_block(const uint64_t data_field_block_id)
{
    assert_short(data_field_block_id != 0);
    const auto old_attr = block_manager->get_attr(data_field_block_id);
    auto new_attr = old_attr;
    if (new_attr.links > 0) new_attr.links -= 1;
    ACTION_START(actions::ACTION_TRANSACTION_MODIFY_BLOCK_ATTRIBUTES,
        data_field_block_id, cfs_blk_attr_t_to_uint16(old_attr), cfs_blk_attr_t_to_uint16(new_attr))
    block_manager->set_attr(data_field_block_id, new_attr);
    ACTION_END(actions::ACTION_TRANSACTION_MODIFY_BLOCK_ATTRIBUTES)
}

void filesystem::delink_block(const uint64_t data_field_block_id)
{
    unblocked_delink_block(data_field_block_id);
}

void filesystem::revert_transaction()
{
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
                const auto crc64 = last_transaction.front().operands.deallocate_block_tr.crc64;
                auto deleted_block = block_manager->safe_get_block(deleted_block_id);

                // verify COW block integrity
                if (crc64 == deleted_block->crc64()) {
                    // block_manager->free_block(cow_block_id);
                } else if (crc64 == cow_block->crc64()) {
                    // recover data
                    std::vector<uint8_t> deleted_block_data;
                    deleted_block_data.resize(block_manager->block_size);
                    cow_block->get(deleted_block_data.data(), block_manager->block_size, 0);
                    deleted_block->update(deleted_block_data.data(), block_manager->block_size, 0);
                } else { // content not recoverable, metadata no use now
                    error_log("Abort: COW block data corrupted");
                    return;
                }

                // recover metadata
                deleted_block_attr.type = deleted_block_attr.type_backup;
                block_manager->bitset(deleted_block_id, true);
                block_manager->set_attr(deleted_block_id, deleted_block_attr);

                // recalculate bitmap checksum, see if it is the second bitmap checksum entry
                cfs_head_t header = block_manager->get_header();

                // update the bitmap hash table
                try {
                    block_io->update_runtime_info(header);
                } catch (...) {
                    error_log("Updating bitmap checksum failed!");
                }
            }

            return;
        }

        case actions::ACTION_TRANSACTION_ALLOCATE_BLOCK:
        {
            const uint64_t new_block_id = last_transaction.front().operands.operands.operand1;
            if (block_manager->get_attr(new_block_id).frozen || !block_manager->block_allocated(new_block_id)) {
                return;
            }

            block_manager->free_block(new_block_id);

            return;
        }

        case actions::ACTION_TRANSACTION_MODIFY_BLOCK_ATTRIBUTES:
        {
            const uint64_t block_id = last_transaction.front().operands.modify_block_attributes.where;
            const uint64_t before = last_transaction.front().operands.modify_block_attributes.bit_status_before;
            const uint64_t after = last_transaction.front().operands.modify_block_attributes.bit_status_after;
            const uint16_t now = cfs_blk_attr_t_to_uint16(block_manager->get_attr(block_id));

            if (block_manager->get_attr(block_id).frozen) {
                return;
            }

            if (now != after) {
                warning_log("Abort: Block attributes corrupted, trusting journal");
            }

            block_manager->set_attr(block_id, *(cfs_blk_attr_t*)&before);

            return;
        }

        case actions::ACTION_TRANSACTION_MODIFY_DATA_FIELD_BLOCK_CONTENT:
        {
            const uint64_t where = last_transaction.front().operands.modify_block_content.block_data_field_id;
            const uint64_t cow_block = last_transaction.front().operands.modify_block_content.copy_on_write_pointer;
            const uint64_t crc64 = last_transaction.front().operands.modify_block_content.crc64_old_block;
            const uint64_t is_cow_active = last_transaction.front().operands.modify_block_content.is_modifying_a_frozen_block;

            if (is_cow_active) {
                return;
            }

            auto modified_block = block_manager->safe_get_block(where);

            if (block_manager->get_attr(cow_block).type != COW_REDUNDANCY_TYPE) {
                return;
            }

            auto block = block_manager->safe_get_block(cow_block);
            auto original_crc64 = modified_block->crc64();

            if (crc64 == original_crc64) {
                // content not changed
                // block_manager->free_block(cow_block); // cow block deemed unnecessary
            } else if (crc64 == block->crc64()) {
                // content changed, recoverable
                std::vector<uint8_t> block_data;
                block_data.resize(block_manager->block_size);
                block->get(block_data.data(), block_manager->block_size, 0);

                // recover data
                modified_block->update(block_data.data(), block_manager->block_size, 0);
            } else {
                error_log("Abort: Destination COW no correct data");
                return;
            }

            return;
        }

        default: throw std::logic_error("Abort: Action not implemented");
    }
}

cfs_blk_attr_t filesystem::get_attr(const uint64_t data_field_block_id)
{
    return block_manager->get_attr(data_field_block_id);
}

void filesystem::set_attr(const uint64_t data_field_block_id, const cfs_blk_attr_t attr)
{
    const auto old_attr = block_manager->get_attr(data_field_block_id);
    if (old_attr.frozen) {
        error_log("filesystem_frozen_block_protection: ", data_field_block_id);
        throw fs_error::filesystem_frozen_block_protection("Attempting to modify a frozen block");
        return;
    }

    ACTION_START(actions::ACTION_TRANSACTION_MODIFY_BLOCK_ATTRIBUTES,
        data_field_block_id, cfs_blk_attr_t_to_uint16(old_attr), cfs_blk_attr_t_to_uint16(attr))
    block_manager->set_attr(data_field_block_id, attr);
    ACTION_END(actions::ACTION_TRANSACTION_MODIFY_BLOCK_ATTRIBUTES)
}

void filesystem::freeze_block()
{
    ACTION_START_NO_ARGS(actions::ACTION_FREEZE_BLOCK)
    for (uint64_t i = 1; i < block_manager->blk_count; i++) // 0 not freezable
    {
        if (block_manager->block_allocated(i))
        {
            if (auto attr = block_manager->get_attr(i);
                !attr.frozen && attr.type != COW_REDUNDANCY_TYPE)
            {
                attr.frozen = 1;
                block_manager->set_attr(i, attr);
            }
        }
    }
    ACTION_END(actions::ACTION_FREEZE_BLOCK)
}

void filesystem::clear_frozen_all()
{
    ACTION_START_NO_ARGS(actions::ACTION_CLEAR_FROZEN_BLOCK_ALL)
    for (uint64_t i = 1; i < block_manager->blk_count; i++)
    {
        if (block_manager->block_allocated(i))
        {
            if (auto attr = block_manager->get_attr(i); attr.frozen > 0 && attr.links == 0)
            {
                attr.frozen = 0;
                block_manager->set_attr(i, attr);
                block_manager->free_block(i);
            }
        }
    }
    ACTION_END(actions::ACTION_CLEAR_FROZEN_BLOCK_ALL)
}

void filesystem::sync()
{
    sync_commit_cache();
    block_io->sync();
}

struct statvfs filesystem::fstat()
{
    uint64_t allocated = 0;
    for (uint64_t i = 0; i < block_manager->blk_count; i++)
    {
        if (block_manager->block_allocated(i))
        {
            if (const auto attr = block_manager->get_attr(i);
                attr.type != COW_REDUNDANCY_TYPE)
            {
                allocated++;
            }
        }
    }

    const auto free = block_manager->blk_count - allocated;

    const struct statvfs ret = {
        .f_bsize = block_manager->block_size,
        .f_frsize = block_manager->block_size,
        .f_blocks = block_manager->blk_count,
        .f_bfree = free,
        .f_bavail = free,
        .f_files = 0,
        .f_ffree = 0,
        .f_favail = 0,
        .f_fsid = 0,
        .f_flag = 0,
        .f_namemax = CFS_MAX_FILENAME_LENGTH - 1,
        .f_type = 0x65735546 // FUSE
    };

    return ret;
}

void filesystem::release_all_frozen_blocks()
{
    for (uint64_t i = 1; i < block_manager->blk_count; i++)
    {
        if (block_manager->block_allocated(i)) {
            if (const auto attr = block_manager->get_attr(i); attr.frozen > 0) {
                unblocked_delink_block(i);
            }
        }
    }
    clear_frozen_all();
}

void filesystem::reset()
{
    ACTION_START_NO_ARGS(actions::ACTION_RESET_FROM_SNAPSHOT);
    for (uint64_t i = 1; i < block_manager->blk_count; i++)
    {
        if (const auto attr = block_manager->get_attr(i);
            !attr.frozen && attr.type != COW_REDUNDANCY_TYPE)
        {
            // discard ALL changes
            block_manager->free_block(i);
        }
    }
    ACTION_END(actions::ACTION_RESET_FROM_SNAPSHOT);
}
