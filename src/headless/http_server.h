#pragma once

#include "headless_machine.h"

#include <cstdint>
#include <string>

class HttpServer {
public:
    explicit HttpServer(HeadlessMachine& machine) : machine_(machine) {}

    bool Serve(uint16_t requestedPort, const std::string& token,
               const std::string& connectionFile, std::string* error);

private:
    HeadlessMachine& machine_;
};

bool RunHeadlessSelfTest(std::string* error);
