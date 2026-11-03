// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "protocol.hpp"

namespace dl {
enum HostUsageMode { NoHost = 0, AsioBuffers = 1 };

struct HostUsage {
    uint32_t inputs = 0, outputs = 0;
    HostUsageMode mode = NoHost;
};

inline HostUsage readHostUsage(Shared *shared) {
    if (!shared || !valid(shared) || read32(&shared->clientPid) == 0) {
        return {};
    }
    return {
        (uint32_t)read32(&shared->bufferInputMask), (uint32_t)read32(&shared->bufferOutputMask), AsioBuffers};
}
} // namespace dl
