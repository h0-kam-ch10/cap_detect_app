#include "preprocess.h"

namespace cap {

TensorInput make_empty_input(int source_width, int source_height) {
    TensorInput input;
    input.chw_rgb.assign(320 * 320 * 3, 0);
    input.meta.source_width = source_width;
    input.meta.source_height = source_height;
    return input;
}

}  // namespace cap
