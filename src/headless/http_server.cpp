#include "http_server.h"

#include "headless_draw.h"
#include "matrix_keyboard.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <process.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
void CloseSocket(SocketHandle socket) { closesocket(socket); }
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
void CloseSocket(SocketHandle socket) { close(socket); }
#endif

struct SocketRuntime {
    SocketRuntime() {
#ifdef _WIN32
        WSADATA data{};
        ready = WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
        ready = true;
#endif
    }
    ~SocketRuntime() {
#ifdef _WIN32
        if (ready) WSACleanup();
#endif
    }
    bool ready = false;
};

struct Request {
    std::string method;
    std::string path;
    std::map<std::string, std::string> query;
    std::map<std::string, std::string> headers;
};

struct Response {
    int status = 200;
    std::string contentType = "application/json; charset=utf-8";
    std::vector<uint8_t> body;
    bool shutdown = false;
};

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string Trim(std::string value) {
    const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
    if (c >= 'A' && c <= 'F') return 10 + c - 'A';
    return -1;
}

std::string UrlDecode(std::string_view input) {
    std::string output;
    output.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '%' && i + 2 < input.size()) {
            const int high = HexDigit(input[i + 1]);
            const int low = HexDigit(input[i + 2]);
            if (high >= 0 && low >= 0) {
                output.push_back(static_cast<char>((high << 4) | low));
                i += 2;
                continue;
            }
        }
        output.push_back(input[i] == '+' ? ' ' : input[i]);
    }
    return output;
}

std::map<std::string, std::string> ParseQuery(std::string_view value) {
    std::map<std::string, std::string> result;
    size_t start = 0;
    while (start <= value.size()) {
        const size_t amp = value.find('&', start);
        const std::string_view item = value.substr(start, amp == std::string_view::npos ? value.size() - start : amp - start);
        const size_t equals = item.find('=');
        const std::string key = UrlDecode(item.substr(0, equals));
        const std::string val = equals == std::string_view::npos ? "" : UrlDecode(item.substr(equals + 1));
        if (!key.empty()) result[key] = val;
        if (amp == std::string_view::npos) break;
        start = amp + 1;
    }
    return result;
}

bool ParseRequest(const std::string& raw, Request* request) {
    std::istringstream stream(raw);
    std::string firstLine;
    if (!std::getline(stream, firstLine)) return false;
    if (!firstLine.empty() && firstLine.back() == '\r') firstLine.pop_back();

    std::string target;
    std::string version;
    std::istringstream first(firstLine);
    if (!(first >> request->method >> target >> version) || version.rfind("HTTP/", 0) != 0) return false;

    const size_t question = target.find('?');
    request->path = UrlDecode(target.substr(0, question));
    if (question != std::string::npos) request->query = ParseQuery(std::string_view(target).substr(question + 1));

    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break;
        const size_t colon = line.find(':');
        if (colon != std::string::npos) {
            request->headers[Lower(Trim(line.substr(0, colon)))] = Trim(line.substr(colon + 1));
        }
    }
    return true;
}

std::vector<uint8_t> Bytes(const std::string& text) {
    return std::vector<uint8_t>(text.begin(), text.end());
}

std::string JsonEscape(const std::string& value) {
    std::ostringstream output;
    for (unsigned char c : value) {
        switch (c) {
        case '\\': output << "\\\\"; break;
        case '"': output << "\\\""; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (c < 0x20) {
                output << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec;
            } else {
                output << static_cast<char>(c);
            }
        }
    }
    return output.str();
}

Response JsonResponse(int status, const std::string& json) {
    Response response;
    response.status = status;
    response.body = Bytes(json);
    return response;
}

Response ErrorResponse(int status, const std::string& message) {
    return JsonResponse(status, "{\"ok\":false,\"error\":\"" + JsonEscape(message) + "\"}");
}

std::string StatusText(int status) {
    switch (status) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 413: return "Payload Too Large";
    default: return "Internal Server Error";
    }
}

