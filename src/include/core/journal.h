#ifndef JOURNAL_H
#define JOURNAL_H

#include <memory>
#include <map>
#include <vector>

#include "helper/cpp_assert.h"
#include "core/ring_buffer.h"

namespace actions {
    constexpr uint16_t action_start = 0xABCD;
    constexpr uint16_t action_end = 0xFEDC;
    constexpr uint16_t action_bitmap_free = 0x0100;
    constexpr uint16_t action_bitmap_alloc = 0x0001;

    enum Actions : uint16_t {
        ACTION_DONE = 0x08,
        ACTION_MODIFY_BITMAP,
        ACTION_MODIFY_BLOCK_ATTRIBUTES,
        ACTION_MODIFY_BLOCK_CONTENT,
        ACTION_UPDATE_BITMAP_HASH,
        ACTION_ALLOCATE_BLOCK,
        ACTION_DEALLOCATE_BLOCK,
    };
    bool action_done(const std::vector<uint16_t> &);
    typedef bool (*decoder_t)(const std::vector<uint16_t>&);
    const std::map < Actions, uint64_t /* argc */ > action_codec = {
        { ACTION_DONE, 0 },
        { ACTION_MODIFY_BITMAP, 4 /* where */ + 1 /* [before][after] */ },
        { ACTION_MODIFY_BLOCK_ATTRIBUTES, 4 /* where */ + 4 /* before */ + 4 /* after */ },
        { ACTION_MODIFY_BLOCK_CONTENT, 4 /* where */ + 4 /* copy-on-write pointer */ },
        { ACTION_UPDATE_BITMAP_HASH, 4 /* before */ + 4 /* after */ },
        { ACTION_ALLOCATE_BLOCK, 0 },
        { ACTION_DEALLOCATE_BLOCK, 4 /* where */ }
    };
}

class journaling
{
    std::unique_ptr < ring_buffer > rb;
    std::mutex mtx;

public:
    explicit journaling(block_io_t & io)
    {
        cfs_head_t fs_header{};
        auto & head = io.at(0);
        head.get(reinterpret_cast<uint8_t *>(&fs_header), sizeof(fs_header), 0);
        rb = std::make_unique<ring_buffer>(
            io,
            fs_header.static_info.block_size,
            fs_header.static_info.journal_start,
            fs_header.static_info.journal_end);
    }

    template < typename... ArgType >
    void push_action(const actions::Actions action, const ArgType... args)
    {
        std::lock_guard<std::mutex> lock(mtx);
        static_assert(((sizeof(ArgType) >= 2) && ...), "Action is 2 byte aligned");
        rb->write((uint8_t *)&actions::action_start, sizeof(actions::action_start));
        rb->write((uint8_t *)(&action), sizeof(action));
        const std::vector<uint16_t> action_args { static_cast<uint16_t>(args)... };
        try {
            assert_short(action_args.size() == actions::action_codec.at(action));
        } catch (std::out_of_range &) {
            throw runtime_error("No such action");
        }
        for (const auto & arg : action_args) {
            rb->write((uint8_t *)(&arg), sizeof(arg));
        }
        rb->write((uint8_t *)&actions::action_end, sizeof(actions::action_end));
    }

    std::vector<std::vector<uint16_t>> export_journaling();
};

#endif //JOURNAL_H
