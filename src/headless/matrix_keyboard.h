#pragma once

#include "device.h"

#include <array>
#include <cstdint>
#include <string>

class IOBus;

// A frontend-independent active-low PC-8001/8801 keyboard matrix.
class MatrixKeyboard final : public Device {
public:
    MatrixKeyboard();

    bool Init(IOBus* bus);
    bool SetKey(int row, int bit, bool down);
    bool SetNamedKey(const std::string& name, bool down);
    void ReleaseAll();
    uint8_t Row(int row) const;
    const std::array<uint8_t,16>& Rows() const { return matrix_; }
    void SetRows(const std::array<uint8_t,16>& rows) { matrix_=rows; }

    uint IOCALL In(uint port);

    const Descriptor* IFCALL GetDesc() const override;
    uint IFCALL GetStatusSize() override { return 0; }
    bool IFCALL LoadStatus(const uint8*) override { return true; }
    bool IFCALL SaveStatus(uint8*) override { return true; }

private:
    std::array<uint8_t, 16> matrix_{};

    static const Descriptor descriptor;
    static const InFuncPtr indef[];
};
