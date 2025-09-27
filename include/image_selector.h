#pragma once

#include <array>
#include <optional>
#include <cstdint>

#include "global.h"

namespace picostation {

struct ImageSelection {
    uint16_t index;
    std::array<char, c_maxFilePathLength + 1> path;
};

// Presents an interactive USB serial menu that allows the user to browse the
// SD card and select a disc image to mount. If no selection is made (for
// example because no host is connected) the function returns std::nullopt.
std::optional<ImageSelection> promptForImageSelection();

}  // namespace picostation

