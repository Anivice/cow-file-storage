#ifndef JOURNAL_H
#define JOURNAL_H

#include <cstring>
#include <memory>
#include <vector>
#include "core/ring_buffer.h"

namespace actions {
    enum Actions : uint8_t {
        ACTION_DONE = 0x08,
        ACTION_MODIFY_BITMAP, // where (64bit) [before]8 [after]8
        ACTION_MODIFY_BLOCK_ATTRIBUTES, // where, before, after
        ACTION_MODIFY_BLOCK_CONTENT, // where, copy-on-write pointer
        ACTION_UPDATE_BITMAP_HASH, // before, after
        ACTION_ALLOCATE_BLOCK,
        ACTION_DEALLOCATE_BLOCK, // where
    };
}

class journaling
{
    std::unique_ptr < ring_buffer > rb;
    std::mutex mtx;

    template < typename Type >
    void push_arg(std::vector<uint8_t> & buffer, const Type val) {
        buffer.resize(buffer.size() + sizeof(Type));
        std::memcpy(buffer.data() + buffer.size() - sizeof(Type), &val, sizeof(Type));
    }

    template < typename... ArgType >
    void push(std::vector<uint8_t> & buffer, const ArgType... args) {
        (push_arg(buffer, args), ...);
    }

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
        std::vector<uint8_t> buffer;
        push(buffer, action);
        push(buffer, args...);
        rb->write(buffer.data(), buffer.size());
    }

    std::vector<uint8_t> export_journaling();
};

#endif //JOURNAL_H
