/*
    Copyright 2026 Hayao0819

    This file is part of 3Beans.

    3Beans is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    3Beans is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
    General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with 3Beans. If not, see <https://www.gnu.org/licenses/>.
*/

// mcp.h - minimal single-header C++11 MCP (Model Context Protocol) server library
// with a tools capability; no dependencies beyond the STL and POSIX sockets.
// Transports: stdio per the MCP spec, or line-delimited JSON-RPC over local TCP
// (bridge into stdio clients with `socat STDIO TCP:host:port`).

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

namespace mcp {

// A small JSON value: parse, build, and serialize
class Json {
public:
    enum Type { NUL, BOOL, NUM, STR, ARR, OBJ };

    Json(): type(NUL) {}
    Json(bool b): type(BOOL), boolean(b) {}
    Json(double n): type(NUM), number(n) {}
    Json(int64_t n): type(NUM), number((double)n) {}
    Json(int n): type(NUM), number(n) {}
    Json(uint32_t n): type(NUM), number(n) {}
    Json(const char *s): type(STR), string(s) {}
    Json(const std::string &s): type(STR), string(s) {}
    static Json array() { Json j; j.type = ARR; return j; }
    static Json object() { Json j; j.type = OBJ; return j; }

    Type getType() const { return type; }
    bool isNull() const { return type == NUL; }
    bool toBool(bool def = false) const { return type == BOOL ? boolean : def; }
    double toNum(double def = 0) const { return type == NUM ? number : def; }
    int64_t toInt(int64_t def = 0) const { return type == NUM ? (int64_t)number : def; }
    std::string toStr(const std::string &def = "") const { return type == STR ? string : def; }
    size_t size() const { return type == ARR ? array_.size() : (type == OBJ ? object_.size() : 0); }

    // Object/array access; returns a shared null for missing keys
    const Json &operator[](const std::string &key) const {
        static const Json null;
        if (type != OBJ) return null;
        auto it = object_.find(key);
        return it == object_.end() ? null : it->second;
    }
    const Json &at(size_t i) const {
        static const Json null;
        return (type == ARR && i < array_.size()) ? array_[i] : null;
    }

    Json &set(const std::string &key, Json value) {
        type = OBJ;
        object_[key] = std::move(value);
        keyOrder.push_back(key);
        return *this;
    }
    Json &push(Json value) {
        type = ARR;
        array_.push_back(std::move(value));
        return *this;
    }

    std::string dump() const {
        std::string out;
        write(out);
        return out;
    }

    static Json parse(const std::string &text, bool &ok) {
        size_t pos = 0;
        ok = true;
        Json j = parseValue(text, pos, ok);
        return ok ? j : Json();
    }

private:
    Type type;
    bool boolean = false;
    double number = 0;
    std::string string;
    std::vector<Json> array_;
    std::map<std::string, Json> object_;
    std::vector<std::string> keyOrder; // preserves insertion order when serializing

    void write(std::string &out) const {
        char buf[32];
        switch (type) {
            case NUL: out += "null"; return;
            case BOOL: out += boolean ? "true" : "false"; return;
            case NUM:
                if (number == (double)(int64_t)number)
                    snprintf(buf, sizeof(buf), "%lld", (long long)number);
                else
                    snprintf(buf, sizeof(buf), "%.17g", number);
                out += buf;
                return;
            case STR: writeString(out, string); return;
            case ARR:
                out += '[';
                for (size_t i = 0; i < array_.size(); i++) {
                    if (i) out += ',';
                    array_[i].write(out);
                }
                out += ']';
                return;
            case OBJ: {
                out += '{';
                bool first = true;
                for (const std::string &key : keyOrder) {
                    auto it = object_.find(key);
                    if (it == object_.end()) continue;
                    if (!first) out += ',';
                    first = false;
                    writeString(out, key);
                    out += ':';
                    it->second.write(out);
                }
                out += '}';
                return;
            }
        }
    }