bool ParseUnsigned(const std::string& text, uint32_t maximum, uint32_t* value) {
    if (text.empty()) return false;
    std::string number = text;
    int base = 10;
    if (number.size() > 2 && number[0] == '0' && (number[1] == 'x' || number[1] == 'X')) {
        base = 16;
        number.erase(0, 2);
    } else if (!number.empty() && (number.back() == 'h' || number.back() == 'H')) {
        base = 16;
        number.pop_back();
    }
    try {
        size_t consumed = 0;
        const unsigned long parsed = std::stoul(number, &consumed, base);
        if (consumed != number.size() || parsed > maximum) return false;
        *value = static_cast<uint32_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool QueryBool(const std::map<std::string, std::string>& query, const std::string& key, bool fallback) {
    const auto it = query.find(key);
    if (it == query.end()) return fallback;
    const std::string value = Lower(it->second);
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

std::string RegistersJson(const HeadlessMachine::Registers& reg) {
    std::ostringstream out;
    out << "{\"pc\":" << reg.pc << ",\"sp\":" << reg.sp
        << ",\"af\":" << reg.af << ",\"bc\":" << reg.bc
        << ",\"de\":" << reg.de << ",\"hl\":" << reg.hl
        << ",\"ix\":" << reg.ix << ",\"iy\":" << reg.iy
        << ",\"af_alt\":" << reg.afAlt << ",\"bc_alt\":" << reg.bcAlt
        << ",\"de_alt\":" << reg.deAlt << ",\"hl_alt\":" << reg.hlAlt
        << ",\"i\":" << static_cast<unsigned>(reg.i)
        << ",\"r\":" << static_cast<unsigned>(reg.r)
        << ",\"interrupt_mode\":" << static_cast<unsigned>(reg.interruptMode)
        << ",\"iff1\":" << (reg.iff1 ? "true" : "false")
        << ",\"iff2\":" << (reg.iff2 ? "true" : "false") << '}';
    return out.str();
}

std::string StatusJson(const HeadlessMachine& machine) {
    std::ostringstream out;
    out << "{\"ok\":true,\"machine\":\"" << machine.MachineName() << "\",\"mode\":\""
        << machine.BasicModeName() << "\",\"n80_rom\":\""
        << JsonEscape(machine.SelectedN80Rom()) << "\",\"frames\":"
        << machine.FrameCount() << ",\"framebuffer\":{\"width\":" << machine.Framebuffer().Width()
        << ",\"height\":" << machine.Framebuffer().Height() << "},\"registers\":"
        << RegistersJson(machine.GetRegisters()) << '}';
    return out.str();
}

bool Authorized(const Request& request, const std::string& token) {
    const auto direct = request.headers.find("x-m88-token");
    if (direct != request.headers.end() && direct->second == token) return true;
    const auto authorization = request.headers.find("authorization");
    return authorization != request.headers.end() && authorization->second == "Bearer " + token;
}

bool WriteFile(const std::string& path, const std::vector<uint8_t>& bytes, std::string* error) {
    std::ofstream output(fs::u8path(path), std::ios::binary | std::ios::trunc);
    if (!output) {
        if (error) *error = "cannot create file: " + path;
        return false;
    }
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        if (error) *error = "failed while writing file: " + path;
        return false;
    }
    return true;
}

std::string Hex(const std::vector<uint8_t>& bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string output;
    output.resize(bytes.size() * 2);
    for (size_t i = 0; i < bytes.size(); ++i) {
        output[i * 2] = digits[bytes[i] >> 4];
        output[i * 2 + 1] = digits[bytes[i] & 0x0f];
    }
    return output;
}

Response HandleRequest(HeadlessMachine& machine, const Request& request, const std::string& token) {
    if (request.path == "/health") return JsonResponse(200, "{\"ok\":true}");
    if (request.path.rfind("/v1/", 0) != 0) return ErrorResponse(404, "endpoint not found");
    if (!Authorized(request, token)) return ErrorResponse(401, "missing or invalid API token");

    if (request.path == "/v1/status" && request.method == "GET") {
        return JsonResponse(200, StatusJson(machine));
    }
    if (request.path == "/v1/registers" && request.method == "GET") {
        return JsonResponse(200, "{\"ok\":true,\"registers\":" + RegistersJson(machine.GetRegisters()) + '}');
    }
    if (request.path == "/v1/reset" && request.method == "POST") {
        machine.ResetMachine();
        return JsonResponse(200, StatusJson(machine));
    }
    if (request.path == "/v1/run" && request.method == "POST") {
        uint32_t frames = 1;
        const auto it = request.query.find("frames");
        if (it != request.query.end() && !ParseUnsigned(it->second, 100000, &frames)) {
            return ErrorResponse(400, "frames must be an integer from 0 to 100000");
        }
        std::string error;
        if (!machine.RunFrames(frames, &error)) return ErrorResponse(400, error);
        return JsonResponse(200, StatusJson(machine));
    }
    if (request.path == "/v1/load-bin" && request.method == "POST") {
        const auto path = request.query.find("path");
        if (path == request.query.end() || path->second.empty()) return ErrorResponse(400, "path is required");
        uint32_t address = 0xc000;
        const auto addressValue = request.query.find("address");
        if (addressValue != request.query.end() && !ParseUnsigned(addressValue->second, 0xffff, &address)) {
            return ErrorResponse(400, "address must be a 16-bit integer (for example C000H)");
        }
        std::string error;
        if (!machine.LoadBinary(path->second, static_cast<uint16_t>(address), QueryBool(request.query, "launcher", true), &error)) {
            return ErrorResponse(400, error);
        }
        return JsonResponse(200, StatusJson(machine));
    }
    if (request.path == "/v1/tape/open" && request.method == "POST") {
        const auto path = request.query.find("path");
        if (path == request.query.end() || path->second.empty()) return ErrorResponse(400, "path is required");
        std::string error;
        if (!machine.OpenTape(path->second, &error)) return ErrorResponse(400, error);
        return JsonResponse(200, "{\"ok\":true,\"path\":\"" + JsonEscape(fs::absolute(fs::u8path(path->second)).string()) + "\"}");
    }
    if (request.path == "/v1/key" && request.method == "POST") {
        const bool down = QueryBool(request.query, "down", true);
        const auto name = request.query.find("name");
        if (name != request.query.end()) {
            if (!machine.SetNamedKey(name->second, down)) return ErrorResponse(400, "unknown key name");
            return JsonResponse(200, "{\"ok\":true,\"name\":\"" + JsonEscape(name->second) + "\",\"down\":" + (down ? "true" : "false") + '}');
        }
        uint32_t row = 0, bit = 0;
        const auto rowValue = request.query.find("row");
        const auto bitValue = request.query.find("bit");
        if (rowValue == request.query.end() || bitValue == request.query.end() ||
            !ParseUnsigned(rowValue->second, 15, &row) || !ParseUnsigned(bitValue->second, 7, &bit) ||
            !machine.SetKey(static_cast<int>(row), static_cast<int>(bit), down)) {
            return ErrorResponse(400, "provide name or row=0..15 and bit=0..7");
        }
        return JsonResponse(200, "{\"ok\":true,\"row\":" + std::to_string(row) + ",\"bit\":" + std::to_string(bit) + ",\"down\":" + (down ? "true" : "false") + '}');
    }
    if (request.path == "/v1/keys/release" && request.method == "POST") {
        machine.ReleaseAllKeys();
        return JsonResponse(200, "{\"ok\":true}");
    }
    if (request.path == "/v1/frame.png" && request.method == "GET") {
        Response response;
        response.contentType = "image/png";
        response.body = machine.Framebuffer().EncodePng();
        if (response.body.empty()) return ErrorResponse(500, "failed to encode framebuffer");
        return response;
    }
    if (request.path == "/v1/capture" && request.method == "POST") {
        const auto path = request.query.find("path");
        if (path == request.query.end() || path->second.empty()) return ErrorResponse(400, "path is required");
        std::string error;
        if (!machine.Framebuffer().SavePng(path->second, &error)) return ErrorResponse(400, error);
        return JsonResponse(200, "{\"ok\":true,\"path\":\"" + JsonEscape(fs::absolute(fs::u8path(path->second)).string()) + "\"}");
    }
    if (request.path == "/v1/memory" && request.method == "GET") {
        const std::string space = request.query.count("space") ? request.query.at("space") : "ram";
        uint32_t address = 0;
        uint32_t length = 256;
        if (request.query.count("address") && !ParseUnsigned(request.query.at("address"), 0xffff, &address)) {
            return ErrorResponse(400, "invalid address");
        }
        if (request.query.count("length") && !ParseUnsigned(request.query.at("length"), 0x10000, &length)) {
            return ErrorResponse(400, "invalid length");
        }
        std::string error;
        const std::vector<uint8_t> data = machine.ReadMemory(space, address, length, &error);
        if (!error.empty()) return ErrorResponse(400, error);
        return JsonResponse(200, "{\"ok\":true,\"space\":\"" + JsonEscape(space) + "\",\"address\":" + std::to_string(address) + ",\"length\":" + std::to_string(data.size()) + ",\"hex\":\"" + Hex(data) + "\"}");
    }
    if (request.path == "/v1/dump" && request.method == "GET") {
        Response response;
        response.contentType = "application/octet-stream";
        response.body = machine.CreateDevelopmentDump();
        return response;
    }
    if (request.path == "/v1/dump-file" && request.method == "POST") {
        const auto path = request.query.find("path");
        if (path == request.query.end() || path->second.empty()) return ErrorResponse(400, "path is required");
        std::string error;
        if (!WriteFile(path->second, machine.CreateDevelopmentDump(), &error)) return ErrorResponse(400, error);
        return JsonResponse(200, "{\"ok\":true,\"path\":\"" + JsonEscape(fs::absolute(fs::u8path(path->second)).string()) + "\"}");
    }
    if (request.path == "/v1/shutdown" && request.method == "POST") {
        Response response = JsonResponse(200, "{\"ok\":true,\"shutdown\":true}");
        response.shutdown = true;
        return response;
    }
    if ((request.path == "/v1/status" || request.path == "/v1/registers" || request.path == "/v1/frame.png" ||
         request.path == "/v1/memory" || request.path == "/v1/dump") && request.method != "GET") {
        return ErrorResponse(405, "method not allowed");
    }
    return ErrorResponse(404, "endpoint not found");
}

bool ReceiveHeaders(SocketHandle socket, std::string* raw) {
    constexpr size_t maximum = 64 * 1024;
    std::array<char, 4096> buffer{};
    while (raw->find("\r\n\r\n") == std::string::npos) {
#ifdef _WIN32
        const int count = recv(socket, buffer.data(), static_cast<int>(buffer.size()), 0);
#else
        const ssize_t count = recv(socket, buffer.data(), buffer.size(), 0);
#endif
        if (count <= 0) return false;
        raw->append(buffer.data(), static_cast<size_t>(count));
        if (raw->size() > maximum) return false;
    }
    return true;
}

bool SendAll(SocketHandle socket, const uint8_t* data, size_t size) {
    while (size > 0) {
#ifdef _WIN32
        const int chunk = send(socket, reinterpret_cast<const char*>(data), static_cast<int>(std::min<size_t>(size, std::numeric_limits<int>::max())), 0);
#else
        const ssize_t chunk = send(socket, data, size, 0);
#endif
        if (chunk <= 0) return false;
        data += chunk;
        size -= static_cast<size_t>(chunk);
    }
    return true;
}

bool SendResponse(SocketHandle socket, const Response& response) {
    std::ostringstream header;
    header << "HTTP/1.1 " << response.status << ' ' << StatusText(response.status) << "\r\n"
           << "Content-Type: " << response.contentType << "\r\n"
           << "Content-Length: " << response.body.size() << "\r\n"
           << "Cache-Control: no-store\r\n"
           << "Connection: close\r\n\r\n";
    const std::string text = header.str();
    return SendAll(socket, reinterpret_cast<const uint8_t*>(text.data()), text.size()) &&
           SendAll(socket, response.body.data(), response.body.size());
}

bool WriteConnectionFile(const std::string& path, uint16_t port, const std::string& token, std::string* error) {
    if (path.empty()) return true;
    const fs::path outputPath = fs::u8path(path);
    std::error_code ec;
    if (!outputPath.parent_path().empty()) fs::create_directories(outputPath.parent_path(), ec);
    if (ec) {
        if (error) *error = "cannot create connection-file directory: " + ec.message();
        return false;
    }
    std::ofstream output(outputPath, std::ios::trunc);
    if (!output) {
        if (error) *error = "cannot create connection file: " + path;
        return false;
    }
    output << "{\n  \"url\": \"http://127.0.0.1:" << port << "\",\n"
           << "  \"token\": \"" << JsonEscape(token) << "\",\n"
           << "  \"pid\": ";
#ifdef _WIN32
    output << _getpid();
#else
    output << getpid();
#endif
    output << "\n}\n";
    return static_cast<bool>(output);
}

} // namespace

bool HttpServer::Serve(uint16_t requestedPort, const std::string& token,
                       const std::string& connectionFile, std::string* error) {
    SocketRuntime runtime;
    if (!runtime.ready) {
        if (error) *error = "socket runtime initialization failed";
        return false;
    }

    SocketHandle listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == kInvalidSocket) {
        if (error) *error = "cannot create listening socket";
        return false;
    }
    int reuse = 1;
#ifdef _WIN32
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
#else
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(requestedPort);
    if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || listen(listener, 8) != 0) {
        CloseSocket(listener);
        if (error) *error = "cannot bind 127.0.0.1:" + std::to_string(requestedPort);
        return false;
    }

    sockaddr_in bound{};
