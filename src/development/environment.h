// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026, marinyan
#pragma once

#include <cstdlib>

namespace M88V {
// Prefer the M88V name; retain the former name for existing launch scripts.
// Empty values are treated as unset, consistently in both frontends.
inline const char* EnvironmentValue(const char* name, const char* legacyName) {
    const char* value = std::getenv(name);
    if (value && *value) return value;
    value = std::getenv(legacyName);
    return value && *value ? value : nullptr;
}
} // namespace M88V
