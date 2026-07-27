// debugger.h - MCP debug server exposing emulator-native operations (run control,
// key input, memory/register dumps, MMU-translated reads) over local TCP.
// Enabled when B3_MCP_PORT is set; connect with `socat STDIO TCP:127.0.0.1:<port>`.

#pragma once

#include <chrono>
#include <thread>

#include "../mcp/mcp.h"
#include "b3_frame.h"
#include "../core/core.h"

class B3Debugger {
public:
    B3Debugger(b3Frame *frame): frame(frame), server("3beans-debug", "0.1.0") { buildTools(); }

    void start(uint16_t port) { server.startTcp(port); }

private:
    b3Frame *frame;
    mcp::Server server;

    static const char *cpuName(int id) {
        static const char *names[MAX_CPUS] = { "ARM11A", "ARM11B", "ARM11C", "ARM11D", "ARM9" };
        return (id >= 0 && id < MAX_CPUS) ? names[id] : "?";
    }

    static const char *keyName(int i) {
        static const char *names[] = { "A", "B", "Select", "Start", "Right", "Left", "Up", "Down", "R", "L", "X", "Y" };
        return (i >= 0 && i < 12) ? names[i] : "?";
    }

    static int keyIndex(const std::string &name) {
        for (int i = 0; i < 12; i++)
            if (name == keyName(i)) return i;
        return -1;
    }

    // Parse a "keys" argument that is either an array of names or a comma-separated string
    static uint32_t parseKeys(const mcp::Json &keys, std::string &bad) {
        uint32_t mask = 0;
        auto add = [&](const std::string &name) {
            int i = keyIndex(name);
            if (i < 0) bad = name; else mask |= (1u << i);
        };
        if (keys.getType() == mcp::Json::ARR) {
            for (size_t i = 0; i < keys.size(); i++) add(keys.at(i).toStr());
        }
        else {
            std::string s = keys.toStr(), tok;
            for (char c : s + ",") {
                if (c == ',' || c == ' ') { if (!tok.empty()) add(tok), tok.clear(); }
                else tok += c;
            }
        }
        return mask;
    }

    std::string heldList() {
        uint32_t held = frame->heldKeys.load();
        std::string out;
        for (int i = 0; i < 12; i++)
            if (held & (1u << i)) out += (out.empty() ? "" : ",") + std::string(keyName(i));
        return out.empty() ? "(none)" : out;
    }

    // Physical RAM ranges that are safe to read without MMIO side effects
    static bool safePhys(uint32_t addr, uint32_t len) {
        struct { uint32_t start, end; } ranges[] = {
            { 0x08000000, 0x08180000 }, // ARM9 internal RAM
            { 0x18000000, 0x18600000 }, // VRAM
            { 0x1FF00000, 0x20000000 }, // DSP/AXI WRAM
            { 0x20000000, 0x30000000 }, // FCRAM (+N3DS extension)
        };
        for (auto &r : ranges)
            if (addr >= r.start && len <= r.end - addr) return true;
        return false;
    }

    // ARMv6 short-descriptor table walk using physical reads (no CPU side effects)
    bool virtToPhys(Core *core, int cpu, uint32_t vaddr, uint32_t &paddr) {
        if (cpu >= ARM9) return false; // ARM9 has no MMU
        uint32_t ttbcr = core->cp15.readReg((CpuId)cpu, 2, 0, 2) & 7;
        uint32_t ttbr = core->cp15.readReg((CpuId)cpu, 2, 0, 0);
        if (ttbcr && (vaddr >> (32 - ttbcr)))
            ttbr = core->cp15.readReg((CpuId)cpu, 2, 0, 1);
        uint32_t l1Addr = (ttbr & ~0x3FFF) | (((vaddr >> 20) & 0xFFF) << 2);
        if (!safePhys(l1Addr, 4)) return false;
        uint32_t l1 = core->memory.read<uint32_t>((CpuId)cpu, l1Addr);
        switch (l1 & 3) {
            case 2: // Section or supersection
                if (l1 & BIT(18))
                    paddr = (l1 & 0xFF000000) | (vaddr & 0xFFFFFF);
                else
                    paddr = (l1 & 0xFFF00000) | (vaddr & 0xFFFFF);
                return true;
            case 1: { // Coarse page table
                uint32_t l2Addr = (l1 & ~0x3FF) | (((vaddr >> 12) & 0xFF) << 2);
                if (!safePhys(l2Addr, 4)) return false;
                uint32_t l2 = core->memory.read<uint32_t>((CpuId)cpu, l2Addr);
                if ((l2 & 3) == 1) { // 64KB large page
                    paddr = (l2 & 0xFFFF0000) | (vaddr & 0xFFFF);
                    return true;
                }
                if (l2 & 2) { // 4KB small page
                    paddr = (l2 & 0xFFFFF000) | (vaddr & 0xFFF);
                    return true;
                }
                return false;
            }
            default:
                return false;
        }
    }

