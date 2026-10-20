// Path: engine/src/main/cpp/include/box/core/trace_section.hpp
#pragma once

#include <android/trace.h>

namespace box::core {

/** Scoped Android system-trace marker, visible in Perfetto and legacy Systrace. */
class TraceSection final {
public:
    explicit TraceSection(const char* name) noexcept { ATrace_beginSection(name); }
    ~TraceSection() { end(); }

    TraceSection(const TraceSection&) = delete;
    TraceSection& operator=(const TraceSection&) = delete;

    void end() noexcept {
        if (!active_) return;
        ATrace_endSection();
        active_ = false;
    }

private:
    bool active_ = true;
};

}  // namespace box::core
