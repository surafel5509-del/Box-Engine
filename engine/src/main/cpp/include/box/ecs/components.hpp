#pragma once

#include <string>

namespace box::ecs {

/** Human-readable entity name exposed to scene inspectors and JSON files. */
struct NameComponent {
    std::string value;
};

}  // namespace box::ecs