    // Hex dump lines: "address: 16 hex bytes  ascii"
    std::string hexDump(Core *core, int cpu, uint32_t addr, uint32_t len, bool virt, std::string &error) {
        std::string out;
        char buf[16];
        for (uint32_t i = 0; i < len; i += 16) {
            snprintf(buf, sizeof(buf), "%08x: ", addr + i);
            out += buf;
            std::string ascii;
            for (uint32_t k = 0; k < 16 && i + k < len; k++) {
                uint32_t a = addr + i + k, pa = a;
                if (virt && !virtToPhys(core, cpu, a, pa)) {
                    error = "unmapped virtual address";
                    return out;
                }
                if (!safePhys(pa, 1)) {
                    error = "address outside RAM (use unsafe:true for MMIO)";
                    return out;
                }
                uint8_t b = core->memory.read<uint8_t>((CpuId)cpu, pa);
                snprintf(buf, sizeof(buf), "%02x ", b);
                out += buf;
                ascii += (b >= 0x20 && b < 0x7F) ? (char)b : '.';
            }
            out += ' ' + ascii + '\n';
        }
        return out;
    }

    // Run fn with the core locked; reports an error if no core is booted
    mcp::ToolResult withCore(std::function<mcp::ToolResult(Core*)> fn) {
        std::lock_guard<std::mutex> lock(frame->mutex);
        if (!frame->core) return mcp::ToolResult::err("no core booted (use emu_start)");
        return fn(frame->core);
    }