    static void writeString(std::string &out, const std::string &s) {
        out += '"';
        for (unsigned char c : s) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20) {
                        char buf[8];
                        snprintf(buf, sizeof(buf), "\\u%04x", c);
                        out += buf;
                    }
                    else out += c;
            }
        }
        out += '"';
    }

    static void skipWs(const std::string &t, size_t &p) {
        while (p < t.size() && (t[p] == ' ' || t[p] == '\t' || t[p] == '\n' || t[p] == '\r')) p++;
    }

    static Json parseValue(const std::string &t, size_t &p, bool &ok) {
        skipWs(t, p);
        if (p >= t.size()) { ok = false; return Json(); }
        char c = t[p];
        if (c == '{') return parseObject(t, p, ok);
        if (c == '[') return parseArray(t, p, ok);
        if (c == '"') return Json(parseString(t, p, ok));
        if (!t.compare(p, 4, "true")) { p += 4; return Json(true); }
        if (!t.compare(p, 5, "false")) { p += 5; return Json(false); }
        if (!t.compare(p, 4, "null")) { p += 4; return Json(); }
        // Number
        char *end = nullptr;
        double num = strtod(t.c_str() + p, &end);
        if (end == t.c_str() + p) { ok = false; return Json(); }
        p = end - t.c_str();
        return Json(num);
    }

    static std::string parseString(const std::string &t, size_t &p, bool &ok) {
        std::string out;
        p++; // opening quote
        while (p < t.size() && t[p] != '"') {
            if (t[p] == '\\' && p + 1 < t.size()) {
                p++;
                switch (t[p]) {
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u': {
                        if (p + 4 >= t.size()) { ok = false; return out; }
                        unsigned cp = strtoul(t.substr(p + 1, 4).c_str(), nullptr, 16);
                        p += 4;
                        // Surrogate pair
                        if (cp >= 0xD800 && cp <= 0xDBFF && p + 6 < t.size() && t[p+1] == '\\' && t[p+2] == 'u') {
                            unsigned lo = strtoul(t.substr(p + 3, 4).c_str(), nullptr, 16);
                            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                                p += 6;
                            }
                        }
                        // Encode UTF-8
                        if (cp < 0x80) out += (char)cp;
                        else if (cp < 0x800) {
                            out += (char)(0xC0 | (cp >> 6));
                            out += (char)(0x80 | (cp & 0x3F));
                        }
                        else if (cp < 0x10000) {
                            out += (char)(0xE0 | (cp >> 12));
                            out += (char)(0x80 | ((cp >> 6) & 0x3F));
                            out += (char)(0x80 | (cp & 0x3F));
                        }
                        else {
                            out += (char)(0xF0 | (cp >> 18));
                            out += (char)(0x80 | ((cp >> 12) & 0x3F));
                            out += (char)(0x80 | ((cp >> 6) & 0x3F));
                            out += (char)(0x80 | (cp & 0x3F));
                        }
                        break;
                    }
                    default: out += t[p];
                }
                p++;
            }
            else out += t[p++];
        }
        if (p >= t.size()) { ok = false; return out; }
        p++; // closing quote
        return out;
    }

    static Json parseArray(const std::string &t, size_t &p, bool &ok) {
        Json j = array();
        p++; // '['
        skipWs(t, p);
        if (p < t.size() && t[p] == ']') { p++; return j; }
        while (true) {
            j.push(parseValue(t, p, ok));
            if (!ok) return j;
            skipWs(t, p);
            if (p >= t.size()) { ok = false; return j; }
            if (t[p] == ',') { p++; continue; }
            if (t[p] == ']') { p++; return j; }
            ok = false;
            return j;
        }
    }

    static Json parseObject(const std::string &t, size_t &p, bool &ok) {
        Json j = object();
        p++; // '{'
        skipWs(t, p);
        if (p < t.size() && t[p] == '}') { p++; return j; }
        while (true) {
            skipWs(t, p);
            if (p >= t.size() || t[p] != '"') { ok = false; return j; }
            std::string key = parseString(t, p, ok);
            if (!ok) return j;
            skipWs(t, p);
            if (p >= t.size() || t[p] != ':') { ok = false; return j; }
            p++;
            j.set(key, parseValue(t, p, ok));
            if (!ok) return j;
            skipWs(t, p);
            if (p >= t.size()) { ok = false; return j; }
            if (t[p] == ',') { p++; continue; }
            if (t[p] == '}') { p++; return j; }
            ok = false;
            return j;
        }
    }
};

// Result of a tool call: text content, optionally flagged as an error
struct ToolResult {
    std::string text;
    bool error;

    ToolResult(std::string text = "", bool error = false): text(text), error(error) {}
    static ToolResult ok(const std::string &text) { return ToolResult(text, false); }
    static ToolResult err(const std::string &text) { return ToolResult(text, true); }
};

typedef std::function<ToolResult(const Json &args)> ToolFn;

struct Tool {
    std::string name, description, schema;
    ToolFn fn;
};

class Server {
public:
    Server(const std::string &name, const std::string &version): name(name), version(version) {}

