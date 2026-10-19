/*
 * debugger.cpp -- the h89debug engine (core/include/h89debug.h).
 *
 * Built on VirtualH89's Z80 hooks (core/v89/z80.h): the instruction hook,
 * called before every instruction with PC on the next opcode, and the I/O
 * and memory hooks, installed only while a breakpoint of that kind exists.
 * Attached, the instruction hook checks the stop request, the breakpoints
 * and the step mode; stopping PARKS the machine thread right there until a
 * window resumes it -- the CPU's own progress is what stops. Detached, no
 * hook is installed, so a session that never opens the debugger pays
 * nothing. The family's design (the Master System sibling's engine), over
 * this machine. The FujiNet trace (fujitrace.h) is the exception: the card
 * hands it its bytes from the session's start, attached or not.
 *
 * Threading. Everything public is called from UI threads; the hooks run on
 * the machine thread. Every public call that reads or edits the machine
 * holds the host's run lock (h89host_lock), which the machine thread holds
 * while it runs a batch and lets go of while it is parked here -- so a
 * window refreshing while the machine runs waits a moment instead of
 * tearing, and a stopped machine is simply the UI's. Lock order: the run
 * lock, then d->lock.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "debugger_internal.h"

#include "expr.h"
#include "fujitrace.h"
#include "z80dasm.h"

#include "FujiNetCard.h"
#include "GeneralPurposePort.h"
#include "H19Terminal.h"
#include "INS8250.h"
#include "IntrController.h"
#include "Machine.h"
#include "MemoryDecoder.h"
#include "SingleStep.h"
#include "h17.h"
#include "h37.h"
#include "h89-timer.h"
#include "host.h"
#include "mms77316.h"
#include "mms77320.h"
#include "z80.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

namespace
{

const int MAX_BP  = 128;
const int TRACE_N = 4096;

enum
{
    MODE_RUN = 0,
    MODE_STEP,
    MODE_OVER,
    MODE_OUT,
    MODE_RUNTO,
    MODE_TICKS,
};

}

struct h89debug
{
    std::mutex              lock;
    std::condition_variable cond;

    std::atomic<int> attached{0};
    std::atomic<int> stopped{0};       // the machine thread is parked
    std::atomic<int> stop_req{0};
    std::atomic<int> mode{MODE_RUN};
    long             count       = 0;
    uint16_t         target      = 0;
    uint16_t         sp_guard    = 0;
    uint64_t         target_tick = 0;
    int              was_attached = 0;  // across a power cycle

    char                  reason[128] = "";
    int                   reason_addr = -1;
    std::atomic<unsigned> generation{0};

    h89debug_breakpoint bp[MAX_BP];
    int                 nbp     = 0;
    int                 next_id = 1;
    uint8_t             map_exec[65536];
    uint8_t             map_mem[65536];   // H89DEBUG_BP_READ / _WRITE bits
    uint8_t             map_io[256];      // H89DEBUG_BP_IN / _OUT bits
    uint8_t             watch = 0;        // the kinds any breakpoint watches
    std::atomic<int>    break_pending{0};
    char                break_reason[128] = "";

    // the access a condition sees as `value` / `addr`
    long acc_value = 0, acc_addr = 0;
    int  in_access = 0;

    std::atomic<int> trace_on{0};
    h89debug_trace   ring[TRACE_N];
    int              ring_head = 0, ring_count = 0;

    FujiTrace fuji;   // its own lock: fed by the card hook, attached or not

    symtab           syms;
    std::atomic<int> radix{H89_RADIX_HEX};
};

namespace
{

Machine*
machine()
{
    return h89host_machine();
}

// The card's bytes (FujiNetCard::setTrace), on the machine thread.
void
fuji_tap(void* user, int dir, const uint8_t* buf, size_t len)
{
    static_cast<h89debug*>(user)->fuji.feed(dir, buf, len);
}

void
bump(h89debug* d)
{
    d->generation.fetch_add(1);
}

struct RunLock
{
    RunLock()
    {
        h89host_lock();
    }
    ~RunLock()
    {
        h89host_unlock();
    }
};

uint8_t
peek(Machine* m, uint16_t addr)
{
    return m->memory().readByte(addr);
}

Z80::Registers
regs(Machine* m)
{
    Z80::Registers r;
    m->cpu().getRegisters(r);
    return r;
}

int
decode_at(Machine* m, uint16_t pc, z80d_insn* ins, int radix = Z80D_RADIX_HEX)
{
    uint8_t code[4];
    for (int i = 0; i < 4; ++i)
    {
        code[i] = peek(m, (uint16_t) (pc + i));
    }
    return z80_disassemble_radix(ins, pc, code, radix);
}

int
dasm_radix(h89debug* d)
{
    switch (d->radix.load())
    {
        case H89_RADIX_OCTAL:
            return Z80D_RADIX_OCTAL;
        case H89_RADIX_SPLIT_OCTAL:
            return Z80D_RADIX_SPLIT_OCTAL;
        default:
            return Z80D_RADIX_HEX;
    }
}

const char*
fmt16(int radix, unsigned v, char* buf, int bufsz)
{
    v &= 0xffff;
    if (radix == H89_RADIX_SPLIT_OCTAL)
    {
        snprintf(buf, (size_t) bufsz, "%03o.%03o", v >> 8, v & 0xff);
    }
    else if (radix == H89_RADIX_OCTAL)
    {
        snprintf(buf, (size_t) bufsz, "%06oQ", v);
    }
    else
    {
        snprintf(buf, (size_t) bufsz, "%04X", v);
    }
    return buf;
}

const char*
fmt8(int radix, unsigned v, char* buf, int bufsz)
{
    v &= 0xff;
    if (radix == H89_RADIX_HEX)
    {
        snprintf(buf, (size_t) bufsz, "%02X", v);
    }
    else
    {
        snprintf(buf, (size_t) bufsz, "%03o", v);
    }
    return buf;
}

} // namespace

/* ---- expressions ------------------------------------------------------------------ */

extern "C" const char* const h89debug_reg_names[] = {
    "a",  "f",  "b",   "c",   "d",   "e",   "h",   "l",   "af",  "bc",     "de",
    "hl", "ix", "iy",  "sp",  "pc",  "i",   "r",   "im",  "iff1", "iff2", "af'",
    "bc'", "de'", "hl'", "cf", "nf", "pf", "hf", "zf", "sf", "cycles", "ticks",
    "value", "addr", nullptr,
};

