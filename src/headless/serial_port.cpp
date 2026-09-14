#include "headers.h"
#include "serial_port.h"
#include "pc88/sio.h"
#include <algorithm>
#include <array>
#ifdef _WIN32
#include <windows.h>
#endif

struct SerialPort::State {
#ifdef _WIN32
    HANDLE handle=INVALID_HANDLE_VALUE;
    OVERLAPPED read{},write{};
    bool reading=false,writing=false;
    std::array<uint8,4096> input{};
    std::vector<uint8> received,output;
    size_t sent=0;
    ~State() {
        if(handle!=INVALID_HANDLE_VALUE) {
            CancelIoEx(handle,nullptr);
            DWORD count;
            if(reading) GetOverlappedResult(handle,&read,&count,TRUE);
            if(writing) GetOverlappedResult(handle,&write,&count,TRUE);
            CloseHandle(handle);
        }
        if(read.hEvent) CloseHandle(read.hEvent);
        if(write.hEvent) CloseHandle(write.hEvent);
    }
#endif
};
SerialPort::SerialPort()=default;
SerialPort::~SerialPort()=default;
void SerialPort::Close() { state_.reset(); name_.clear(); }
bool SerialPort::Connected() const { return bool(state_); }
bool SerialPort::Open(const std::string& port,unsigned baud,unsigned bits,
                      const std::string& parity,const std::string& stop,const std::string& flow,
                      std::string& error) {
    if(Connected()) { error="close the current COM connection first"; return false; }
    if(port.size()<4 || port.size()>8 || port.substr(0,3)!="COM" || port[3]=='0' ||
       port.find_first_not_of("0123456789",3)!=std::string::npos || !baud || baud>4000000 ||
       bits<5 || bits>8 || (parity!="none" && parity!="odd" && parity!="even") ||
       (stop!="1" && stop!="1.5" && stop!="2") || (stop=="1.5" && bits!=5) ||
       (stop=="2" && bits==5) || (flow!="none" && flow!="rtscts")) {
        error="invalid COM port or serial settings"; return false;
    }
#ifdef _WIN32
    auto next=std::make_unique<State>();
    const std::string path="\\\\.\\"+port;
    next->handle=CreateFileA(path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
    auto fail=[&]() { error="COM operation failed (Windows error "+std::to_string(GetLastError())+")";error_=error;return false; };
    if(next->handle==INVALID_HANDLE_VALUE) return fail();
    DCB dcb{}; dcb.DCBlength=sizeof(dcb);
    if(!GetCommState(next->handle,&dcb)) return fail();
    dcb.BaudRate=baud;dcb.ByteSize=static_cast<BYTE>(bits);
    dcb.Parity=parity=="none"?NOPARITY:parity=="odd"?ODDPARITY:EVENPARITY;
    dcb.StopBits=stop=="1"?ONESTOPBIT:stop=="2"?TWOSTOPBITS:ONE5STOPBITS;
    dcb.fBinary=TRUE;dcb.fParity=parity!="none";
    dcb.fOutxCtsFlow=flow=="rtscts";dcb.fOutxDsrFlow=FALSE;
    dcb.fDtrControl=DTR_CONTROL_ENABLE;dcb.fDsrSensitivity=FALSE;
    dcb.fTXContinueOnXoff=TRUE;dcb.fOutX=FALSE;dcb.fInX=FALSE;
    dcb.fErrorChar=FALSE;dcb.fNull=FALSE;dcb.fAbortOnError=FALSE;
    dcb.fRtsControl=flow=="rtscts"?RTS_CONTROL_HANDSHAKE:RTS_CONTROL_ENABLE;
    if(!SetCommState(next->handle,&dcb)) return fail();
    COMMTIMEOUTS timeouts{};timeouts.ReadIntervalTimeout=MAXDWORD;
    // Immediate buffered reads; writes remain overlapped even with flow control stalled.
    if(!SetCommTimeouts(next->handle,&timeouts)) return fail();
    next->read.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    next->write.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!next->read.hEvent || !next->write.hEvent) return fail();
    state_=std::move(next);name_=port;error_.clear();return true;
#else
    error="COM connections are supported on Windows only";return false;
#endif
}
void SerialPort::Pump(PC8801::SIO& serial) {
#ifdef _WIN32
    if(!state_) return;
    auto& s=*state_;
    auto fail=[&](DWORD code) { error_="COM I/O failed (Windows error "+std::to_string(code)+")";Close(); };
    DWORD errors=0;
    if(!ClearCommError(s.handle,&errors,nullptr)) { fail(GetLastError());return; }
    if(errors) { error_="COM line error (mask "+std::to_string(errors)+")";Close();return; }
    DWORD count=0;
    if(s.reading) {
        if(!GetOverlappedResult(s.handle,&s.read,&count,FALSE)) {
            auto code=GetLastError();if(code!=ERROR_IO_INCOMPLETE) { fail(code);return; }
        } else { s.reading=false;s.received.assign(s.input.begin(),s.input.begin()+count); }
    }
    if(!s.received.empty() && serial.HostWrite(s.received)) s.received.clear();
    if(s.received.empty() && !s.reading) {
        ResetEvent(s.read.hEvent);
        if(ReadFile(s.handle,s.input.data(),static_cast<DWORD>(s.input.size()),&count,&s.read)) {
            s.received.assign(s.input.begin(),s.input.begin()+count);
            if(serial.HostWrite(s.received)) s.received.clear();
        } else {
            auto code=GetLastError();if(code!=ERROR_IO_PENDING) { fail(code);return; }s.reading=true;
        }
    }
    if(s.writing) {
        if(!GetOverlappedResult(s.handle,&s.write,&count,FALSE)) {
            auto code=GetLastError();if(code!=ERROR_IO_INCOMPLETE) fail(code);return;
        }
        s.writing=false;s.sent+=count;
    }
    if(s.sent==s.output.size()) { s.output=serial.HostRead(4096);s.sent=0; }
    if(s.sent<s.output.size()) {
        ResetEvent(s.write.hEvent);
        if(WriteFile(s.handle,s.output.data()+s.sent,static_cast<DWORD>(s.output.size()-s.sent),&count,&s.write)) s.sent+=count;
        else { auto code=GetLastError();if(code!=ERROR_IO_PENDING) {fail(code);return;}s.writing=true; }
    }
#else
    (void)serial;
#endif
}
std::vector<std::string> SerialPort::Ports() {
    std::vector<std::string> ports;
#ifdef _WIN32
    // Read-only enumeration. Query all DOS device names instead of opening ports.
    std::vector<char> names(65536);
    if(QueryDosDeviceA(nullptr,names.data(),static_cast<DWORD>(names.size()))) {
        for(const char* p=names.data();*p;p+=strlen(p)+1) {
            std::string name=p;
            if(name.size()>3 && name.substr(0,3)=="COM" && name.find_first_not_of("0123456789",3)==std::string::npos) ports.push_back(name);
        }
        std::sort(ports.begin(),ports.end());
    }
#endif
    return ports;
}
