#pragma once

#include <cstdint>
#include <vector>

namespace cap {

struct LetterboxMeta {
    float scale = 1.0f;
    int pad_x = 0;
    int pad_y = 0;
    int source_width = 0;
    int source_height = 0;
};

struct TensorInput {
    std::vector<std::uint8_t> chw_rgb;
    LetterboxMeta meta;
};

TensorInput make_empty_input(int source_width, int source_height);

}  // namespace cap