namespace
{

uint8_t
expr_read(void* user, uint16_t addr)
{
    Machine* m = machine();
    (void) user;
    return m ? peek(m, addr) : 0xff;
}

int
expr_name(void* user, const char* name, long* out)
{
    h89debug* d = static_cast<h89debug*>(user);
    Machine*  m = machine();

    if (!strcmp(name, "value") && d->in_access)
    {
        *out = d->acc_value;
        return 1;
    }
    if (!strcmp(name, "addr") && d->in_access)
    {
        *out = d->acc_addr;
        return 1;
    }
    if (m)
    {
        Z80::Registers r = regs(m);
        const uint8_t  f = r.af & 0xff;
        struct
        {
            const char* n;
            long        v;
        } t[] = {
            {"a", r.af >> 8}, {"f", f}, {"b", r.bc >> 8}, {"c", r.bc & 0xff},
            {"d", r.de >> 8}, {"e", r.de & 0xff}, {"h", r.hl >> 8}, {"l", r.hl & 0xff},
            {"af", r.af}, {"bc", r.bc}, {"de", r.de}, {"hl", r.hl}, {"ix", r.ix}, {"iy", r.iy},
            {"sp", r.sp}, {"pc", r.pc}, {"i", r.i}, {"r", r.r}, {"im", r.im},
            {"iff1", r.iff1}, {"iff2", r.iff2}, {"af'", r.af2}, {"bc'", r.bc2},
            {"de'", r.de2}, {"hl'", r.hl2}, {"cf", f & 1}, {"nf", (f >> 1) & 1},
            {"pf", (f >> 2) & 1}, {"hf", (f >> 4) & 1}, {"zf", (f >> 6) & 1},
            {"sf", (f >> 7) & 1}, {"cycles", (long) m->cpu().totalCycles()},
            {"ticks", (long) m->slices()},
        };
        for (const auto& e : t)
        {
            if (!strcmp(name, e.n))
            {
                *out = e.v;
                return 1;
            }
        }
    }
    int a = symtab_find(&d->syms, name);
    if (a >= 0)
    {
        *out = a;
        return 1;
    }
    return 0;
}

} // namespace

extern "C" int
h89debug_eval(h89debug* d, const char* text, long* out, char* err, int errsz)
{
    expr_ctx ctx = {expr_name, expr_read, d};
    RunLock  l;
    return expr_eval(text, &ctx, out, err, errsz);
}

/* ---- the hooks (machine thread) ---------------------------------------------------- */