#ifdef _WIN32
    int boundLength = sizeof(bound);
#else
    socklen_t boundLength = sizeof(bound);
#endif
    if (getsockname(listener, reinterpret_cast<sockaddr*>(&bound), &boundLength) != 0) {
        CloseSocket(listener);
        if (error) *error = "cannot determine listening port";
        return false;
    }
    const uint16_t port = ntohs(bound.sin_port);
    if (!WriteConnectionFile(connectionFile, port, token, error)) {
        CloseSocket(listener);
        return false;
    }

    std::cout << "{\"event\":\"listening\",\"url\":\"http://127.0.0.1:" << port
              << "\",\"token\":\"" << JsonEscape(token) << "\"}" << std::endl;

    bool stopping = false;
    while (!stopping) {
        sockaddr_in peer{};
#ifdef _WIN32
        int peerLength = sizeof(peer);
#else
        socklen_t peerLength = sizeof(peer);
#endif
        SocketHandle client = accept(listener, reinterpret_cast<sockaddr*>(&peer), &peerLength);
        if (client == kInvalidSocket) continue;

        Response response;
        std::string raw;
        Request request;
        if (!ReceiveHeaders(client, &raw) || !ParseRequest(raw, &request)) {
            response = ErrorResponse(400, "invalid HTTP request");
        } else {
            response = HandleRequest(machine_, request, token);
        }
        SendResponse(client, response);
        CloseSocket(client);
        stopping = response.shutdown;
    }
    CloseSocket(listener);
    return true;
}

