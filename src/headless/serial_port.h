#pragma once
#include <memory>
#include <string>
#include <vector>
namespace PC8801 { class SIO; }

// All calls run on the emulation/API thread; Windows owns pending I/O only.
class SerialPort {
public:
    SerialPort();
    ~SerialPort();
    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;
    bool Open(const std::string& port, unsigned baud, unsigned bits,
              const std::string& parity, const std::string& stop, const std::string& flow,
              std::string& error);
    void Close();
    void Pump(PC8801::SIO& serial);
    bool Connected() const;
    const std::string& Name() const { return name_; }
    const std::string& Error() const { return error_; }
    static std::vector<std::string> Ports();
private:
    struct State;
    std::unique_ptr<State> state_;
    std::string name_, error_;
};
