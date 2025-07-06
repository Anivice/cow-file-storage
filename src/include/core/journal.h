#ifndef JOURNAL_H
#define JOURNAL_H

#include <cstring>
#include <memory>
#include <vector>
#include "core/ring_buffer.h"

namespace actions {
    enum Actions : uint64_t {
        ACTION_TRANSACTION_BEGIN = 0xDEADBEEF454E4F44,
        ACTION_TRANSACTION_ALLOCATE_BLOCK, // where
        ACTION_TRANSACTION_DEALLOCATE_BLOCK, // where, old block attr, copy-on-write pointer, block crc64
        ACTION_TRANSACTION_MODIFY_DATA_FIELD_BLOCK_CONTENT /* G */,    // where, copy-on-write pointer, block crc64
        ACTION_TRANSACTION_MODIFY_BLOCK_ATTRIBUTES, // where, old, new
        ACTION_TRANSACTION_END,

        ACTION_TRANSACTION_ABORT_ON_ERROR, // what, reason
        ACTION_TRANSACTION_DONE, // what

        ACTION_REVERT_LAST_TRANSACTION,
        ACTION_FREEZE_BLOCK,
        ACTION_CLEAR_FROZEN_BLOCK_ALL,
        ACTION_CLEAR_FROZEN_BLOCK_BUT_ONE,
    };

    enum ActionErrors : uint64_t {
        ACTION_NO_REASON_AVAILABLE = 0,
        ACTION_NO_SPACE_AVAILABLE = 1,
    };
}

struct entry_t {
    uint64_t magic;
    uint64_t timestamp;
    uint64_t operation_name;
    struct {
        uint64_t _reserved;
    } flags;

    union {
        struct {
            uint64_t _none1;
            uint64_t _none2;
            uint64_t _none3;
            uint64_t _none4;
        } _none_;

        struct {
            uint64_t where;
            uint64_t bit_status_before;
            uint64_t bit_status_after;
            uint64_t _reserved;
        } modify_bitmap;

        struct {
            uint64_t where;
            uint64_t bit_status_before;
            uint64_t bit_status_after;
            uint64_t _reserved;
        } modify_block_attributes;

        struct {
            uint64_t block_data_field_id;
            uint64_t copy_on_write_pointer;
            uint64_t crc64_old_block;
            uint64_t is_modifying_a_frozen_block;
        } modify_block_content;

        struct {
            uint64_t before;
            uint64_t after;
            uint64_t _reserved;
            uint64_t _reserved2;
        } modify_bitmap_hash;

        struct {
            uint64_t where;
            uint64_t deallocated_block_status_backup;
            uint64_t _reserved;
            uint64_t _reserved2;
        } deallocate_block;

        struct {
            uint64_t where;
            uint64_t deallocated_block_status_backup;
            uint64_t cow_block;
            uint64_t crc64;
        } deallocate_block_tr;

        struct {
            uint64_t action_name;
            uint64_t _reserved;
            uint64_t _reserved2;
            uint64_t _reserved3;
        } done_action;

        struct {
            uint64_t failed_action_name;
            uint64_t reason;
            uint64_t _reserved2;
            uint64_t _reserved3;
        } failed_action;

        struct {
            uint64_t operand1;
            uint64_t operand2;
            uint64_t operand3;
            uint64_t operand4;
        } operands;
    } operands;
};
static_assert(sizeof(entry_t) == 64);

class journaling
{
    std::unique_ptr < ring_buffer > rb;
    std::mutex mtx;
    const uint64_t magic = 0xABCDABCDDEADBEEF;

public:
    explicit journaling(block_io_t & io)
    {
        cfs_head_t fs_header{};
        auto head = io.safe_at(0);
        head->get(reinterpret_cast<uint8_t *>(&fs_header), sizeof(fs_header), 0);
        rb = std::make_unique<ring_buffer>(
            io,
            fs_header.static_info.block_size,
            fs_header.static_info.journal_start,
            fs_header.static_info.journal_end);
    }

    void revert_last_action()
    {
        std::lock_guard<std::mutex> lock(mtx);
        rb->retreat_wrote_steps(sizeof(entry_t));
    }

    void push_action(const actions::Actions action,
        const uint64_t operand1 = 0,
        const uint64_t operand2 = 0,
        const uint64_t operand3 = 0,
        const uint64_t operand4 = 0)
    {
        std::lock_guard<std::mutex> lock(mtx);
        const entry_t entry = {
            .magic = magic,
            .timestamp = get_timestamp(),
            .operation_name = action,
            .flags = { },
            .operands = {
                .operands = {
                    .operand1 = operand1,
                    .operand2 = operand2,
                    .operand3 = operand3,
                    .operand4 = operand4
                }
            }
        };
        std::vector<uint8_t> buffer;
        buffer.resize(sizeof(entry));
        std::memcpy(buffer.data(), &entry, sizeof(entry));
        rb->write(buffer.data(), buffer.size());
    }

    std::vector<entry_t> export_journaling();
};

#endif //JOURNAL_H