bool RunHeadlessSelfTest(std::string* error) {
    MatrixKeyboard keyboard;
    if (!keyboard.SetNamedKey("esc", true) || keyboard.Row(9) != 0x7f ||
        !keyboard.SetNamedKey("esc", false) || keyboard.Row(9) != 0xff) {
        if (error) *error = "keyboard matrix self-test failed";
        return false;
    }
    if (!keyboard.SetNamedKey("numpad2", true) || keyboard.Row(0) != 0xfb ||
        !keyboard.SetNamedKey("numpad2", false) ||
        !keyboard.SetNamedKey("numpad8", true) || keyboard.Row(1) != 0xfe ||
        !keyboard.SetNamedKey("numpad8", false) ||
        !keyboard.SetNamedKey("left_shift", true) || keyboard.Row(8) != 0xbf ||
        !keyboard.SetNamedKey("left_shift", false) ||
        !keyboard.SetNamedKey("space", true) || keyboard.Row(9) != 0xbf ||
        !keyboard.SetNamedKey("space", false)) {
        if (error) *error = "game control matrix self-test failed";
        return false;
    }

    HeadlessDraw draw;
    if (!draw.Init(2, 2, 8)) {
        if (error) *error = "framebuffer initialization self-test failed";
        return false;
    }
    Draw::Palette palette[2] = {{0, 0, 0, 0}, {255, 0, 0, 0}};
    draw.SetPalette(0, 2, palette);
    uint8* image = nullptr;
    int stride = 0;
    if (!draw.Lock(&image, &stride) || stride != 2) {
        if (error) *error = "framebuffer lock self-test failed";
        return false;
    }
    image[0] = 1;
    draw.Unlock();
    const std::vector<uint8_t> png = draw.EncodePng();
    const uint8_t signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (png.size() < sizeof(signature) || !std::equal(std::begin(signature), std::end(signature), png.begin())) {
        if (error) *error = "PNG encoder self-test failed";
        return false;
    }
    return true;
}
