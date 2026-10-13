#pragma once
#include <cstddef>
#include <cstdint>
namespace aliro::access {
struct Assignment { uint8_t type; uint16_t index; bool enabled; };
inline bool allowed(uint16_t issuerIndex, uint8_t endpointType, uint16_t endpointIndex,
                    const Assignment* assignments, size_t count) {
    bool endpointAssigned = false;
    bool endpointEnabled = false;
    bool issuerAssigned = false;
    bool issuerEnabled = false;
    for (size_t i = 0; i < count; ++i) {
        const auto& assignment = assignments[i];
        if (endpointIndex && assignment.index == endpointIndex && assignment.type == endpointType) {
            endpointAssigned = true;
            endpointEnabled |= assignment.enabled;
        }
        if (issuerIndex && assignment.index == issuerIndex && assignment.type == 6) {
            issuerAssigned = true;
            issuerEnabled |= assignment.enabled;
        }
    }
    // A device's explicit assignment takes precedence over its issuer.
    if (endpointAssigned) return endpointEnabled;
    return !issuerAssigned || issuerEnabled;
}
}
