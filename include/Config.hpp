#pragma once

#include <cstddef>

namespace config {
    inline constexpr std::size_t MAX_HISTORY_SIZE     = 1000;
    inline constexpr std::size_t MAX_CAT_FILE_SIZE    = 10 * 1024 * 1024; // 10 MiB
    inline constexpr std::size_t IO_CHUNK_SIZE        = 64 * 1024;        // 64 KiB
    inline constexpr int         DIR_SLOT_COUNT       = 10;
    inline constexpr int         PAGE_BUFFER_MULT     = 2;
}
