#include "core/journal.h"

std::vector<std::vector<uint16_t>> journaling::export_journaling()
{
    // shadow read buffer
    std::vector<uint16_t> journal_data;
    const auto buffer_len = rb->available_buffer();
    journal_data.resize(buffer_len);
    rb->read(reinterpret_cast<uint8_t *>(journal_data.data()), buffer_len, true);
    std::vector<std::vector<uint16_t>> journal;
    std::vector<uint16_t> entry;
    bool entry_start = false;
    for (const auto & c : journal_data)
    {
        if (c == actions::action_start) {
            entry_start = true;
            continue;
        }

        if (c == actions::action_end) {
            if (!entry.empty()) {
                journal.push_back(entry);
                entry.clear();
            }
            entry_start = false;
            continue;
        }

        if (entry_start) {
            entry.push_back(c);
        }
    }

    return journal;
}
