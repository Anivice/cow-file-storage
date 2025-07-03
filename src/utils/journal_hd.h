#ifndef JOURNAL_HD_H
#define JOURNAL_HD_H

#include <vector>
#include <string>
#include <cstdint>

std::vector<std::string> decoder_jentries(const std::vector<std::vector<uint8_t>> & journal);

#endif //JOURNAL_HD_H
