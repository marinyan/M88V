// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include <string>
#include <cstdio>
namespace M88V {
inline std::string Quote(const std::string& value) {
    std::string out="\"";
    for (unsigned char c:value) {
        if (c=='"' || c=='\\') { out+='\\'; out+=c; }
        else if (c<32) { char b[7]; std::snprintf(b,sizeof(b),"\\u%04x",c); out+=b; }
        else out+=c;
    }
    return out+'"';
}
}
