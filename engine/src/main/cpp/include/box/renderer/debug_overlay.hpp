// Path: engine/src/main/cpp/include/box/renderer/debug_overlay.hpp
#pragma once

#include <cstddef>
#include <cstdint>

#include "box/core/frame_profiler.hpp"

namespace box::renderer {

struct DebugOverlayStats {
    core::FrameProfileSnapshot frame{};
    core::FrameProfiler* profiler = nullptr;
    std::size_t entityCount = 0U;
    std::uint64_t residentMemoryBytes = 0U;
};

}  // namespace box::renderer
