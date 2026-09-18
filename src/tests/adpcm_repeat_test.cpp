#include "headers.h"
#include "opna.h"

#include <algorithm>
#include <array>
#include <iostream>

namespace {

constexpr unsigned Eos = 0x04;
constexpr unsigned Playing = 0x20;
int failures = 0;

void Check(bool result, const char* message) {
    if (!result) {
        std::cerr << message << '\n';
        ++failures;
    }
}

class TestOPNA : public FM::OPNA {
public:
    // Drive individual decoder nibbles so the boundary assertions do not
    // depend on host sample rate or mixer interpolation.
    void Decode(unsigned count) {
        while (count--) ReadRAMN();
    }
    bool irq = false;

private:
    void Intr(bool level) override { irq = level; }
};

void Start(TestOPNA& chip, bool repeat, bool eightBitMemory, bool maskEos = false) {
    chip.Reset();
    std::fill_n(chip.GetADPCMBuffer(), 0x40000, 0x17);
    chip.SetReg(0x101, eightBitMemory ? 0xc2 : 0xc0);
    // Nonzero start address; start = stop describes one address-register unit.
    chip.SetReg(0x102, 1);
    chip.SetReg(0x103, 0);
    chip.SetReg(0x104, 1);
    chip.SetReg(0x105, 0);
    chip.SetReg(0x109, 0xff);
    chip.SetReg(0x10a, 0xff);
    chip.SetReg(0x10b, 0xff);
    chip.SetReg(0x110, maskEos ? Eos : 0);
    chip.SetReg(0x100, repeat ? 0xb0 : 0xa0);
}

void TestBoundaries(TestOPNA& chip, bool eightBitMemory) {
    const unsigned nibbles = eightBitMemory ? 64 : 8;
    Start(chip, false, eightBitMemory);
    chip.Decode(nibbles - 1);
    Check((chip.ReadStatusEx() & (Eos | Playing)) == Playing,
          "one-shot: EOS before stop address");
    chip.Decode(1);
    Check((chip.ReadStatusEx() & (Eos | Playing)) == Eos && chip.irq,
          "one-shot: stop must raise EOS/IRQ and end playback");

    Start(chip, true, eightBitMemory);
    for (int loop = 0; loop < 7; ++loop) {
        chip.Decode(nibbles - 1);
        Check((chip.ReadStatusEx() & (Eos | Playing)) == Playing && !chip.irq,
              "repeat: EOS/IRQ before stop address");
        chip.Decode(1);
        Check((chip.ReadStatusEx() & (Eos | Playing)) == (Eos | Playing) && chip.irq,
              "repeat: each stop must raise EOS/IRQ without ending playback");
        Check((chip.ReadStatusEx() & Eos) != 0,
              "repeat: reading status must not acknowledge EOS");
        chip.SetReg(0x110, 0x80);
        Check((chip.ReadStatusEx() & (Eos | Playing)) == Playing && !chip.irq,
              "repeat: flag reset must clear EOS/IRQ without ending playback");
    }

    Start(chip, true, eightBitMemory, true);
    chip.Decode(nibbles * 2);
    Check((chip.ReadStatusEx() & (Eos | Playing)) == Playing && !chip.irq,
          "repeat: masked EOS must not raise status or IRQ");
    chip.SetReg(0x110, 0);
    Check((chip.ReadStatusEx() & Eos) == 0,
          "repeat: masked EOS must not become visible when unmasked");
    chip.Decode(nibbles);
    Check((chip.ReadStatusEx() & (Eos | Playing)) == (Eos | Playing) && chip.irq,
          "repeat: EOS must resume at the next unmasked boundary");
    chip.SetReg(0x100, 1);
    Check((chip.ReadStatusEx() & Playing) == 0,
          "repeat: explicit stop must end playback");
}

} // namespace

int main() {
    TestOPNA chip;
    if (!chip.Init(7987200, 44100)) {
        std::cerr << "OPNA initialization failed\n";
        return 1;
    }
    TestBoundaries(chip, false);
    TestBoundaries(chip, true);

    // Exercise the public mixer path as well as exact decoder boundaries.
    Start(chip, true, true);
    for (int loop = 0; loop < 7; ++loop) {
        std::array<FM::Sample, 1024> samples{};
        chip.Mix(samples.data(), static_cast<int>(samples.size() / 2));
        Check((chip.ReadStatusEx() & (Eos | Playing)) == (Eos | Playing),
              "mixer: repeated EOS must remain observable during playback");
        chip.SetReg(0x110, 0x80);
    }

    if (failures) {
        std::cerr << failures << " ADPCM repeat checks failed\n";
        return 1;
    }
    std::cout << "ADPCM repeat EOS: decoder boundaries, seven loops, masks, IRQ and mixer passed\n";
    return 0;
}
