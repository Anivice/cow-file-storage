#include "helper/log.h"
#include "service.h"

struct block_mapping_tail_t {
    uint64_t inode_level_pointers; // i.e., level 1 pointers
    uint64_t level2_pointers; // level 2 pointers reside in level 1 pointed blocks, whose number is given by inode_level_pointers
    uint64_t level3_pointers; // level 3 pointers reside in level 2 pointed blocks, whose number is given by level2_pointers
    uint64_t last_level2_pointer_block_has_this_many_pointers;
    uint64_t last_level3_pointer_block_has_this_many_pointers;
};

block_mapping_tail_t pointer_mapping_linear_to_abstracted(
    const uint64_t file_length,
    const uint64_t inode_level1_pointers,
    const uint64_t level2_pointers_per_block,
    const uint64_t block_size)
{
    const uint64_t max_file_size = inode_level1_pointers * level2_pointers_per_block * level2_pointers_per_block * block_size;

    if (file_length > max_file_size) {
        throw fs_error::filesystem_space_depleted("Exceeding max file size");
    }

    const uint64_t required_blocks = ceil_div(file_length, block_size); // i.e., level 3 pointers
    const uint64_t required_level2_pointers = ceil_div(required_blocks, level2_pointers_per_block);
    const uint64_t required_level1_pointers = ceil_div(required_level2_pointers, level2_pointers_per_block);

    return {
        .inode_level_pointers = required_level1_pointers,
        .level2_pointers = required_level2_pointers,
        .level3_pointers = required_blocks,
        .last_level2_pointer_block_has_this_many_pointers = required_level2_pointers % level2_pointers_per_block,
        .last_level3_pointer_block_has_this_many_pointers = required_blocks % level2_pointers_per_block,
    };
}

int main(int argc, char *argv[])
{
    try {
        // filesystem fs("./file");
        auto result = pointer_mapping_linear_to_abstracted(1024*1024*1024+1024*512*793,
            (4096 - 272) / 8,
            4096 / 8,
            4096);
        int c = 0;
    } catch (const std::exception &e) {
        error_log(e.what());
    }
}
