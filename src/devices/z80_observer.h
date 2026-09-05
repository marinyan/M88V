// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026, marinyan
#pragma once
#include <cstdint>
class Z80C;
// Optional instrumentation, attached/detached only while execution is stopped.
struct Z80Observer {
    virtual ~Z80Observer() = default;
    virtual void Begin(Z80C& cpu, const char* kind) = 0;
    virtual bool End(Z80C& cpu, uint32_t elapsed, uint32_t waits, uint32_t idle, const char* kind) = 0;
    virtual void Write(Z80C& cpu, uint16_t address, uint8_t value) = 0;
};
