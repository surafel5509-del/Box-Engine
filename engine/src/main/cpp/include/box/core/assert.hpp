#pragma once

#include <cstdlib>

#include "box/core/log.hpp"

#define BOX_ASSERT(condition, message)                                                     \
    do {                                                                                   \
        if (!(condition)) {                                                                \
            BOX_LOG_ERROR("Assertion failed: %s (%s:%d): %s", #condition, __FILE__,       \
                          __LINE__, (message));                                             \
            std::abort();                                                                  \
        }                                                                                  \
    } while (false)