namespace
{

void
trace_record(h89debug* d, Machine* m, uint16_t pc)
{
    h89debug_trace* t = &d->ring[d->ring_head];
    Z80::Registers  r = regs(m);
    t->pc             = pc;
    t->sp             = r.sp;
    t->af             = r.af;
    t->bc             = r.bc;
    t->de             = r.de;
    t->hl             = r.hl;
    t->cycles         = m->cpu().totalCycles();
    d->ring_head      = (d->ring_head + 1) % TRACE_N;
    if (d->ring_count < TRACE_N)
    {
        d->ring_count++;
    }
}

int
cond_true(h89debug* d, const h89debug_breakpoint* b)
{
    long v = 0;
    char err[64];
    if (!b->condition[0])
    {
        return 1;
    }
    if (h89debug_eval(d, b->condition, &v, err, sizeof err) != 0)
    {
        return 1;   // a condition that no longer evaluates stops, rather than hides
    }
    return v != 0;
}

/* Park until a window resumes. On the machine thread, with the run lock
 * held and d->lock not; lets go of the run lock while parked. */
void
park(h89debug* d, uint16_t pc, const char* reason)
{
    {
        std::unique_lock<std::mutex> lk(d->lock);
        snprintf(d->reason, sizeof d->reason, "%s", reason);
        d->reason_addr = pc;
        d->mode.store(MODE_RUN);
        d->stop_req.store(0);
        d->stopped.store(1);
        bump(d);
        // parked, the machine is the windows'
        h89host_park_release();
        d->cond.wait(lk, [d] { return !d->stopped.load() || !d->attached.load(); });
        d->stopped.store(0);
    }
    h89host_park_reacquire();
}

void
instr_hook(void* user, WORD pc)
{
    h89debug* d = static_cast<h89debug*>(user);
    Machine*  m = machine();
    char      why[160];

    if (!m)
    {
        return;
    }
    if (d->trace_on.load(std::memory_order_relaxed))
    {
        trace_record(d, m, pc);
    }
    if (d->break_pending.load())
    {
        d->break_pending.store(0);
        {
            std::lock_guard<std::mutex> lk(d->lock);
            snprintf(why, sizeof why, "%s", d->break_reason);
        }
        park(d, pc, why);
        return;
    }
    if (d->stop_req.load())
    {
        park(d, pc, "stopped");
        return;
    }
    if (d->map_exec[pc])
    {
        int hit = 0;
        {
            std::lock_guard<std::mutex> lk(d->lock);
            for (int i = 0; i < d->nbp; ++i)
            {
                h89debug_breakpoint* b = &d->bp[i];
                if (b->enabled && (b->type & H89DEBUG_BP_EXEC) && pc >= b->start && pc <= b->end &&
                    cond_true(d, b))
                {
                    b->hits++;
                    hit = 1;
                    break;
                }
            }
        }
        if (hit)
        {
            char a[16];
            snprintf(why, sizeof why, "breakpoint: exec %s", fmt16(d->radix.load(), pc, a, sizeof a));
            park(d, pc, why);
            return;
        }
    }

    switch (d->mode.load())
    {
        case MODE_STEP:
            if (--d->count <= 0)
            {
                park(d, pc, "step");
            }
            break;
        case MODE_OVER:
            if (pc == d->target && regs(m).sp >= d->sp_guard)
            {
                park(d, pc, "step over");
            }
            break;
        case MODE_OUT:
            if (pc == d->target && regs(m).sp > d->sp_guard)
            {
                park(d, pc, "step out");
            }
            break;
        case MODE_RUNTO:
            if (pc == d->target)
            {
                park(d, pc, "run to");
            }
            break;
        case MODE_TICKS:
            if (m->slices() >= d->target_tick)
            {
                park(d, pc, "ticks");
            }
            break;
        default:
            break;
    }
}

void
access(h89debug* d, int type, unsigned addr, uint8_t data, bool io)
{
    int bits = io ? d->map_io[addr & 0xff] : d->map_mem[addr & 0xffff];
    if (!(bits & type) || d->break_pending.load())
    {
        return;
    }
    std::lock_guard<std::mutex> lk(d->lock);
    d->acc_value = data;
    d->acc_addr  = addr;
    d->in_access = 1;
    for (int i = 0; i < d->nbp; ++i)
    {
        h89debug_breakpoint* b = &d->bp[i];
        if (b->enabled && (b->type & type) && addr >= b->start && addr <= b->end && cond_true(d, b))
        {
            const int r = d->radix.load();
            char      a[16], v[16];
            b->hits++;
            if (io)
            {
                snprintf(d->break_reason, sizeof d->break_reason, "%s %s %s %s",
                         type == H89DEBUG_BP_IN ? "in" : "out", fmt8(r, addr, a, sizeof a),
                         type == H89DEBUG_BP_IN ? "->" : "<-", fmt8(r, data, v, sizeof v));
            }
            else
            {
                snprintf(d->break_reason, sizeof d->break_reason, "%s %s = %s",
                         type == H89DEBUG_BP_READ ? "read" : "write", fmt16(r, addr, a, sizeof a),
                         fmt8(r, data, v, sizeof v));
            }
            d->break_pending.store(1);
            break;
        }
    }
    d->in_access = 0;
}

void
io_hook(void* user, bool out, BYTE port, BYTE data)
{
    access(static_cast<h89debug*>(user), out ? H89DEBUG_BP_OUT : H89DEBUG_BP_IN, port, data, true);
}

void
mem_hook(void* user, bool write, WORD addr, BYTE data)
{
    access(static_cast<h89debug*>(user), write ? H89DEBUG_BP_WRITE : H89DEBUG_BP_READ, addr, data,
           false);
}

/* The hooks for what the breakpoints watch. Run lock held. */
void
hook(h89debug* d)
{
    Machine* m = machine();
    if (!m)
    {
        return;
    }
    m->cpu().setInstrHook(instr_hook, d);
    m->cpu().setIoHook((d->watch & (H89DEBUG_BP_IN | H89DEBUG_BP_OUT)) ? io_hook : nullptr, d);
    m->cpu().setMemHook((d->watch & (H89DEBUG_BP_READ | H89DEBUG_BP_WRITE)) ? mem_hook : nullptr, d);
}

void
unhook()
{
    Machine* m = machine();
    if (!m)
    {
        return;
    }
    m->cpu().setInstrHook(nullptr, nullptr);
    m->cpu().setIoHook(nullptr, nullptr);
    m->cpu().setMemHook(nullptr, nullptr);
}

/* Rebuild the lookup maps and the hooks. Run lock and d->lock held. */
void
rebuild_locked(h89debug* d)
{
    memset(d->map_exec, 0, sizeof d->map_exec);
    memset(d->map_mem, 0, sizeof d->map_mem);
    memset(d->map_io, 0, sizeof d->map_io);
    d->watch = 0;
    for (int i = 0; i < d->nbp; ++i)
    {
        const h89debug_breakpoint* b = &d->bp[i];
        if (!b->enabled)
        {
            continue;
        }
        d->watch |= (uint8_t) b->type;
        for (unsigned a = b->start; a <= b->end && a <= 0xffff; ++a)
        {
            if (b->type & H89DEBUG_BP_EXEC)
            {
                d->map_exec[a] = 1;
            }
            if (b->type & (H89DEBUG_BP_READ | H89DEBUG_BP_WRITE))
            {
                d->map_mem[a] |= (uint8_t) (b->type & (H89DEBUG_BP_READ | H89DEBUG_BP_WRITE));
            }
            if ((b->type & (H89DEBUG_BP_IN | H89DEBUG_BP_OUT)) && a < 256)
            {
                d->map_io[a] |= (uint8_t) (b->type & (H89DEBUG_BP_IN | H89DEBUG_BP_OUT));
            }
        }
    }
    if (d->attached.load())
    {
        hook(d);
    }
}

void
go(h89debug* d, int mode)
{
    std::lock_guard<std::mutex> lk(d->lock);
    d->mode.store(mode);
    d->stop_req.store(0);
    d->stopped.store(0);
    d->cond.notify_all();
    bump(d);
}

} // namespace

/* ---- lifecycle ---------------------------------------------------------------------- */

extern "C" h89debug*
h89debug_create(void)
{
    h89debug* d = new h89debug();
    memset(d->bp, 0, sizeof d->bp);
    memset(d->map_exec, 0, sizeof d->map_exec);
    memset(d->map_mem, 0, sizeof d->map_mem);
    memset(d->map_io, 0, sizeof d->map_io);
    symtab_init(&d->syms);
    return d;
}

extern "C" void
h89debug_free(h89debug* d)
{
    if (!d)
    {
        return;
    }
    h89debug_detach(d);
    FujiNetCard::unsetTrace(d);
    symtab_free(&d->syms);
    delete d;
}

extern "C" void
h89debug_fujinet_install(h89debug* d)
{
    if (d)
    {
        FujiNetCard::setTrace(fuji_tap, d);
    }
}

extern "C" void
h89debug_attach(h89debug* d)
{
    if (!d)
    {
        return;
    }
    RunLock                     l;
    std::lock_guard<std::mutex> lk(d->lock);
    if (!d->attached.load())
    {
        d->attached.store(1);
        d->stop_req.store(1);
        d->mode.store(MODE_RUN);
        hook(d);
    }
    bump(d);
}

