// SPDX-License-Identifier: BSD-2-Clause
#include "headers.h"
#include "pc88/sio.h"
#include <iostream>
#include <stdexcept>
void Check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
int main() {
    try {
        IOBus bus;Check(bus.Init(16),"bus init");
        PC8801::SIO sio(0);sio.Init(&bus,1,2);sio.Reset();
        Check(!sio.HostWrite({1}),"closed write rejected");
        sio.EnableHost(true);
        Check(sio.HostWrite({0xff,0x81}),"queue before RX enabled");
        Check(!(sio.GetStatus()&2),"RX disabled");
        sio.SetControl(0,0x42); // 5-bit asynchronous mode
        sio.SetControl(0,5);
        Check(sio.GetData()==31 && sio.GetData()==1,"RX masks data length and preserves order");
        Check(!(sio.GetStatus()&2),"RX empty");
        sio.AcceptData(0,99);Check(!(sio.GetStatus()&2),"cassette input excluded");
        for(size_t i=0;i<PC8801::SIO::HostCapacity+1;++i) sio.SetData(0,0xff);
        Check(sio.HostTxPending()==PC8801::SIO::HostCapacity && sio.HostDropped()==1,"bounded TX with counted overflow");
        Check(!(sio.GetStatus()&1),"TX not ready when full");
        Check(sio.HostRead(1)==std::vector<uint8>{31},"TX masks bits");
        Check((sio.GetStatus()&1)!=0,"TX ready after draining");
        sio.HostRead(PC8801::SIO::HostCapacity);
        Check((sio.GetStatus()&4)!=0,"TX empty after draining");
        sio.HostWrite({3});sio.SetData(0,4);sio.Reset();
        Check(sio.HostEnabled() && !sio.HostRxPending() && !sio.HostTxPending() && !sio.HostDropped(),"reset clears queues");
        sio.EnableHost(false);sio.AcceptData(0,6);
        Check(!sio.HostEnabled(),"close");
        std::cout<<"Serial queue, framing and backpressure tests passed\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