    void buildTools() {
        const char *noArgs = "{\"type\":\"object\",\"properties\":{}}";

        server.tool("emu_status", "Get emulator state: running/paused, FPS, and PC/CPSR of each CPU core.",
            noArgs, [this](const mcp::Json&) {
            return withCore([this](Core *core) {
                std::string out = frame->running.load() ? "running" : "stopped";
                if (frame->dbgPause.load()) out += " (debug-paused)";
                out += ", fps=" + std::to_string(core->fps);
                out += core->n3dsMode ? ", New3DS mode\n" : ", Old3DS mode\n";
                char buf[96];
                int cpus = core->n3dsMode ? MAX_CPUS : 3;
                for (int i = 0; i < cpus; i++) {
                    if (!core->arms[i].registers[15]) continue;
                    snprintf(buf, sizeof(buf), "%s: pc=%08x cpsr=%08x halted=%d\n", cpuName(i),
                        *core->arms[i].registers[15], core->arms[i].cpsr, core->arms[i].halted);
                    out += buf;
                }
                return mcp::ToolResult::ok(out);
            });
        });

        server.tool("emu_faults", "List MMU aborts and unimplemented-opcode events, with the faulting instruction, "
            "the address and the ARMv6 fault status. Pass first=true for the earliest events rather than the most "
            "recent ones, which is what you want when a fault is storming in a loop.",
            "{\"type\":\"object\",\"properties\":{\"limit\":{\"type\":\"integer\",\"default\":40},"
            "\"first\":{\"type\":\"boolean\",\"default\":false}}}",
            [this](const mcp::Json &args) {
            int limit = (int)args["limit"].toInt(40);
            bool first = args["first"].toBool(false);
            return withCore([&](Core *core) {
                uint32_t total = core->faultCount.load();
                if (!total) return mcp::ToolResult::ok("no faults recorded");
                uint32_t stored = total < Core::MAX_FAULTS ? total : Core::MAX_FAULTS;
                int show = (int)stored < limit ? (int)stored : limit;
                std::string out = "total=" + std::to_string(total) +
                    (first ? " showing first " : " showing last ") + std::to_string(show) + ":\n";
                static const char *kinds[] = { "unkARM", "unkTHUMB", "dataAbort", "prefAbort" };
                static const char *statuses[16] = { "", "align", "", "align", "", "translation-section", "",
                    "translation-page", "", "domain-section", "", "domain-page", "", "permission-section", "",
                    "permission-page" };
                char buf[128];
                for (int i = 0; i < show; i++) {
                    Core::Fault f = first ? core->firstFaults[i] : core->faults[(total - 1 - i) % Core::MAX_FAULTS];
                    snprintf(buf, sizeof(buf), "%-9s cpu%d pc=%08x addr=%08x %s\n", f.kind < 4 ? kinds[f.kind] : "?",
                        f.cpu, f.pc, f.addr, statuses[f.status & 0xF]);
                    out += buf;
                }
                return mcp::ToolResult::ok(out);
            });
        });

        server.tool("emu_cp15", "Dump a CPU's MMU state: control register, translation table bases, domain access "
            "control, and the data and instruction fault status and address registers.",
            "{\"type\":\"object\",\"properties\":{\"cpu\":{\"type\":\"integer\",\"default\":0}}}",
            [this](const mcp::Json &args) {
            int cpu = (int)args["cpu"].toInt(0);
            if (cpu < 0 || cpu >= ARM9) return mcp::ToolResult::err("bad cpu id (ARM11 only)");
            return withCore([&](Core *core) {
                static const struct { const char *name; uint8_t cn, cm, cp; } regs[] = {
                    { "SCTLR", 1, 0, 0 }, { "TTBR0", 2, 0, 0 }, { "TTBR1", 2, 0, 1 }, { "TTBCR", 2, 0, 2 },
                    { "DACR", 3, 0, 0 }, { "DFSR", 5, 0, 0 }, { "IFSR", 5, 0, 1 }, { "DFAR", 6, 0, 0 },
                    { "IFAR", 6, 0, 2 }
                };
                std::string out;
                char buf[64];
                for (const auto &r : regs) {
                    snprintf(buf, sizeof(buf), "%-5s = %08x\n", r.name, core->cp15.readReg((CpuId)cpu, r.cn, r.cm, r.cp));
                    out += buf;
                }
                return mcp::ToolResult::ok(out);
            });
        });

        server.tool("emu_find", "Search physical RAM for a byte pattern (ascii text= or hex bytes hex=, e.g. hex=deadbeef). "
            "Returns up to max hit addresses. Default range covers FCRAM.",
            "{\"type\":\"object\",\"properties\":{\"text\":{\"type\":\"string\"},\"hex\":{\"type\":\"string\"},"
            "\"start\":{\"type\":\"string\",\"default\":\"0x20000000\"},\"end\":{\"type\":\"string\",\"default\":\"0x28000000\"},"
            "\"max\":{\"type\":\"integer\",\"default\":16}}}",
            [this](const mcp::Json &args) {
            std::vector<uint8_t> pat;
            std::string text = args["text"].toStr(), hex = args["hex"].toStr();
            if (!text.empty()) pat.assign(text.begin(), text.end());
            else for (size_t i = 0; i + 1 < hex.size(); i += 2)
                pat.push_back((uint8_t)strtoul(hex.substr(i, 2).c_str(), nullptr, 16));
            if (pat.empty()) return mcp::ToolResult::err("give text= or hex=");
            uint32_t start = (uint32_t)strtoul(args["start"].toStr("0x20000000").c_str(), nullptr, 0);
            uint32_t end = (uint32_t)strtoul(args["end"].toStr("0x28000000").c_str(), nullptr, 0);
            int max = (int)args["max"].toInt(16);
            return withCore([&](Core *core) {
                std::string out;
                int hits = 0;
                std::vector<uint8_t> buf(0x1000 + pat.size());
                for (uint64_t base = start; base < end && hits < max; base += 0x1000) {
                    if (!safePhys((uint32_t)base, 1)) continue;
                    uint32_t len = 0x1000 + ((base + 0x1000 + pat.size() <= end) ? pat.size() : 0);
                    for (uint32_t i = 0; i < len; i++)
                        buf[i] = core->memory.read<uint8_t>(ARM11A, (uint32_t)base + i);
                    for (uint32_t i = 0; i + pat.size() <= len && hits < max; i++) {
                        if (memcmp(&buf[i], pat.data(), pat.size())) continue;
                        char line[16];
                        snprintf(line, sizeof(line), "%08x\n", (uint32_t)(base + i));
                        out += line;
                        hits++;
                    }
                }
                return mcp::ToolResult::ok(out.empty() ? "no hits" : out);
            });
        });

        server.tool("emu_trace_on", "Arm the ARM11A instruction-PC trace. It records every ARM11A PC and auto-freezes "
            "the moment execution enters the null page (pc<0x8000) in user mode - capturing the run-up to a jump-to-null.",
            noArgs, [this](const mcp::Json&) {
            Core::traceIdx = 0;
            Core::traceFrozen = false;
            Core::traceOn = true;
            return mcp::ToolResult::ok("trace armed (records ARM11A, freezes on null-page entry)");
        });

        server.tool("emu_trace", "Dump the ARM11A PC trace (oldest to newest). If frozen, the tail is the exact path "
            "into the null page. limit = how many of the most recent PCs to show.",
            "{\"type\":\"object\",\"properties\":{\"limit\":{\"type\":\"integer\",\"default\":64}}}",
            [this](const mcp::Json &args) {
            int limit = (int)args["limit"].toInt(64);
            uint32_t total = Core::traceIdx;
            if (!total) return mcp::ToolResult::err("trace empty (arm it with emu_trace_on before booting)");
            uint32_t stored = total < (uint32_t)Core::TRACE_N ? total : (uint32_t)Core::TRACE_N;
            int show = (int)stored < limit ? (int)stored : limit;
            std::string out = std::string(Core::traceFrozen ? "FROZEN (user-mode zero opcode)" : "live") +
                ", recorded=" + std::to_string(total) + ", showing last " + std::to_string(show) + ":\n";
            char buf[64];
            uint32_t prevCpsr = ~0u, prevCore = ~0u;
            for (int i = show - 1; i >= 0; i--) {
                uint32_t slot = ((total - 1 - i) & (Core::TRACE_N - 1)) * 3;
                uint32_t pc = Core::traceBuf[slot], cp = Core::traceBuf[slot + 1], cr = Core::traceBuf[slot + 2];
                // Annotate core/mode only when they change, to keep the dump compact
                if (cp != prevCpsr || cr != prevCore) {
                    snprintf(buf, sizeof(buf), "%08x  core%u cpsr=%08x mode=%02x %s\n",
                        pc, cr, cp, cp & 0x1F, (cp & BIT(5)) ? "THUMB" : "ARM");
                    prevCpsr = cp;
                    prevCore = cr;
                }
                else snprintf(buf, sizeof(buf), "%08x\n", pc);
                out += buf;
            }
            return mcp::ToolResult::ok(out);
        });

        server.tool("emu_start", "Boot the emulated 3DS from NAND. Optionally hold buttons from the very first "
            "cycle (e.g. hold:\"Down\" to make Luma3DS chainload down_*.firm) - these stay held until emu_unhold. "
            "This is the reliable way to hold a key across boot, since early HID polls happen before any live keypress.",
            "{\"type\":\"object\",\"properties\":{\"hold\":{\"type\":[\"string\",\"array\"],"
            "\"items\":{\"type\":\"string\"}}}}",
            [this](const mcp::Json &args) {
            std::string bad;
            if (!args["hold"].isNull()) {
                uint32_t mask = parseKeys(args["hold"], bad);
                if (!bad.empty()) return mcp::ToolResult::err("unknown key: " + bad);
                frame->heldKeys.store(mask);
            }
            frame->CallAfter([this] { frame->startCore(true); });
            return mcp::ToolResult::ok("boot requested, holding: " + heldList());
        });

        server.tool("emu_hold", "Add buttons to the persistent held set and press them now if running. "
            "Held buttons are re-applied automatically on every boot. Keys: A B Select Start Right Left Up Down R L X Y.",
            "{\"type\":\"object\",\"properties\":{\"keys\":{\"type\":[\"string\",\"array\"],"
            "\"items\":{\"type\":\"string\"}}},\"required\":[\"keys\"]}",
            [this](const mcp::Json &args) {
            std::string bad;
            uint32_t mask = parseKeys(args["keys"], bad);
            if (!bad.empty()) return mcp::ToolResult::err("unknown key: " + bad);
            frame->heldKeys.store(frame->heldKeys.load() | mask);
            for (int i = 0; i < 12; i++)
                if (mask & (1u << i)) frame->pressKey(i);
            return mcp::ToolResult::ok("holding: " + heldList());
        });

        server.tool("emu_unhold", "Remove buttons from the held set and release them (omit keys to clear all).",
            "{\"type\":\"object\",\"properties\":{\"keys\":{\"type\":[\"string\",\"array\"],"
            "\"items\":{\"type\":\"string\"}}}}",
            [this](const mcp::Json &args) {
            uint32_t mask = 0xFFF;
            if (!args["keys"].isNull()) {
                std::string bad;
                mask = parseKeys(args["keys"], bad);
                if (!bad.empty()) return mcp::ToolResult::err("unknown key: " + bad);
            }
            frame->heldKeys.store(frame->heldKeys.load() & ~mask);
            for (int i = 0; i < 12; i++)
                if (mask & (1u << i)) frame->releaseKey(i);
            return mcp::ToolResult::ok("holding: " + heldList());
        });

        server.tool("emu_stop", "Stop the emulation and destroy the core.",
            noArgs, [this](const mcp::Json&) {
            frame->CallAfter([this] { frame->stopCore(true); });
            return mcp::ToolResult::ok("stop requested");
        });

        server.tool("emu_pause", "Pause emulation (freezes CPUs; memory/register reads are then race-free).",
            noArgs, [this](const mcp::Json&) {
            frame->dbgPause.store(true);
            return mcp::ToolResult::ok("paused");
        });

        server.tool("emu_resume", "Resume emulation after emu_pause.",
            noArgs, [this](const mcp::Json&) {
            frame->dbgPause.store(false);
            return mcp::ToolResult::ok("resumed");
        });

        server.tool("emu_step", "While paused, run a number of frames then pause again.",
            "{\"type\":\"object\",\"properties\":{\"frames\":{\"type\":\"integer\",\"default\":1}}}",
            [this](const mcp::Json &args) {
            int frames = (int)args["frames"].toInt(1);
            if (!frame->dbgPause.load()) return mcp::ToolResult::err("not paused");
            frame->dbgStep.store(frames);
            while (frame->dbgStep.load() > 0 && frame->running.load())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return mcp::ToolResult::ok("stepped " + std::to_string(frames) + " frame(s)");
        });

        server.tool("emu_regs", "Dump registers r0-r15 and CPSR of a CPU (0=ARM11A 1=ARM11B 2=ARM9, 3dsMode order).",
            "{\"type\":\"object\",\"properties\":{\"cpu\":{\"type\":\"integer\",\"default\":0}}}",
            [this](const mcp::Json &args) {
            int cpu = (int)args["cpu"].toInt(0);
            if (cpu < 0 || cpu >= MAX_CPUS) return mcp::ToolResult::err("bad cpu id");
            return withCore([this, cpu](Core *core) {
                if (!core->arms[cpu].registers[0]) return mcp::ToolResult::err("cpu not initialized");
                std::string out = std::string(cpuName(cpu)) + ":\n";
                char buf[32];
                for (int i = 0; i < 16; i++) {
                    snprintf(buf, sizeof(buf), "r%-2d=%08x%s", i, *core->arms[cpu].registers[i],
                        (i % 4 == 3) ? "\n" : " ");
                    out += buf;
                }
                snprintf(buf, sizeof(buf), "cpsr=%08x\n", core->arms[cpu].cpsr);
                out += buf;
                return mcp::ToolResult::ok(out);
            });
        });

        server.tool("emu_read", "Hex-dump emulated memory. addr accepts decimal or hex string. "
            "virt=true translates through the CPU's MMU (ARM11 only); otherwise addr is physical. "
            "Safe RAM only unless unsafe=true (MMIO reads may have side effects).",
            "{\"type\":\"object\",\"properties\":{"
            "\"addr\":{\"type\":\"string\"},\"len\":{\"type\":\"integer\",\"default\":64},"
            "\"cpu\":{\"type\":\"integer\",\"default\":0},\"virt\":{\"type\":\"boolean\",\"default\":false},"
            "\"unsafe\":{\"type\":\"boolean\",\"default\":false}},\"required\":[\"addr\"]}",
            [this](const mcp::Json &args) {
            uint32_t addr = (uint32_t)strtoul(args["addr"].toStr("0").c_str(), nullptr, 0);
            uint32_t len = (uint32_t)args["len"].toInt(64);
            int cpu = (int)args["cpu"].toInt(0);
            bool virt = args["virt"].toBool(false);
            bool unsafe = args["unsafe"].toBool(false);
            if (len > 0x10000) return mcp::ToolResult::err("len too large (max 65536)");
            if (cpu < 0 || cpu >= MAX_CPUS) return mcp::ToolResult::err("bad cpu id");
            return withCore([&](Core *core) {
                if (unsafe && !virt) {
                    std::string out;
                    char buf[16];
                    for (uint32_t i = 0; i < len; i += 4) {
                        if (i % 16 == 0) {
                            snprintf(buf, sizeof(buf), "%s%08x: ", i ? "\n" : "", addr + i);
                            out += buf;
                        }
                        snprintf(buf, sizeof(buf), "%08x ", core->memory.read<uint32_t>((CpuId)cpu, addr + i));
                        out += buf;
                    }
                    return mcp::ToolResult::ok(out + "\n");
                }
                std::string error;
                std::string out = hexDump(core, cpu, addr, len, virt, error);
                return error.empty() ? mcp::ToolResult::ok(out) : mcp::ToolResult::err(error + "\n" + out);
            });
        });

        server.tool("emu_stack", "Dump memory around a CPU's current stack pointer (via MMU translation on ARM11).",
            "{\"type\":\"object\",\"properties\":{\"cpu\":{\"type\":\"integer\",\"default\":0},"
            "\"len\":{\"type\":\"integer\",\"default\":256}}}",
            [this](const mcp::Json &args) {
            int cpu = (int)args["cpu"].toInt(0);
            uint32_t len = (uint32_t)args["len"].toInt(256);
            if (cpu < 0 || cpu >= MAX_CPUS) return mcp::ToolResult::err("bad cpu id");
            if (len > 0x10000) return mcp::ToolResult::err("len too large");
            return withCore([&](Core *core) {
                if (!core->arms[cpu].registers[13]) return mcp::ToolResult::err("cpu not initialized");
                uint32_t sp = *core->arms[cpu].registers[13];
                char buf[48];
                snprintf(buf, sizeof(buf), "%s sp=%08x\n", cpuName(cpu), sp);
                std::string error;
                std::string out = buf + hexDump(core, cpu, sp, len, cpu < ARM9, error);
                return error.empty() ? mcp::ToolResult::ok(out) : mcp::ToolResult::err(error + "\n" + out);
            });
        });

        server.tool("emu_vtophys", "Translate an ARM11 virtual address to physical through the emulated MMU tables.",
            "{\"type\":\"object\",\"properties\":{\"addr\":{\"type\":\"string\"},"
            "\"cpu\":{\"type\":\"integer\",\"default\":0}},\"required\":[\"addr\"]}",
            [this](const mcp::Json &args) {
            uint32_t vaddr = (uint32_t)strtoul(args["addr"].toStr("0").c_str(), nullptr, 0);
            int cpu = (int)args["cpu"].toInt(0);
            return withCore([&](Core *core) {
                uint32_t paddr;
                if (!virtToPhys(core, cpu, vaddr, paddr)) return mcp::ToolResult::err("unmapped");
                char buf[32];
                snprintf(buf, sizeof(buf), "%08x -> %08x\n", vaddr, paddr);
                return mcp::ToolResult::ok(buf);
            });
        });

        server.tool("emu_key", "Press a 3DS key. action: tap (press+hold+release), press, or release. "
            "Keys: A B Select Start Right Left Up Down R L X Y. Input is only sampled while emulation runs.",
            "{\"type\":\"object\",\"properties\":{\"key\":{\"type\":\"string\"},"
            "\"action\":{\"type\":\"string\",\"default\":\"tap\"},"
            "\"hold_ms\":{\"type\":\"integer\",\"default\":150}},\"required\":[\"key\"]}",
            [this](const mcp::Json &args) {
            std::string key = args["key"].toStr();
            int idx = keyIndex(key);
            if (idx < 0) return mcp::ToolResult::err("unknown key: " + key);
            std::string action = args["action"].toStr("tap");
            int holdMs = (int)args["hold_ms"].toInt(150);
            if (action == "press" || action == "tap") frame->pressKey(idx);
            if (action == "tap") std::this_thread::sleep_for(std::chrono::milliseconds(holdMs));
            if (action == "release" || action == "tap") frame->releaseKey(idx);
            return mcp::ToolResult::ok(action + " " + key);
        });

        server.tool("emu_touch", "Touch the bottom screen at (x, y) in 0-319/0-239, or release with release=true.",
            "{\"type\":\"object\",\"properties\":{\"x\":{\"type\":\"integer\"},\"y\":{\"type\":\"integer\"},"
            "\"release\":{\"type\":\"boolean\",\"default\":false}}}",
            [this](const mcp::Json &args) {
            if (args["release"].toBool(false)) {
                frame->releaseScreen();
                return mcp::ToolResult::ok("touch released");
            }
            int x = (int)args["x"].toInt(0), y = (int)args["y"].toInt(0);
            frame->pressScreen(x, y);
            return mcp::ToolResult::ok("touching " + std::to_string(x) + "," + std::to_string(y));
        });
    }
};
