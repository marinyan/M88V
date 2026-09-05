#include "headless_machine.h"
#include "http_server.h"

#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>

namespace {

void PrintUsage() {
    std::cout
        << "M88V - V is for Vibe coding\n"
        << "m88-headless --rom-dir PATH [--n80-rom FILE] [--port 8802] [--token TOKEN]\n"
        << "             [--basic-mode n802|n80v2|n|n88v1|n88v1h|n88v2]\n"
        << "             [--connection-file .m88-headless/connection.json]\n"
        << "m88-headless --self-test\n\n"
        << "The HTTP server binds to 127.0.0.1 only. Default mode: n802 (PC-8001mkII).\n";
}

std::string GenerateToken() {
    std::random_device random;
    std::ostringstream token;
    token << std::hex << std::setfill('0');
    for (int i = 0; i < 16; ++i) token << std::setw(2) << (random() & 0xffu);
    return token.str();
}

bool ParsePort(const std::string& text, uint16_t* port) {
    try {
        size_t consumed = 0;
        const unsigned long value = std::stoul(text, &consumed, 10);
        if (consumed != text.size() || value > 65535) return false;
        *port = static_cast<uint16_t>(value);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

int main(int argc, char** argv) {
    std::string romDirectory;
    std::string n80Rom;
    M88V::BasicMode basicMode = PC8801::Config::N802;
    std::string token;
    std::string connectionFile = ".m88-headless/connection.json";
    uint16_t port = 8802;
    bool selfTest = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto requireValue = [&](const char* option) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << option << " requires a value\n";
                return nullptr;
            }
            return argv[++i];
        };
        if (arg == "--rom-dir") {
            const char* value = requireValue("--rom-dir");
            if (!value) return 2;
            romDirectory = value;
        } else if (arg == "--n80-rom") {
            const char* value = requireValue("--n80-rom");
            if (!value) return 2;
            n80Rom = value;
        } else if (arg == "--basic-mode") {
            const char* value = requireValue("--basic-mode");
            if (!value) return 2;
            if (!M88V::ParseBasicMode(value, &basicMode)) {
                std::cerr << "--basic-mode must be n802, n80v2, n, n88v1, n88v1h, or n88v2\n";
                return 2;
            }
        } else if (arg == "--port") {
            const char* value = requireValue("--port");
            if (!value || !ParsePort(value, &port)) {
                std::cerr << "--port must be 0..65535\n";
                return 2;
            }
        } else if (arg == "--token") {
            const char* value = requireValue("--token");
            if (!value) return 2;
            token = value;
        } else if (arg == "--connection-file") {
            const char* value = requireValue("--connection-file");
            if (!value) return 2;
            connectionFile = value;
        } else if (arg == "--self-test") {
            selfTest = true;
        } else if (arg == "--help" || arg == "-h") {
            PrintUsage();
            return 0;
        } else {
            std::cerr << "unknown argument: " << arg << '\n';
            PrintUsage();
            return 2;
        }
    }

    if (selfTest) {
        std::string error;
        if (!RunHeadlessSelfTest(&error)) {
            std::cerr << "self-test failed: " << error << '\n';
            return 1;
        }
        std::cout << "m88-headless self-test: PASS\n";
        return 0;
    }

    if (romDirectory.empty()) {
        if (const char* fromEnvironment = std::getenv("M88M_ROM_DIR")) romDirectory = fromEnvironment;
    }
    if (romDirectory.empty()) {
        std::cerr << "--rom-dir is required (or set M88M_ROM_DIR)\n";
        return 2;
    }
    if (token.empty()) token = GenerateToken();

    HeadlessMachine machine;
    std::string error;
    if (!machine.Initialize(romDirectory, n80Rom, basicMode, &error)) {
        std::cerr << "initialization failed: " << error << '\n';
        return 1;
    }

    HttpServer server(machine);
    if (!server.Serve(port, token, connectionFile, &error)) {
        std::cerr << "server failed: " << error << '\n';
        return 1;
    }
    return 0;
}