    // schema is the tool's full inputSchema as a JSON string
    void tool(const std::string &name, const std::string &description, const std::string &schema, ToolFn fn) {
        tools.push_back({ name, description, schema, fn });
    }

    // Handle one JSON-RPC message; returns the response, or "" for notifications
    std::string handle(const std::string &line) {
        bool ok = true;
        Json msg = Json::parse(line, ok);
        if (!ok) return rpcError(Json(), -32700, "parse error");
        const Json &id = msg["id"];
        std::string method = msg["method"].toStr();
        if (method.empty()) return ""; // ignore responses/invalid
        bool isNotification = id.isNull();

        if (method == "initialize") {
            std::string ver = msg["params"]["protocolVersion"].toStr("2024-11-05");
            Json result = Json::object()
                .set("protocolVersion", ver)
                .set("capabilities", Json::object().set("tools", Json::object()))
                .set("serverInfo", Json::object().set("name", name).set("version", version));
            return rpcResult(id, result);
        }
        if (method == "ping") return rpcResult(id, Json::object());
        if (method == "tools/list") {
            Json list = Json::array();
            for (const Tool &t : tools) {
                bool sok = true;
                Json schema = Json::parse(t.schema, sok);
                if (!sok) schema = Json::parse("{\"type\":\"object\"}", sok);
                list.push(Json::object().set("name", t.name)
                    .set("description", t.description).set("inputSchema", schema));
            }
            return rpcResult(id, Json::object().set("tools", list));
        }
        if (method == "tools/call") {
            std::string toolName = msg["params"]["name"].toStr();
            for (const Tool &t : tools) {
                if (t.name != toolName) continue;
                ToolResult res;
                try {
                    res = t.fn(msg["params"]["arguments"]);
                }
                catch (const std::exception &e) {
                    res = ToolResult::err(std::string("exception: ") + e.what());
                }
                catch (...) {
                    res = ToolResult::err("unknown exception");
                }
                Json content = Json::array().push(Json::object().set("type", "text").set("text", res.text));
                return rpcResult(id, Json::object().set("content", content).set("isError", res.error));
            }
            return rpcError(id, -32602, "unknown tool: " + toolName);
        }
        if (isNotification) return ""; // e.g. notifications/initialized
        return rpcError(id, -32601, "method not found: " + method);
    }

    // Serve MCP over stdio (blocking)
    void runStdio() {
        std::string line;
        int c;
        while ((c = fgetc(stdin)) != EOF) {
            if (c != '\n') { line += (char)c; continue; }
            std::string resp = handle(line);
            line.clear();
            if (resp.empty()) continue;
            resp += '\n';
            fwrite(resp.data(), 1, resp.size(), stdout);
            fflush(stdout);
        }
    }

    // Serve MCP over a local TCP socket, one client at a time (blocking)
    void runTcp(uint16_t port) {
        int listenFd = socket(AF_INET, SOCK_STREAM, 0);
        if (listenFd < 0) return;
        int one = 1;
        setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(port);
        if (bind(listenFd, (sockaddr*)&addr, sizeof(addr)) < 0 || listen(listenFd, 1) < 0) {
            close(listenFd);
            return;
        }
        while (true) {
            int fd = accept(listenFd, nullptr, nullptr);
            if (fd < 0) break;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
            std::string buf;
            char chunk[4096];
            ssize_t n;
            while ((n = read(fd, chunk, sizeof(chunk))) > 0) {
                buf.append(chunk, n);
                size_t nl;
                while ((nl = buf.find('\n')) != std::string::npos) {
                    std::string resp = handle(buf.substr(0, nl));
                    buf.erase(0, nl + 1);
                    if (resp.empty()) continue;
                    resp += '\n';
                    if (write(fd, resp.data(), resp.size()) < 0) break;
                }
            }
            close(fd);
        }
        close(listenFd);
    }

    // Run the TCP server on a detached background thread
    void startTcp(uint16_t port) {
        std::thread([this, port] { runTcp(port); }).detach();
    }

private:
    std::string name, version;
    std::vector<Tool> tools;

    static std::string rpcResult(const Json &id, Json result) {
        return Json::object().set("jsonrpc", "2.0").set("id", id).set("result", result).dump();
    }
    static std::string rpcError(const Json &id, int code, const std::string &message) {
        return Json::object().set("jsonrpc", "2.0").set("id", id)
            .set("error", Json::object().set("code", code).set("message", message)).dump();
    }
};

} // namespace mcp