extern "C" void
h89debug_detach(h89debug* d)
{
    if (!d)
    {
        return;
    }
    {
        RunLock                     l;
        std::lock_guard<std::mutex> lk(d->lock);
        d->attached.store(0);
        d->stop_req.store(0);
        d->mode.store(MODE_RUN);
        unhook();
        d->cond.notify_all();
        bump(d);
    }
    // wait for the parked thread to leave the hook
    for (int i = 0; i < 2000 && d->stopped.load(); ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

extern "C" int
h89debug_is_attached(h89debug* d)
{
    return d && d->attached.load();
}

extern "C" void
h89debug_power_down(h89debug* d)
{
    if (!d)
    {
        return;
    }
    d->was_attached = d->attached.load();
    h89debug_detach(d);
}

extern "C" void
h89debug_power_up(h89debug* d)
{
    if (!d)
    {
        return;
    }
    // attached across the power cycle, or attached before there was a
    // machine to hook (a window opened before the session started)
    const int want = d->was_attached || d->attached.load();
    d->attached.store(0);
    if (want)
    {
        h89debug_attach(d);
    }
    d->was_attached = 0;
}

extern "C" symtab*
h89debug_symbols(h89debug* d)
{
    return &d->syms;
}

extern "C" int
h89debug_is_stopped(h89debug* d)
{
    return d && d->stopped.load();
}

extern "C" void
h89debug_stop(h89debug* d)
{
    if (!d)
    {
        return;
    }
    if (!d->attached.load())
    {
        h89debug_attach(d);
    }
    d->stop_req.store(1);
}

extern "C" void
h89debug_resume(h89debug* d)
{
    if (d && d->stopped.load())
    {
        go(d, MODE_RUN);
    }
}

extern "C" int
h89debug_stop_reason(h89debug* d, char* dst, int dstsz, int* address)
{
    if (!d || !dst || dstsz <= 0)
    {
        return 0;
    }
    std::lock_guard<std::mutex> lk(d->lock);
    const bool stopped = d->stopped.load();
    int        n       = snprintf(dst, (size_t) dstsz, "%s", stopped ? d->reason : "running");
    if (address)
    {
        *address = stopped ? d->reason_addr : -1;
    }
    return n;
}

extern "C" unsigned
h89debug_generation(h89debug* d)
{
    return d ? d->generation.load() : 0;
}

/* ---- the radix ------------------------------------------------------------------------- */

extern "C" int
h89debug_radix(h89debug* d)
{
    return d ? d->radix.load() : H89_RADIX_HEX;
}

extern "C" void
h89debug_set_radix(h89debug* d, int radix)
{
    if (!d || radix < H89_RADIX_HEX || radix > H89_RADIX_SPLIT_OCTAL)
    {
        return;
    }
    d->radix.store(radix);
    bump(d);
}

extern "C" const char*
h89debug_fmt16(h89debug* d, unsigned value, char* buf, int bufsz)
{
    return fmt16(d ? d->radix.load() : H89_RADIX_HEX, value, buf, bufsz);
}

extern "C" const char*
h89debug_fmt8(h89debug* d, unsigned value, char* buf, int bufsz)
{
    return fmt8(d ? d->radix.load() : H89_RADIX_HEX, value, buf, bufsz);
}

/* ---- stepping ---------------------------------------------------------------------------- */

extern "C" void
h89debug_step(h89debug* d)
{
    if (!d || !d->stopped.load())
    {
        return;
    }
    d->count = 1;
    go(d, MODE_STEP);
}

extern "C" void
h89debug_step_over(h89debug* d)
{
    int over = 0;
    if (!d)
    {
        return;
    }
    {
        RunLock  l;
        Machine* m = machine();
        if (m && d->stopped.load())
        {
            z80d_insn            ins;
            const Z80::Registers r = regs(m);
            decode_at(m, r.pc, &ins);
            if ((ins.flags & (Z80D_CALL | Z80D_BLOCK | Z80D_HALT)) ||
                ((ins.flags & Z80D_RELATIVE) && (ins.flags & Z80D_COND) && ins.bytes[0] == 0x10))
            {
                // CALL/RST, the block repeats, HALT and DJNZ: to the next one
                d->target   = (uint16_t) (r.pc + ins.len);
                d->sp_guard = r.sp;
                over        = 1;
            }
        }
    }
    if (over)
    {
        go(d, MODE_OVER);
    }
    else
    {
        h89debug_step(d);
    }
}

extern "C" void
h89debug_step_out(h89debug* d)
{
    int ok = 0;
    if (!d)
    {
        return;
    }
    {
        RunLock  l;
        Machine* m = machine();
        if (m && d->stopped.load())
        {
            // to the return address the CALL pushed, with an SP-depth guard
            const Z80::Registers r = regs(m);
            d->target   = (uint16_t) (peek(m, r.sp) | (peek(m, (uint16_t) (r.sp + 1)) << 8));
            d->sp_guard = r.sp;
            ok          = 1;
        }
    }
    if (ok)
    {
        go(d, MODE_OUT);
    }
}

extern "C" void
h89debug_ticks(h89debug* d, int n)
{
    int ok = 0;
    if (!d)
    {
        return;
    }
    {
        RunLock  l;
        Machine* m = machine();
        if (m && d->stopped.load())
        {
            d->target_tick = m->slices() + (uint64_t) (n > 0 ? n : 1);
            ok             = 1;
        }
    }
    if (ok)
    {
        go(d, MODE_TICKS);
    }
}

extern "C" void
h89debug_run_to(h89debug* d, uint16_t addr)
{
    if (!d || !d->stopped.load())
    {
        return;
    }
    d->target = addr;
    go(d, MODE_RUNTO);
}

/* ---- CPU ---------------------------------------------------------------------------------- */

extern "C" void
h89debug_cpu_get(h89debug* d, h89debug_cpu* o)
{
    (void) d;
    memset(o, 0, sizeof *o);
    RunLock  l;
    Machine* m = machine();
    if (!m)
    {
        return;
    }
    const Z80::Registers r = regs(m);
    const int            f = r.af & 0xff;
    o->pc     = r.pc;
    o->sp     = r.sp;
    o->af     = r.af;
    o->bc     = r.bc;
    o->de     = r.de;
    o->hl     = r.hl;
    o->ix     = r.ix;
    o->iy     = r.iy;
    o->wz     = r.wz;
    o->af2    = r.af2;
    o->bc2    = r.bc2;
    o->de2    = r.de2;
    o->hl2    = r.hl2;
    o->i      = r.i;
    o->r      = r.r;
    o->im     = r.im;
    o->iff1   = r.iff1;
    o->iff2   = r.iff2;
    o->halted = r.halted;
    o->sf     = (f >> 7) & 1;
    o->zf     = (f >> 6) & 1;
    o->yf     = (f >> 5) & 1;
    o->hf     = (f >> 4) & 1;
    o->xf     = (f >> 3) & 1;
    o->pf     = (f >> 2) & 1;
    o->nf     = (f >> 1) & 1;
    o->cf     = f & 1;
    o->cycles = m->cpu().totalCycles();
    o->ticks  = m->slices();
}

extern "C" void
h89debug_cpu_set(h89debug* d, int reg, int value)
{
    RunLock  l;
    Machine* m = machine();
    if (!m)
    {
        return;
    }
    Z80::Registers r  = regs(m);
    const WORD     v  = (WORD) value;
    const BYTE     b  = (BYTE) value;
    auto           hi = [b](WORD& w) { w = (WORD) ((w & 0x00ff) | (b << 8)); };
    auto           lo = [b](WORD& w) { w = (WORD) ((w & 0xff00) | b); };
    auto flag         = [&](int bit) {
        r.af = (WORD) (value ? (r.af | (1 << bit)) : (r.af & ~(1 << bit)));
    };
    switch (reg)
    {
        case H89_REG_PC: r.pc = v; break;
        case H89_REG_SP: r.sp = v; break;
        case H89_REG_AF: r.af = v; break;
        case H89_REG_BC: r.bc = v; break;
        case H89_REG_DE: r.de = v; break;
        case H89_REG_HL: r.hl = v; break;
        case H89_REG_IX: r.ix = v; break;
        case H89_REG_IY: r.iy = v; break;
        case H89_REG_AF2: r.af2 = v; break;
        case H89_REG_BC2: r.bc2 = v; break;
        case H89_REG_DE2: r.de2 = v; break;
        case H89_REG_HL2: r.hl2 = v; break;
        case H89_REG_I: r.i = b; break;
        case H89_REG_R: r.r = b; break;
        case H89_REG_IM: r.im = (BYTE) (value & 3); break;
        case H89_REG_IFF1: r.iff1 = value != 0; break;
        case H89_REG_IFF2: r.iff2 = value != 0; break;
        case H89_REG_A: hi(r.af); break;
        case H89_REG_F: lo(r.af); break;
        case H89_REG_B: hi(r.bc); break;
        case H89_REG_C: lo(r.bc); break;
        case H89_REG_D: hi(r.de); break;
        case H89_REG_E: lo(r.de); break;
        case H89_REG_H: hi(r.hl); break;
        case H89_REG_L: lo(r.hl); break;
        case H89_FLAG_S: flag(7); break;
        case H89_FLAG_Z: flag(6); break;
        case H89_FLAG_H: flag(4); break;
        case H89_FLAG_PV: flag(2); break;
        case H89_FLAG_N: flag(1); break;
        case H89_FLAG_C: flag(0); break;
        default: return;
    }
    m->cpu().setRegisters(r);
    if (d)
    {
        bump(d);
    }
}

/* ---- memory ---------------------------------------------------------------------------------- */

extern "C" int
h89debug_read(h89debug* d, uint16_t addr, uint8_t* dst, int n)
{
    (void) d;
    RunLock  l;
    Machine* m = machine();
    for (int i = 0; i < n; ++i)
    {
        dst[i] = m ? peek(m, (uint16_t) (addr + i)) : 0xff;
    }
    return m ? n : 0;
}

extern "C" void
h89debug_write(h89debug* d, uint16_t addr, uint8_t value)
{
    RunLock  l;
    Machine* m = machine();
    if (m)
    {
        m->memory().writeByte(addr, value);
        if (d)
        {
            bump(d);
        }
    }
}

/* ---- the H89's parts ---------------------------------------------------------------------------- */

namespace
{

struct Out
{
    char* dst;
    int   cap;
    int   len = 0;
    void  operator()(const char* fmt, ...)
    {
        va_list ap;
        if (len >= cap - 1)
        {
            return;
        }
        va_start(ap, fmt);
        int n = vsnprintf(dst + len, (size_t) (cap - len), fmt, ap);
        va_end(ap);
        if (n > 0)
        {
            len += n < cap - len ? n : cap - len - 1;
        }
    }
};

void
drives_of(Out& o, int ctrl)
{
    const int n = h89drive_count();
    for (int i = 0; i < n; ++i)
    {
        h89_drive_info info;
        if (h89drive_info_at(i, &info) != 0 || info.ctrl != ctrl)
        {
            continue;
        }
        o("  %-28s %s%s%s%s\n", info.label, info.loaded ? info.path : "(empty)",
          info.loaded ? (info.write_protected ? "  [protected]" : "") : "",
          info.dirty ? "  [unsaved]" : "", info.active ? "  [busy]" : "");
    }
}

const char* const kSectionNames[H89DEBUG_SECTION_COUNT] = {
    "Terminal", "Memory", "Timer & Interrupts", "H17", "Floppy Controllers", "SASI",
    "Serial Ports", "FujiNet Card",
};

} // namespace

extern "C" const char*
h89debug_section_name(int section)
{
    return section >= 0 && section < H89DEBUG_SECTION_COUNT ? kSectionNames[section] : nullptr;
}

extern "C" int
h89debug_section(h89debug* d, int section, char* dst, int dstsz)
{
    Out o{dst, dstsz};
    if (!dst || dstsz <= 0)
    {
        return 0;
    }
    dst[0] = '\0';
    RunLock  l;
    Machine* m = machine();
    if (!m)
    {
        o("(the machine is off)\n");
        return o.len;
    }
    const int          rx = d ? d->radix.load() : H89_RADIX_HEX;
    const h89_config&  c  = m->config();
    char               a[16], b[16];

    switch (section)
    {
        case H89DEBUG_SECTION_TERMINAL:
        {
            H19Terminal&       t  = m->terminal();
            H19Terminal::Modes md = t.modes();
            o("H19: cursor row %u column %u%s%s, line 25 %s\n", t.cursorRow() + 1, t.cursorCol() + 1,
              t.cursorVisible() ? "" : " (hidden)", t.cursorBlock() ? " block" : "",
              t.line25() ? "on" : "off");
            o("modes:%s%s%s%s%s%s%s%s%s%s%s%s\n", md.ansi ? " ANSI" : " Heath",
              md.offline ? " off-line" : "",
              md.holdScreen ? " hold-screen" : "", md.keypadShifted ? " keypad-shifted" : "",
              md.altKeypad ? " alternate-keypad" : "", md.keyboardDisabled ? " keyboard-off" : "",
              md.reverseVideo ? " reverse" : "", md.graphics ? " graphics" : "",
              md.insert ? " insert" : "", md.wrapEOL ? " wrap" : "", md.autoLF ? " auto-LF" : "",
              md.autoCR ? " auto-CR" : "");
            o("line: %u baud, SW401 %s, SW402 %s, escape state %d, %zu key byte(s) queued, "
              "%zu held for SCROLL\n",
              md.baud, fmt8(rx, md.sw401, a, sizeof a), fmt8(rx, md.sw402, b, sizeof b), md.escState,
              md.queued, md.held);
            break;
        }
        case H89DEBUG_SECTION_MEMORY:
        {
            const uint8_t sw = m->sw501();
            o("%s, %s, P503 %s, monitor %s\n", h89_model_name(c.model), h89_memory_name(c.memory),
              h89_p503_name(c.p503), c.monitor);
            o("SW501 %s%s\n", fmt8(rx, sw, a, sizeof a), c.sw501_auto ? " (device switches from the slots)" : "");
            o("%s", m->gpp().dumpDebug().c_str());
            o("memory layout %d%s\n", m->memory().getCurrentLayoutNum(),
              m->h17RamWritable() ? ", H17 RAM writable" : "");
            if (c.speedup)
            {
                o("speed-up %dx\n", c.speedup);
            }
            break;
        }
        case H89DEBUG_SECTION_TIMER:
        {
            const Z80::Registers r = regs(m);
            o("2 ms clock: %lu ticks, interrupt %s\n", (unsigned long) m->timer().count(),
              m->timer().interruptEnabled() ? "enabled (GPP bit 1)" : "disabled");
            o("single step (GPP bit 0): %s\n",
              !m->singleStep().enabled() ? "off"
              : m->singleStep().asserted() ? "on, RST 10H raised" : "on, waiting for EI");
            o("interrupt levels pending %s%s, CPU IM %d IFF1 %d IFF2 %d%s\n",
              fmt8(rx, m->intr().levels(), a, sizeof a), m->intr().blocked() ? " (blocked)" : "",
              r.im, r.iff1, r.iff2, r.halted ? ", halted" : "");
            o("%llu cycles, %llu ticks since power-on\n",
              (unsigned long long) m->cpu().totalCycles(), (unsigned long long) m->slices());
            break;
        }
        case H89DEBUG_SECTION_H17:
            if (!m->h17())
            {
                o("(no H-88-1 fitted)\n");
                break;
            }
            o("%s", m->h17()->dumpDebug().c_str());
            drives_of(o, H89_CTRL_H17);
            break;
        case H89DEBUG_SECTION_FDC:
            if (!m->h37() && !m->mms316())
            {
                o("(no Z-89-37 or MMS 77316 fitted)\n");
                break;
            }
            if (m->h37())
            {
                o("%s", m->h37()->dumpDebug().c_str());
                drives_of(o, H89_CTRL_H37);
            }
            if (m->mms316())
            {
                o("%s", m->mms316()->dumpDebug().c_str());
                drives_of(o, H89_CTRL_MMS77316);
            }
            break;
        case H89DEBUG_SECTION_SASI:
            if (!m->mms320())
            {
                o("(no MMS 77320 fitted)\n");
                break;
            }
            o("%s", m->mms320()->dumpDebug().c_str());
            drives_of(o, H89_CTRL_MMS77320);
            break;
        case H89DEBUG_SECTION_SERIAL:
        {
            static const char* const names[] = {"aux (320Q)", "modem (330Q)", "line printer (340Q)"};
            auto port = [&](const char* name, INS8250& p) {
                INS8250::State s = p.state();
                char           c1[16], c2[16];
                o("%-20s LCR %s IER %s MCR %s LSR %s divisor %u%s%s\n", name,
                  fmt8(rx, s.lcr, a, sizeof a), fmt8(rx, s.ier, b, sizeof b),
                  fmt8(rx, s.mcr, c1, sizeof c1), fmt8(rx, s.lsr, c2, sizeof c2), s.divisor,
                  s.rxFull ? " rx-full" : "", s.intPending ? " int" : "");
            };
            port("console (350Q)", m->consolePort());
            for (int i = 0; i < 3; ++i)
            {
                if (m->serialPort(i))
                {
                    port(names[i], *m->serialPort(i));
                }
            }
            break;
        }
        case H89DEBUG_SECTION_CARD:
        {
            FujiNetCard* card = m->card();
            if (!card)
            {
                o("(no FujiNet card fitted)\n");
                break;
            }
            FujiNetCard::State s = card->state();
            char               c1[16], c2[16], c3[16], c4[16];
            o("82C55 mode %s  port B %s  port C %s  status %s  out %s  in %s\n",
              fmt8(rx, s.mode, a, sizeof a), fmt8(rx, s.portB, c4, sizeof c4),
              fmt8(rx, s.portC, b, sizeof b), fmt8(rx, s.status, c1, sizeof c1),
              fmt8(rx, s.outLatch, c2, sizeof c2), fmt8(rx, s.inLatch, c3, sizeof c3));
            o("/OBF %s  IBF %d  INTE1 %d INTE2 %d  245 %s, /OE %s\n",
              s.obfLow ? "low (byte waiting)" : "high", s.ibf, s.inte1, s.inte2,
              s.dirToEsp ? "H89->ESP32" : "ESP32->H89", s.oeLow ? "low" : "high");
            char lamps[9];
            for (int i = 0; i < 8; ++i)
            {
                lamps[i] = (s.lamps & (0x80 >> i)) ? '*' : '.';
            }
            lamps[8] = 0;
            o("lamps PB7-PB0 %s\n", lamps);
            static const char* const links[] = {"no FujiNet runtime", "connecting", "connected"};
            const int lk = (int) s.link;
            o("link: %s, %u connect(s), %u drop(s)\n", lk >= 0 && lk < 3 ? links[lk] : "?",
              s.connects, s.drops);
            o("bytes to FujiNet %llu, from FujiNet %llu, dropped %llu, /ACK waits %llu, queued %zu out %zu in\n",
              (unsigned long long) s.bytesToEsp, (unsigned long long) s.bytesFromEsp,
              (unsigned long long) s.txDropped, (unsigned long long) s.ackWaits, s.txQueued,
              s.rxQueued);
            break;
        }
        default:
            break;
    }
    return o.len;
}

/* ---- disassembly ------------------------------------------------------------------------------ */

namespace
{

void
fill_line(h89debug* d, Machine* m, uint16_t addr, h89debug_line* l, uint16_t pc)
{
    z80d_insn   ins;
    const char* lab;
    const int   rx = d->radix.load();

    memset(l, 0, sizeof *l);
    decode_at(m, addr, &ins, dasm_radix(d));
    l->address        = addr;
    l->length         = ins.len;
    l->is_pc          = addr == pc;
    l->has_breakpoint = d->map_exec[addr] != 0;
    fmt16(rx, addr, l->addr_text, sizeof l->addr_text);
    for (int i = 0, o = 0; i < ins.len && o < (int) sizeof l->bytes - 4; ++i)
    {
        char b[8];
        o += snprintf(l->bytes + o, sizeof l->bytes - (size_t) o, "%s%s", i ? " " : "",
                      fmt8(rx, ins.bytes[i], b, sizeof b));
    }
    lab = symtab_lookup(&d->syms, addr, SYM_BANK_ANY);
    if (lab)
    {
        snprintf(l->label, sizeof l->label, "%s", lab);
    }
    snprintf(l->disasm, sizeof l->disasm, "%s", ins.text);

    if (ins.flags & (Z80D_JUMP | Z80D_CALL))
    {
        const char* t = symtab_lookup(&d->syms, ins.target, SYM_BANK_ANY);
        if (t)
        {
            snprintf(l->comment, sizeof l->comment, "%s", t);
        }
        return;
    }
    // IN A,(n) / OUT (n),A: the port's name
    const uint8_t op = ins.bytes[0];
    if ((op == 0xdb || op == 0xd3) && ins.len == 2)
    {
        const char* pn = h89_port_name(ins.bytes[1]);
        if (pn)
        {
            snprintf(l->comment, sizeof l->comment, "%s", pn);
        }
        return;
    }
    // an absolute 16-bit operand that names a place: LD (nn),A, LD HL,nn ...
    if (ins.len >= 3)
    {
        const int      at = (op == 0xdd || op == 0xfd || op == 0xed) ? 2 : 1;
        const uint16_t v  = (uint16_t) (ins.bytes[at] | (ins.bytes[at + 1] << 8));
        const char*    t  = symtab_lookup(&d->syms, v, SYM_BANK_ANY);
        if (t && at + 1 < ins.len)
        {
            snprintf(l->comment, sizeof l->comment, "%s", t);
        }
    }
}

int
row_address_locked(Machine* m, uint16_t addr, int rows)
{
    z80d_insn ins;
    if (!m || rows == 0)
    {
        return addr;
    }
    if (rows > 0)
    {
        for (int i = 0; i < rows; ++i)
        {
            addr = (uint16_t) (addr + decode_at(m, addr, &ins));
        }
        return addr;
    }
    // backwards: the longest run of instructions that lands exactly on addr
    rows = -rows;
    for (int back = rows * 4; back >= 1; --back)
    {
        uint16_t starts[256];
        int      count = 0;
        uint16_t a     = (uint16_t) (addr - back);
        while (count < 256)
        {
            int dist = (uint16_t) (addr - a);
            if (dist == 0 || dist > back)
            {
                break;
            }
            starts[count++] = a;
            a               = (uint16_t) (a + decode_at(m, a, &ins));
        }
        if (a == addr && count >= rows)
        {
            return starts[count - rows];
        }
    }
    return (uint16_t) (addr - rows);
}

} // namespace

extern "C" int
h89debug_disassemble(h89debug* d, uint16_t addr, h89debug_line* out, int max, int* pc_line)
{
    int n = 0;
    if (pc_line)
    {
        *pc_line = -1;
    }
    if (!d)
    {
        return 0;
    }
    RunLock  l;
    Machine* m = machine();
    if (!m)
    {
        return 0;
    }
    const uint16_t pc = regs(m).pc;
    while (n < max)
    {
        fill_line(d, m, addr, &out[n], pc);
        if (out[n].is_pc && pc_line)
        {
            *pc_line = n;
        }
        addr = (uint16_t) (addr + out[n].length);
        n++;
    }
    return n;
}

extern "C" int
h89debug_row_address(h89debug* d, uint16_t addr, int rows)
{
    (void) d;
    RunLock l;
    return row_address_locked(machine(), addr, rows);
}

extern "C" int
h89debug_label_address(h89debug* d, const char* label)
{
    return d && label ? symtab_find(&d->syms, label) : -1;
}

extern "C" int
h89debug_address_label(h89debug* d, uint16_t addr, char* dst, int dstsz)
{
    const char* l = d ? symtab_lookup(&d->syms, addr, SYM_BANK_ANY) : nullptr;
    return snprintf(dst, (size_t) dstsz, "%s", l ? l : "");
}

extern "C" int
h89debug_set_label(h89debug* d, uint16_t addr, const char* label)
{
    if (!d)
    {
        return -1;
    }
    symtab_remove_at(&d->syms, addr);
    int rc = (label && *label) ? symtab_add(&d->syms, addr, SYM_BANK_ANY, label, 0) : 0;
    bump(d);
    return rc;
}

extern "C" int
h89debug_load_symbols(h89debug* d, const char* path, char* msg, int msgsz)
{
    if (!d || !path || !*path)
    {
        if (msg && msgsz > 0)
        {
            snprintf(msg, (size_t) msgsz, "Choose a symbol file (z88dk .map, .sym, .noi).");
        }
        return -1;
    }
    int n = symtab_load(&d->syms, path, msg, msgsz);
    bump(d);
    return n;
}

/* ---- breakpoints -------------------------------------------------------------------------------- */

extern "C" int
h89debug_breakpoint_add(h89debug* d, int type, uint16_t start, uint16_t end, const char* cond)
{
    char err[96];
    if (!d || !type)
    {
        return -1;
    }
    if (cond && *cond && expr_eval(cond, nullptr, nullptr, err, sizeof err) != 0)
    {
        return -1;
    }
    if (end < start)
    {
        std::swap(start, end);
    }
    RunLock                     l;
    std::lock_guard<std::mutex> lk(d->lock);
    if (d->nbp >= MAX_BP)
    {
        return -1;
    }
    h89debug_breakpoint* b = &d->bp[d->nbp++];
    memset(b, 0, sizeof *b);
    const int id = d->next_id++;
    b->id        = id;
    b->type      = type;
    b->start     = start;
    b->end       = end;
    b->enabled   = 1;
    snprintf(b->condition, sizeof b->condition, "%s", cond ? cond : "");
    rebuild_locked(d);
    bump(d);
    return id;
}

extern "C" void
h89debug_breakpoint_remove(h89debug* d, int id)
{
    if (!d)
    {
        return;
    }
    RunLock                     l;
    std::lock_guard<std::mutex> lk(d->lock);
    for (int i = 0; i < d->nbp; ++i)
    {
        if (d->bp[i].id == id)
        {
            memmove(&d->bp[i], &d->bp[i + 1], (size_t) (d->nbp - i - 1) * sizeof d->bp[0]);
            d->nbp--;
            break;
        }
    }
    rebuild_locked(d);
    bump(d);
}

extern "C" void
h89debug_breakpoint_enable(h89debug* d, int id, int enabled)
{
    if (!d)
    {
        return;
    }
    RunLock                     l;
    std::lock_guard<std::mutex> lk(d->lock);
    for (int i = 0; i < d->nbp; ++i)
    {
        if (d->bp[i].id == id)
        {
            d->bp[i].enabled = enabled != 0;
        }
    }
    rebuild_locked(d);
    bump(d);
}

extern "C" int
h89debug_breakpoint_list(h89debug* d, h89debug_breakpoint* out, int max)
{
    if (!d)
    {
        return 0;
    }
    std::lock_guard<std::mutex> lk(d->lock);
    int                         n = d->nbp < max ? d->nbp : max;
    memcpy(out, d->bp, (size_t) n * sizeof d->bp[0]);
    return n;
}

extern "C" void
h89debug_breakpoint_clear(h89debug* d)
{
    if (!d)
    {
        return;
    }
    RunLock                     l;
    std::lock_guard<std::mutex> lk(d->lock);
    d->nbp = 0;
    rebuild_locked(d);
    bump(d);
}

extern "C" int
h89debug_breakpoint_check(h89debug* d, uint16_t addr)
{
    return d && d->map_exec[addr] != 0;
}

extern "C" int
h89debug_breakpoint_toggle(h89debug* d, uint16_t addr)
{
    if (!d)
    {
        return 0;
    }
    int id = -1;
    {
        std::lock_guard<std::mutex> lk(d->lock);
        for (int i = 0; i < d->nbp; ++i)
        {
            if (d->bp[i].type == H89DEBUG_BP_EXEC && d->bp[i].start == addr && d->bp[i].end == addr)
            {
                id = d->bp[i].id;
                break;
            }
        }
    }
    if (id >= 0)
    {
        h89debug_breakpoint_remove(d, id);
        return 0;
    }
    h89debug_breakpoint_add(d, H89DEBUG_BP_EXEC, addr, addr, "");
    return 1;
}

/* ---- the trace ring --------------------------------------------------------------------------------- */

extern "C" void
h89debug_trace_enable(h89debug* d, int on)
{
    if (d)
    {
        d->trace_on.store(on != 0);
        bump(d);
    }
}

extern "C" int
h89debug_trace_enabled(h89debug* d)
{
    return d && d->trace_on.load();
}

extern "C" int
h89debug_trace_read(h89debug* d, h89debug_trace* out, int max)
{
    if (!d)
    {
        return 0;
    }
    RunLock l;
    int     n = d->ring_count < max ? d->ring_count : max;
    for (int i = 0; i < n; ++i)
    {
        out[i] = d->ring[(d->ring_head - 1 - i + TRACE_N) % TRACE_N];
    }
    return n;
}

extern "C" void
h89debug_trace_clear(h89debug* d)
{
    if (!d)
    {
        return;
    }
    RunLock l;
    d->ring_head  = 0;
    d->ring_count = 0;
    bump(d);
}

/* ---- FujiNet ------------------------------------------------------------------------------------ */

extern "C" int
h89debug_fujinet_trace(h89debug* d, h89_fuji_packet* out, int max)
{
    return d ? d->fuji.read(out, max) : 0;
}

extern "C" void
h89debug_fujinet_trace_clear(h89debug* d)
{
    if (d)
    {
        d->fuji.clear();
    }
}

extern "C" int
h89debug_fujinet_state(h89debug* d, char* dst, int dstsz)
{
    return h89debug_section(d, H89DEBUG_SECTION_CARD, dst, dstsz);
}

/* ---- files ------------------------------------------------------------------------------------------- */

extern "C" int
h89debug_save(h89debug* d, const char* kind, const char* path, char* msg, int msgsz)
{
    FILE* f;
    if (!d || !kind || !path)
    {
        return -1;
    }
    if (!strcmp(kind, "ram"))
    {
        uint8_t buf[65536];
        h89debug_read(d, 0, buf, 65536);
        f = fopen(path, "wb");
        if (!f || fwrite(buf, 1, sizeof buf, f) != sizeof buf)
        {
            if (f)
            {
                fclose(f);
            }
            snprintf(msg, (size_t) msgsz, "cannot write %s", path);
            return -1;
        }
        fclose(f);
        snprintf(msg, (size_t) msgsz, "64K as the CPU sees it saved to %s", path);
        return 0;
    }
    if (!strcmp(kind, "dis") || !strcmp(kind, "regs"))
    {
        f = fopen(path, "w");
        if (!f)
        {
            snprintf(msg, (size_t) msgsz, "cannot write %s", path);
            return -1;
        }
        if (!strcmp(kind, "dis"))
        {
            static h89debug_line lines[256];
            unsigned             addr = 0;
            int                  pcl;
            while (addr < 0x10000)
            {
                int n = h89debug_disassemble(d, (uint16_t) addr, lines, 256, &pcl);
                for (int i = 0; i < n && addr < 0x10000; ++i)
                {
                    if (lines[i].label[0])
                    {
                        fprintf(f, "%s:\n", lines[i].label);
                    }
                    fprintf(f, "%-10s %-16s %-24s%s%s\n", lines[i].addr_text, lines[i].bytes,
                            lines[i].disasm, lines[i].comment[0] ? "; " : "", lines[i].comment);
                    addr += (unsigned) lines[i].length;
                }
                if (!n)
                {
                    break;
                }
            }
        }
        else
        {
            static char  text[8192];
            h89debug_cpu c;
            h89debug_cpu_get(d, &c);
            fprintf(f, "PC=%04X SP=%04X AF=%04X BC=%04X DE=%04X HL=%04X IX=%04X IY=%04X\n", c.pc,
                    c.sp, c.af, c.bc, c.de, c.hl, c.ix, c.iy);
            fprintf(f, "AF'=%04X BC'=%04X DE'=%04X HL'=%04X I=%02X R=%02X IM%d IFF1=%d IFF2=%d\n",
                    c.af2, c.bc2, c.de2, c.hl2, c.i, c.r, c.im, c.iff1, c.iff2);
            for (int s = 0; s < H89DEBUG_SECTION_COUNT; ++s)
            {
                h89debug_section(d, s, text, sizeof text);
                fprintf(f, "\n[%s]\n%s", h89debug_section_name(s), text);
            }
        }
        fclose(f);
        snprintf(msg, (size_t) msgsz, "saved to %s", path);
        return 0;
    }
    snprintf(msg, (size_t) msgsz, "unknown kind '%s' (dis, ram, regs)", kind);
    return -1;
}
