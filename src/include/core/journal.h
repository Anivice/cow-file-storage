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

    enum Actions : uint16_t { ACTION_DONE = 0x08 };
    bool action_done(const std::vector<uint16_t> &);
    typedef bool (*decoder_t)(const std::vector<uint16_t>&);
    const std::map < actions::Actions, std::pair < decoder_t /* func */, uint64_t /* argc */ > > action_codec = {
        { actions::ACTION_DONE, { actions::action_done, 0 } }
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
            assert_short(action_args.size() == actions::action_codec.at(action).second);
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
