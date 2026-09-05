#include "matrix_keyboard.h"

#include "device_i.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace {

struct MatrixPosition {
    int row;
    int bit;
};

std::string Normalize(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

const std::unordered_map<std::string, MatrixPosition>& NamedKeys() {
    static const std::unordered_map<std::string, MatrixPosition> keys = {
        {"numpad0", {0, 0}}, {"numpad1", {0, 1}}, {"numpad2", {0, 2}},
        {"numpad3", {0, 3}}, {"numpad4", {0, 4}}, {"numpad5", {0, 5}},
        {"numpad6", {0, 6}}, {"numpad7", {0, 7}}, {"numpad8", {1, 0}},
        {"numpad9", {1, 1}}, {"kp0", {0, 0}}, {"kp1", {0, 1}},
        {"kp2", {0, 2}}, {"kp3", {0, 3}}, {"kp4", {0, 4}},
        {"kp5", {0, 5}}, {"kp6", {0, 6}}, {"kp7", {0, 7}},
        {"kp8", {1, 0}}, {"kp9", {1, 1}},
        {"a", {2, 1}}, {"b", {2, 2}}, {"c", {2, 3}}, {"d", {2, 4}},
        {"e", {2, 5}}, {"f", {2, 6}}, {"g", {2, 7}}, {"h", {3, 0}},
        {"i", {3, 1}}, {"j", {3, 2}}, {"k", {3, 3}}, {"l", {3, 4}},
        {"m", {3, 5}}, {"n", {3, 6}}, {"o", {3, 7}}, {"p", {4, 0}},
        {"q", {4, 1}}, {"r", {4, 2}}, {"s", {4, 3}}, {"t", {4, 4}},
        {"u", {4, 5}}, {"v", {4, 6}}, {"w", {4, 7}}, {"x", {5, 0}},
        {"y", {5, 1}}, {"z", {5, 2}},
        {"0", {6, 0}}, {"1", {6, 1}}, {"2", {6, 2}}, {"3", {6, 3}},
        {"4", {6, 4}}, {"5", {6, 5}}, {"6", {6, 6}}, {"7", {6, 7}},
        {"8", {7, 0}}, {"9", {7, 1}},
        {"up", {8, 1}}, {"right", {8, 2}}, {"backspace", {8, 3}},
        {"grph", {8, 4}}, {"kana", {8, 5}}, {"shift", {8, 6}},
        {"leftshift", {8, 6}}, {"left_shift", {8, 6}},
        {"ctrl", {8, 7}}, {"stop", {9, 0}}, {"f1", {9, 1}},
        {"f2", {9, 2}}, {"f3", {9, 3}}, {"f4", {9, 4}}, {"f5", {9, 5}},
        {"space", {9, 6}}, {"esc", {9, 7}}, {"escape", {9, 7}},
        {"tab", {10, 0}}, {"down", {10, 1}}, {"left", {10, 2}},
        {"help", {10, 3}}, {"copy", {10, 4}}, {"caps", {10, 7}},
        {"enter", {1, 7}}, {"return", {1, 7}},
        {"f6", {12, 0}}, {"f7", {12, 1}}, {"f8", {12, 2}},
        {"f9", {12, 3}}, {"f10", {12, 4}},
    };
    return keys;
}

} // namespace

MatrixKeyboard::MatrixKeyboard() : Device(DEV_ID('K', 'E', 'Y', 'B')) {
    ReleaseAll();
}

bool MatrixKeyboard::Init(IOBus* bus) {
    static const IOBus::Connector connectors[] = {
        {0x00, IOBus::portin, 0}, {0x01, IOBus::portin, 0},
        {0x02, IOBus::portin, 0}, {0x03, IOBus::portin, 0},
        {0x04, IOBus::portin, 0}, {0x05, IOBus::portin, 0},
        {0x06, IOBus::portin, 0}, {0x07, IOBus::portin, 0},
        {0x08, IOBus::portin, 0}, {0x09, IOBus::portin, 0},
        {0x0a, IOBus::portin, 0}, {0x0b, IOBus::portin, 0},
        {0x0c, IOBus::portin, 0}, {0x0d, IOBus::portin, 0},
        {0x0e, IOBus::portin, 0}, {0x0f, IOBus::portin, 0},
        {0, 0, 0},
    };
    return bus->Connect(this, connectors);
}

bool MatrixKeyboard::SetKey(int row, int bit, bool down) {
    if (row < 0 || row >= static_cast<int>(matrix_.size()) || bit < 0 || bit > 7) {
        return false;
    }
    const uint8_t mask = static_cast<uint8_t>(1u << bit);
    if (down) {
        matrix_[row] &= static_cast<uint8_t>(~mask);
    } else {
        matrix_[row] |= mask;
    }
    return true;
}

bool MatrixKeyboard::SetNamedKey(const std::string& name, bool down) {
    const auto it = NamedKeys().find(Normalize(name));
    return it != NamedKeys().end() && SetKey(it->second.row, it->second.bit, down);
}

void MatrixKeyboard::ReleaseAll() {
    matrix_.fill(0xff);
}

uint8_t MatrixKeyboard::Row(int row) const {
    return row >= 0 && row < static_cast<int>(matrix_.size()) ? matrix_[row] : 0xff;
}

uint IOCALL MatrixKeyboard::In(uint port) {
    return matrix_[port & 0x0f];
}

const Device::Descriptor* IFCALL MatrixKeyboard::GetDesc() const {
    return &descriptor;
}

const Device::Descriptor MatrixKeyboard::descriptor = {indef, nullptr};
const Device::InFuncPtr MatrixKeyboard::indef[] = {
    STATIC_CAST(Device::InFuncPtr, &MatrixKeyboard::In),
};
