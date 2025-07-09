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
    uint64_t new_block_id = UINT64_MAX;
    try {
        new_block_id = block_manager->allocate_block();
    } catch (...) { }
    if (new_block_id != UINT64_MAX) {
        ACTION_START(actions::ACTION_TRANSACTION_ALLOCATE_BLOCK, new_block_id);
        ACTION_END(actions::ACTION_TRANSACTION_ALLOCATE_BLOCK);
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

            block_manager->free_block(cow_blocks.front().first); // free the smallest refresher
            try { // try again
                new_block_id = block_manager->allocate_block();
                ACTION_START(actions::ACTION_TRANSACTION_ALLOCATE_BLOCK, new_block_id);
                ACTION_END(actions::ACTION_TRANSACTION_ALLOCATE_BLOCK);
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
        .links = 0
    });

    return new_block_id;
}

void filesystem::unblocked_deallocate_block(const uint64_t data_field_block_id)
{
    // check frozen status
    if (block_manager->get_attr(data_field_block_id).frozen) {
        return;
    }

    auto old_block = block_manager->safe_get_block(data_field_block_id); // and the old block

    try {
        const auto new_block_id = unblocked_allocate_new_block();
        auto new_block = block_manager->safe_get_block(new_block_id); // get a new block

        std::vector<uint8_t> old_block_data; // name a new buffer
        old_block_data.resize(block_manager->block_size); // allocate space
        old_block->get(old_block_data.data(), block_manager->block_size, 0); // copy from old
        ACTION_START(actions::ACTION_TRANSACTION_DEALLOCATE_BLOCK,
            data_field_block_id, cfs_blk_attr_t_to_uint16(block_manager->get_attr(data_field_block_id)),
            new_block_id, old_block->crc64());
        const cfs_blk_attr_t old_attr = block_manager->get_attr(data_field_block_id); // get attr of old block
        cfs_blk_attr_t new_attr = old_attr; // copy over
        new_attr.type = COW_REDUNDANCY_TYPE; // reset type to cow
        new_attr.type_backup = old_attr.type; // copy the old block type to the backup area
        new_attr.cow_refresh_count = 7; // 0111, allocation will seek the smallest cow_refresh_count to deallocate before allocate new blocks
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

uint64_t filesystem::unblocked_write_block(const uint64_t data_field_block_id, const void * buff, uint64_t size, const uint64_t offset)
{
    if (offset > block_manager->block_size) return 0;
    if (offset + size > block_manager->block_size) size = block_manager->block_size - offset;
    auto data_block = block_manager->safe_get_block(data_field_block_id);
    uint64_t new_block = UINT64_MAX;

    if (block_manager->get_attr(data_field_block_id).frozen)
    {
        new_block = unblocked_allocate_new_block();
        ACTION_START(actions::ACTION_TRANSACTION_MODIFY_DATA_FIELD_BLOCK_CONTENT, data_field_block_id, new_block, data_block->crc64(), 1);
        auto new_cow = block_manager->safe_get_block(new_block);
        std::vector<uint8_t> old_block_data;
        old_block_data.resize(block_manager->block_size);
        data_block->get(old_block_data.data(), block_manager->block_size, 0);
        new_cow->update(old_block_data.data(), block_manager->block_size, 0);
        const auto old_attr = block_manager->get_attr(data_field_block_id);
        block_manager->set_attr(new_block, old_attr);
        new_cow->update((uint8_t*)buff, size, offset);
        ACTION_END(actions::ACTION_TRANSACTION_MODIFY_DATA_FIELD_BLOCK_CONTENT);
        return size;
    }
    else
    {
        try {
            new_block = unblocked_allocate_new_block();
        } catch (fs_error::filesystem_space_depleted &) {
            data_block->update((uint8_t*)buff, size, offset);
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
        old_attr.cow_refresh_count = 7;
        old_attr.type = COW_REDUNDANCY_TYPE;
        block_manager->set_attr(new_block, old_attr);
        data_block->update((uint8_t*)buff, size, offset);
        ACTION_END(actions::ACTION_TRANSACTION_MODIFY_DATA_FIELD_BLOCK_CONTENT);
        return size;
    }
}

void filesystem::delink_block(uint64_t data_field_block_id)
{
    std::lock_guard<std::mutex> lock(mutex);
    const auto old_attr = block_manager->get_attr(data_field_block_id);
    auto new_attr = old_attr;
    if (new_attr.links > 0) new_attr.links -= 1;
    ACTION_START(actions::ACTION_TRANSACTION_MODIFY_BLOCK_ATTRIBUTES,
        data_field_block_id, cfs_blk_attr_t_to_uint16(old_attr), cfs_blk_attr_t_to_uint16(new_attr))
    block_manager->set_attr(data_field_block_id, new_attr);
    ACTION_END(actions::ACTION_TRANSACTION_MODIFY_BLOCK_ATTRIBUTES)
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
                block_manager->update_bitmap_hash(header);

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
    std::lock_guard<std::mutex> lock(mutex);
    return block_manager->get_attr(data_field_block_id);
}

void filesystem::set_attr(const uint64_t data_field_block_id, const cfs_blk_attr_t attr)
{
    std::lock_guard<std::mutex> lock(mutex);
    const auto old_attr = block_manager->get_attr(data_field_block_id);
    if (old_attr.frozen) {
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
    std::lock_guard<std::mutex> lock(mutex);
    ACTION_START_NO_ARGS(actions::ACTION_FREEZE_BLOCK)
    for (uint64_t i = 0; i < block_manager->blk_count; i++)
    {
        if (block_manager->block_allocated(i))
        {
            if (auto attr = block_manager->get_attr(i);
                attr.frozen < 3 && attr.type != COW_REDUNDANCY_TYPE)
            {
                attr.frozen++;
                block_manager->set_attr(i, attr);
            }
        }
    }
    ACTION_END(actions::ACTION_FREEZE_BLOCK)
}

void filesystem::clear_frozen_but_1()
{
    std::lock_guard<std::mutex> lock(mutex);
    ACTION_START_NO_ARGS(actions::ACTION_CLEAR_FROZEN_BLOCK_BUT_ONE)
    for (uint64_t i = 0; i < block_manager->blk_count; i++)
    {
        if (block_manager->block_allocated(i))
        {
            if (auto attr = block_manager->get_attr(i); attr.frozen > 1 && attr.links == 0)
            {
                attr.frozen = 0;
                block_manager->set_attr(i, attr);
                block_manager->free_block(i);
            }
        }
    }
    ACTION_END(actions::ACTION_CLEAR_FROZEN_BLOCK_BUT_ONE)
}

void filesystem::clear_frozen_all()
{
    std::lock_guard<std::mutex> lock(mutex);
    ACTION_START_NO_ARGS(actions::ACTION_CLEAR_FROZEN_BLOCK_ALL)
    for (uint64_t i = 0; i < block_manager->blk_count; i++)
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
