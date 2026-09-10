// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026, marinyan
#include "development/environment.h"
#include <iostream>
#include <stdexcept>
#include <string>

void SetEnvironment(const char* name, const char* value) {
#ifdef _WIN32
    const int result = _putenv_s(name, value ? value : "");
#else
    const int result = value ? setenv(name, value, 1) : unsetenv(name);
#endif
    if (result != 0) throw std::runtime_error("Cannot set test environment");
}

class SavedEnvironment {
public:
    explicit SavedEnvironment(const char* name) : name_(name) {
        const char* value = std::getenv(name);
        present_ = value != nullptr;
        if (value) value_ = value;
    }
    ~SavedEnvironment() {
        try { SetEnvironment(name_, present_ ? value_.c_str() : nullptr); }
        catch (...) {}
    }
private:
    const char* name_;
    bool present_;
    std::string value_;
};

void CheckPair(const char* name, const char* legacyName) {
    SavedEnvironment savedNew(name), savedOld(legacyName);
    const auto check = [&](const char* expected) {
        const char* actual = M88V::EnvironmentValue(name, legacyName);
        if ((expected == nullptr) != (actual == nullptr) ||
            (expected && std::string(actual) != expected)) {
            throw std::runtime_error(std::string("Environment precedence mismatch: ") + name);
        }
    };
    SetEnvironment(name, nullptr);
    SetEnvironment(legacyName, nullptr);
    check(nullptr);
    SetEnvironment(legacyName, "legacy value");
    check("legacy value");
    SetEnvironment(name, "current value");
    check("current value");
    SetEnvironment(legacyName, nullptr);
    check("current value");
    SetEnvironment(name, "");
    check(nullptr);
    SetEnvironment(legacyName, "legacy value");
    check("legacy value");
    SetEnvironment(legacyName, "");
    check(nullptr);
}

int main() {
    try {
        CheckPair("M88V_ROM_DIR", "M88M_ROM_DIR");
        CheckPair("M88V_LOAD_BIN", "M88M_LOAD_BIN");
        CheckPair("M88V_LOAD_ADDRESS", "M88M_LOAD_ADDRESS");
        std::cout << "M88V environment names / legacy fallback: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
