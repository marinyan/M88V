// SPDX-License-Identifier: BSD-2-Clause
#include "headers.h"
#include "wincore.h"
#include "serial_dialog.h"
#include "resource.h"
#include "pc88/sio.h"
#include "pc88/tapemgr.h"

bool WinCore::OpenSerial(const std::string& port,unsigned baud,unsigned bits,const std::string& parity,const std::string& stop,const std::string& flow,std::string& error) {
    LockObj lock(this);
    if(serialTape && (serialTape->IsOpen() || serialTape->HasRecording() || serialTape->IsOutputActive())) {
        error="Eject the tape and save/clear cassette output before connecting serial.";return false;
    }
    if(!serialPort.Open(port,baud,bits,parity,stop,flow,error)) return false;
    GetSerial()->EnableHost(true);return true;
}
void WinCore::CloseSerial() { LockObj lock(this);serialPort.Close();GetSerial()->EnableHost(false); }
bool WinCore::SerialActive() { LockObj lock(this);return GetSerial()->HostEnabled(); }
std::string WinCore::SerialStatus() {
    LockObj lock(this);
    if(!serialPort.Connected()) return serialPort.Error().empty()?"Disconnected":"Disconnected: "+serialPort.Error();
    return "Connected: "+serialPort.Name()+"   RX queued: "+std::to_string(GetSerial()->HostRxPending())+
        "   TX queued: "+std::to_string(GetSerial()->HostTxPending())+"   Dropped: "+std::to_string(GetSerial()->HostDropped());
}
namespace {
std::string Text(HWND dialog,int id) { char value[128]{};GetDlgItemTextA(dialog,id,value,sizeof(value));return value; }
void Choices(HWND dialog,int id,std::initializer_list<const char*> values,const char* initial) {
    for(auto value:values) SendDlgItemMessageA(dialog,id,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(value));
    SendDlgItemMessageA(dialog,id,CB_SELECTSTRING,-1,reinterpret_cast<LPARAM>(initial));
    SetDlgItemTextA(dialog,id,initial);
}
void Ports(HWND dialog) {
    auto previous=Text(dialog,IDC_SERIAL_PORT);
    SendDlgItemMessageA(dialog,IDC_SERIAL_PORT,CB_RESETCONTENT,0,0);
    for(const auto& port:SerialPort::Ports()) SendDlgItemMessageA(dialog,IDC_SERIAL_PORT,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(port.c_str()));
    SetDlgItemTextA(dialog,IDC_SERIAL_PORT,previous.c_str());
}
void Update(HWND dialog,WinCore& core) {
    const bool active=core.SerialActive();
    for(int id:{IDC_SERIAL_PORT,IDC_SERIAL_BAUD,IDC_SERIAL_BITS,IDC_SERIAL_PARITY,IDC_SERIAL_STOP,IDC_SERIAL_FLOW,IDC_SERIAL_REFRESH,IDC_SERIAL_CONNECT}) EnableWindow(GetDlgItem(dialog,id),!active);
    EnableWindow(GetDlgItem(dialog,IDC_SERIAL_DISCONNECT),active);
    SetDlgItemTextA(dialog,IDC_SERIAL_STATUS,core.SerialStatus().c_str());
}
// Remember choices during this application session; never auto-connect on startup.
std::string settings[]={"","9600","8","none","1","none"};
INT_PTR CALLBACK Dialog(HWND dialog,UINT message,WPARAM wp,LPARAM lp) {
    auto* core=reinterpret_cast<WinCore*>(GetWindowLongPtr(dialog,DWLP_USER));
    if(message==WM_INITDIALOG) {
        core=reinterpret_cast<WinCore*>(lp);SetWindowLongPtr(dialog,DWLP_USER,lp);
        Choices(dialog,IDC_SERIAL_BAUD,{"300","600","1200","2400","4800","9600","19200","38400","57600","115200"},settings[1].c_str());
        Choices(dialog,IDC_SERIAL_BITS,{"5","6","7","8"},settings[2].c_str());
        Choices(dialog,IDC_SERIAL_PARITY,{"none","odd","even"},settings[3].c_str());
        Choices(dialog,IDC_SERIAL_STOP,{"1","1.5","2"},settings[4].c_str());
        Choices(dialog,IDC_SERIAL_FLOW,{"none","rtscts"},settings[5].c_str());
        Ports(dialog);SetDlgItemTextA(dialog,IDC_SERIAL_PORT,settings[0].c_str());
        SetTimer(dialog,1,250,nullptr);Update(dialog,*core);return TRUE;
    }
    if(!core) return FALSE;
    if(message==WM_TIMER) { Update(dialog,*core);return TRUE; }
    if(message==WM_COMMAND) {
        switch(LOWORD(wp)) {
        case IDC_SERIAL_REFRESH: Ports(dialog);return TRUE;
        case IDC_SERIAL_CONNECT: {
            std::string error;
            auto baud=Text(dialog,IDC_SERIAL_BAUD);
            if(baud.empty() || baud.size()>7 || baud.find_first_not_of("0123456789")!=std::string::npos) error="Enter a baud rate from 1 to 4000000.";
            else {
                unsigned rate=static_cast<unsigned>(std::stoul(baud));
                if(core->OpenSerial(Text(dialog,IDC_SERIAL_PORT),rate,static_cast<unsigned>(std::atoi(Text(dialog,IDC_SERIAL_BITS).c_str())),Text(dialog,IDC_SERIAL_PARITY),Text(dialog,IDC_SERIAL_STOP),Text(dialog,IDC_SERIAL_FLOW),error)) {
                    for(int i=0;i<6;++i) settings[i]=Text(dialog,IDC_SERIAL_PORT+i);
                }
            }
            if(!error.empty()) MessageBoxA(dialog,error.c_str(),"Serial",MB_OK|MB_ICONERROR);
            Update(dialog,*core);return TRUE;
        }
        case IDC_SERIAL_DISCONNECT: core->CloseSerial();Update(dialog,*core);return TRUE;
        case IDOK: case IDCANCEL: EndDialog(dialog,0);return TRUE;
        }
    }
    if(message==WM_CLOSE) { EndDialog(dialog,0);return TRUE; }
    if(message==WM_DESTROY) KillTimer(dialog,1);
    return FALSE;
}
}
void ShowSerialDialog(HINSTANCE instance,HWND owner,WinCore& core) {
    DialogBoxParamA(instance,MAKEINTRESOURCEA(IDD_SERIAL),owner,Dialog,reinterpret_cast<LPARAM>(&core));
}
