#include "headers.h"
#include "device.h"
#include "memmgr.h"
#include "Z80c.h"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <iostream>

namespace {

class TestMachine {
public:
    TestMachine() : cpu(DEV_ID('T', 'E', 'S', 'T')) {}

    bool Init() {
        MemoryPage* readPages = nullptr;
        MemoryPage* writePages = nullptr;
        cpu.GetPages(&readPages, &writePages);

        if (!memory.Init(0x10000, readPages, writePages) || !bus.Init(0x100)) {
            return false;
        }

        const int owner = memory.Connect(this);
        if (owner < 0 ||
            !memory.AllocR(owner, 0, ram.size(), ram.data()) ||
            !memory.AllocW(owner, 0, ram.size(), ram.data())) {
            return false;
        }

        return cpu.Init(&memory, &bus, 0);
    }

    int RunOne(std::initializer_list<uint8> code,
               std::initializer_list<std::pair<uint, int>> pageWaits) {
        std::fill(ram.begin(), ram.end(), 0);
        std::copy(code.begin(), code.end(), ram.begin());

        int* waits = cpu.GetWaits();
        std::fill(waits, waits + kPageCount, 0);
        for (const auto& [address, wait] : pageWaits) {
            waits[(address & 0xffff) >> MemoryManager::pagebits] = wait;
        }

        cpu.Reset();
        cpu.SetPC(0);
        return cpu.ExecOne();
    }

    int* Waits() { return cpu.GetWaits(); }

private:
    static constexpr int kPageCount = 0x10000 >> MemoryManager::pagebits;

    Z80C cpu;
    MemoryManager memory;
    IOBus bus;
    alignas(2) std::array<uint8, 0x10000> ram{};
};

bool ExpectDelta(TestMachine& machine,
                 const char* label,
                 std::initializer_list<uint8> code,
                 std::initializer_list<std::pair<uint, int>> pageWaits,
                 int expectedDelta) {
    const int withoutWait = machine.RunOne(code, {});
    const int withWait = machine.RunOne(code, pageWaits);
    const int actualDelta = withWait - withoutWait;
    if (actualDelta == expectedDelta) {
        return true;
    }

    std::cerr << label << ": expected wait delta " << expectedDelta
              << ", got " << actualDelta << " (base=" << withoutWait
              << ", waited=" << withWait << ")\n";
    return false;
}

}  // namespace

int main() {
    TestMachine machine;
    if (!machine.Init()) {
        std::cerr << "failed to initialize Z80 wait-state test machine\n";
        return 1;
    }
    if (!machine.Waits()) {
        std::cerr << "portable Z80 core did not expose a wait table\n";
        return 1;
    }

    bool ok = true;
    ok &= ExpectDelta(machine, "opcode fetch", {0x00}, {{0x0000, 3}}, 3);
    ok &= ExpectDelta(machine, "opcode and immediate fetches",
                      {0x01, 0x34, 0x12}, {{0x0000, 2}}, 6);
    ok &= ExpectDelta(machine, "byte read",
                      {0x3a, 0x00, 0x40}, {{0x4000, 5}}, 5);
    ok &= ExpectDelta(machine, "byte write",
                      {0x32, 0x00, 0x40}, {{0x4000, 7}}, 7);
    ok &= ExpectDelta(machine, "word read",
                      {0x2a, 0x00, 0x40}, {{0x4000, 5}}, 10);
    ok &= ExpectDelta(machine, "word write",
                      {0x22, 0x00, 0x40}, {{0x4000, 5}}, 10);
    ok &= ExpectDelta(machine, "cross-page word read",
                      {0x2a, 0xff, 0x43}, {{0x4000, 5}, {0x4400, 7}}, 12);

    if (!ok) {
        return 1;
    }

    std::cout << "portable Z80 wait-state accounting: PASS\n";
    return 0;
}
