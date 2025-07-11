#include "core/journal.h"
#include "helper/cpp_assert.h"

template <typename Type>
requires (std::is_integral_v<Type>)
Type read_from(std::vector<uint8_t> & vec)
{
    Type result = 0;
    const auto read_size = std::min(sizeof(Type),  vec.size());
    std::memcpy(&result, vec.data(), read_size);
    return result;
}

std::vector<entry_t> journaling::export_journaling()
{
    // shadow read buffer
    std::vector<uint8_t> journal_data;
    std::vector<entry_t> ret;
    const auto buffer_len = rb->available_buffer();
    if (buffer_len == 0) return {};
    journal_data.resize(buffer_len);
    rb->read(journal_data.data(), buffer_len, true);
    for (uint64_t i = 0; i <= buffer_len - sizeof (entry_t);)
    {
        const auto entry = (entry_t*)&journal_data[i];
        if (entry->magic == magic) {
            ret.push_back(*entry);
            i += sizeof (entry_t);
        } else {
            i++;
        }
    }

    return ret;
}
