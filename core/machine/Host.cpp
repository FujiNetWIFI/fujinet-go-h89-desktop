/*
 * Host -- see host.h.
 *
 * The frame slot, the run lock and the deadline ladder follow the siblings'
 * host.c (fujinet-go-sms-desktop core/sms/host.c), in C++ over std::thread
 * because the machine is C++.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "host.h"

#include "FujiNetCard.h"
#include "H19Render.h"
#include "H19Terminal.h"
#include "Machine.h"
#include "drives_internal.h"
#include "MachineContext.h"
#include "h89keys.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <mmsystem.h>
#elif defined(__APPLE__)
#include <pthread.h>
#include <pthread/qos.h>
#else
#include <pthread.h>
#endif

using Clock = std::chrono::steady_clock;

namespace
{

const int slicesPerBatch_c = 8;                        // 16 ms
const std::chrono::nanoseconds batch_c(16000000);
const unsigned blinkSlices_c = 133;                    // ~1.9 Hz: the 6845's 1/32-field blink
const uint64_t warpQuietSlices_c = 1000;               // 2 s of FujiNet silence before warping

std::unique_ptr<Machine> s_machine;
std::thread              s_thread;
std::thread::id          s_machineTid;
std::atomic<bool>        s_running(false);
std::atomic<bool>        s_stopReq(false);
std::atomic<bool>        s_resetReq(false);
std::atomic<bool>        s_warp(false);
std::atomic<bool>        s_paused(false);
std::atomic<uint64_t>    s_slices(0);
std::string              s_error;
h89host_pacing           s_pacing;

// the machine object's lifetime, against the input calls from other threads
std::mutex s_lifeLock;

// ---- the run lock ----
std::mutex         s_runLock;
std::atomic<int>   s_lockWaiters(0);
thread_local int   t_lockDepth = 0;
thread_local MachineContext* t_prevContext = nullptr;

std::mutex              s_pauseLock;
std::condition_variable s_pauseCond;

// ---- the frame slot ----
std::mutex            s_frameLock;
std::vector<uint32_t> s_frame(H89HOST_FB_WIDTH * H89HOST_FB_HEIGHT, 0);
uint64_t              s_frameSerial = 0;

// ---- view settings (any thread -> the machine thread) ----
std::atomic<int>      s_phosphor(0);
std::atomic<int>      s_scanlines(0);
std::atomic<int>      s_blink(1);
std::atomic<unsigned> s_viewGen(1);

H19Render             s_render;
std::vector<uint32_t> s_back(H89HOST_FB_WIDTH * H89HOST_FB_HEIGHT, 0);

void
setThreadName()
{
#if defined(__APPLE__)
    pthread_setname_np("h89-machine");
#elif defined(__linux__)
    pthread_setname_np(pthread_self(), "h89-machine");
#endif
}

void
runLockTake()
{
    // let a waiting UI thread in first: a mutex is not fair, and a machine
    // running behind would relock at once
    for (int i = 0; i < 2000 && s_lockWaiters.load() > 0; i++)
    {
        if (i < 50)
        {
            std::this_thread::yield();
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    }
    s_runLock.lock();
}

void
publish(bool force, uint64_t& lastGen, int& lastPhase, unsigned& lastView)
{
    H19Terminal& term  = s_machine->terminal();
    uint64_t     gen   = term.generation();
    bool         blink = s_blink.load() != 0;
    int          phase = blink ? (int) ((s_slices.load() / blinkSlices_c) & 1) : 0;
    unsigned     view  = s_viewGen.load();

    if (!force && gen == lastGen && phase == lastPhase && view == lastView)
    {
        return;
    }
    if (view != lastView)
    {
        s_render.setPhosphor(s_phosphor.load());
        s_render.setScanlines(s_scanlines.load() != 0);
    }
    lastGen   = gen;
    lastPhase = phase;
    lastView  = view;

    s_render.render(term, phase == 0, s_back.data());
    std::lock_guard<std::mutex> g(s_frameLock);
    s_frame.swap(s_back);
    ++s_frameSerial;
}

bool
cardBusy(uint64_t& lastActivity, unsigned long long& lastBytes)
{
    FujiNetCard* card = s_machine->card();
    if (!card)
    {
        return false;
    }
    FujiNetCard::State st = card->state();
    unsigned long long bytes = st.bytesToEsp + st.bytesFromEsp;
    uint64_t now = s_slices.load();
    if (bytes != lastBytes || st.obfLow || st.ibf || st.rxQueued)
    {
        lastBytes    = bytes;
        lastActivity = now;
    }
    return now - lastActivity < warpQuietSlices_c;
}

void
machineThread()
{
    setThreadName();
#if defined(_WIN32)
    timeBeginPeriod(1);
#elif defined(__APPLE__)
    // Darwin coalesces the timers of a default-QoS thread with no UI by
    // whole frames on a busy machine; an emulator's clock is interactive.
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif

    MachineContext::Scope scope(&s_machine->context());
    const bool paceLog = getenv("H89_PACE_LOG") && *getenv("H89_PACE_LOG") == '1';

    // the machine thread holds the run lock whenever it runs the machine;
    // its own nested h89host_lock calls (the debugger's hooks) are no-ops
    t_lockDepth = 1;
    memset(&s_pacing, 0, sizeof s_pacing);
    s_runLock.lock();

    uint64_t          lastGen   = ~0ull;
    int               lastPhase = -1;
    unsigned          lastView  = 0;
    uint64_t          lastActivity = 0;
    unsigned long long lastBytes = 0;
    Clock::time_point next      = Clock::now();
    Clock::time_point lastPublish = next;
    Clock::time_point logAt     = next + std::chrono::seconds(1);
    uint64_t          logSlices = 0, logSleeps = 0, logResyncs = 0;

    publish(true, lastGen, lastPhase, lastView);

    while (!s_stopReq.load())
    {
        if (s_paused.load())
        {
            s_runLock.unlock();
            {
                std::unique_lock<std::mutex> lk(s_pauseLock);
                s_pauseCond.wait(lk, [] { return !s_paused.load() || s_stopReq.load(); });
            }
            runLockTake();
            next = Clock::now();
            continue;
        }

        if (s_resetReq.exchange(false))
        {
            s_machine->reset();
        }

        for (int i = 0; i < slicesPerBatch_c && !s_stopReq.load(); ++i)
        {
            s_machine->runSlice();
            s_slices.fetch_add(1);
        }
        s_pacing.batches++;

        // disks written to in the last second go back to their files
        drives_internal::service(*s_machine, s_slices.load());

        bool warp = s_warp.load() && !cardBusy(lastActivity, lastBytes);
        Clock::time_point now = Clock::now();
        if (!warp || now - lastPublish >= batch_c)
        {
            publish(false, lastGen, lastPhase, lastView);
            lastPublish = now;
        }

        // between batches the machine is anyone's who asks
        s_runLock.unlock();

        if (warp)
        {
            next = Clock::now();
        }
        else
        {
            next += batch_c;
            now = Clock::now();
            auto behind = now - next;
            if (behind > 4 * batch_c)
            {
                // a laptop resume or a debugger stop: resync, don't fast-forward
                next = now;
                s_pacing.resyncs++;
            }
            else if (behind < Clock::duration::zero())
            {
                std::this_thread::sleep_for(next - now);
                long late = (long) std::chrono::duration_cast<std::chrono::nanoseconds>(
                                Clock::now() - next).count();
                s_pacing.sleeps++;
                if (late > 0)
                {
                    s_pacing.late_ns += (uint64_t) late;
                    if (late > s_pacing.worst_late_ns)
                    {
                        s_pacing.worst_late_ns = late;
                    }
                }
            }
        }

        if (paceLog && Clock::now() >= logAt)
        {
            uint64_t sl = s_slices.load();
            fprintf(stderr, "h89 pace: %llu slices/s (500 = real time), %llu sleeps, %llu resyncs, worst late %ld us%s\n",
                    (unsigned long long) (sl - logSlices),
                    (unsigned long long) (s_pacing.sleeps - logSleeps),
                    (unsigned long long) (s_pacing.resyncs - logResyncs),
                    s_pacing.worst_late_ns / 1000, warp ? " [warp]" : "");
            logSlices  = sl;
            logSleeps  = s_pacing.sleeps;
            logResyncs = s_pacing.resyncs;
            logAt += std::chrono::seconds(1);
            if (Clock::now() > logAt)
            {
                logAt = Clock::now() + std::chrono::seconds(1);
            }
        }

        runLockTake();
    }
    s_runLock.unlock();
#if defined(_WIN32)
    timeEndPeriod(1);
#endif
}

} // namespace

// ---------------------------------------------------------------------------

extern "C" {

int
h89host_start(const h89host_opts* opts)
{
    if (s_running.load())
    {
        s_error = "a machine is already running";
        return -1;
    }
    s_error.clear();
    if (!opts)
    {
        s_error = "no options";
        return -1;
    }
    if (!s_render.setFont(opts->font, opts->font_size))
    {
        s_error = "The H19 character ROM (444-29) is missing; import the H89 ROMs.";
        return -1;
    }

    MachineRoms roms;
    roms.monitor     = opts->monitor;
    roms.monitorSize = opts->monitor_size;
    roms.h17         = opts->h17_rom;
    roms.h17Size     = opts->h17_size;

    MachineOptions mo;
    mo.boipHost = opts->boip_host ? opts->boip_host : "127.0.0.1";
    mo.boipPort = opts->boip_port;
    mo.sasiDir  = opts->sasi_dir ? opts->sasi_dir : "";

    try
    {
        std::lock_guard<std::mutex> g(s_lifeLock);
        s_machine.reset(new Machine(opts->config, roms, mo));
    }
    catch (const std::exception& e)
    {
        s_error = e.what();
        return -1;
    }

    s_render.setPhosphor(s_phosphor.load());
    s_render.setScanlines(s_scanlines.load() != 0);
    s_slices.store(0);
    s_stopReq.store(false);
    s_resetReq.store(false);
    s_paused.store(opts->paused != 0);
    {
        std::lock_guard<std::mutex> g(s_frameLock);
        s_frameSerial = 0;
    }

    try
    {
        s_thread      = std::thread(machineThread);
        s_machineTid  = s_thread.get_id();
    }
    catch (const std::exception&)
    {
        s_error = "cannot start the machine thread";
        std::lock_guard<std::mutex> g(s_lifeLock);
        s_machine.reset();
        return -1;
    }
    s_running.store(true);
    return 0;
}

void
h89host_stop(void)
{
    if (!s_running.load())
    {
        return;
    }
    s_stopReq.store(true);
    {
        std::lock_guard<std::mutex> lk(s_pauseLock);
        s_pauseCond.notify_all();
    }
    if (s_thread.joinable())
    {
        s_thread.join();
    }
    s_running.store(false);
    {
        std::lock_guard<std::mutex> g(s_lifeLock);
        // every disk back to its file before the machine goes
        std::string err;
        if (drives_internal::flushAll(*s_machine, err))
        {
            s_error = "a disk could not be saved: " + err;
            fprintf(stderr, "h89: %s\n", s_error.c_str());
        }
        s_machine.reset();
    }
    std::lock_guard<std::mutex> g(s_frameLock);
    s_frameSerial = 0;
}

int
h89host_is_running(void)
{
    return s_running.load() ? 1 : 0;
}

const char*
h89host_last_error(void)
{
    return s_error.c_str();
}

void
h89host_reset(void)
{
    if (s_running.load())
    {
        s_resetReq.store(true);
    }
}

int
h89host_frame_copy(uint32_t* dst, uint64_t* serial_inout)
{
    std::lock_guard<std::mutex> g(s_frameLock);
    if (*serial_inout == s_frameSerial && *serial_inout != 0)
    {
        return 0;
    }
    if (s_frameSerial == 0 && *serial_inout == 0)
    {
        // nothing published yet: a black screen
        std::fill(dst, dst + H89HOST_FB_WIDTH * H89HOST_FB_HEIGHT, 0u);
        return 1;
    }
    memcpy(dst, s_frame.data(), s_frame.size() * sizeof(uint32_t));
    *serial_inout = s_frameSerial;
    return 1;
}

void
h89host_set_view(int phosphor, int scanlines, int cursor_blink)
{
    s_phosphor.store(phosphor);
    s_scanlines.store(scanlines ? 1 : 0);
    s_blink.store(cursor_blink ? 1 : 0);
    s_viewGen.fetch_add(1);
}

void
h89host_key(unsigned code, int shift)
{
    std::lock_guard<std::mutex> g(s_lifeLock);
    if (!s_machine)
    {
        return;
    }
    // SHIFT+RESET pulls the H89's /RESET; the terminal still clicks
    s_machine->terminal().postKey(code, shift != 0);
    if (code == H89K_RESET)
    {
        s_resetReq.store(true);
    }
}

size_t
h89host_paste(const char* utf8)
{
    std::lock_guard<std::mutex> g(s_lifeLock);
    return (s_machine && utf8) ? s_machine->terminal().postText(utf8) : 0;
}

void
h89host_paste_cancel(void)
{
    std::lock_guard<std::mutex> g(s_lifeLock);
    if (s_machine)
    {
        s_machine->terminal().cancelText();
    }
}

int
h89host_paste_pending(void)
{
    std::lock_guard<std::mutex> g(s_lifeLock);
    return (s_machine && s_machine->terminal().textPending()) ? 1 : 0;
}

void
h89host_take_sounds(unsigned* bells, unsigned* clicks)
{
    std::lock_guard<std::mutex> g(s_lifeLock);
    unsigned b = 0, c = 0;
    if (s_machine)
    {
        b = s_machine->terminal().takeBells();
        c = s_machine->terminal().takeClicks();
    }
    if (bells)
    {
        *bells = b;
    }
    if (clicks)
    {
        *clicks = c;
    }
}

size_t
h89host_screen_text(char* dst, size_t dstsz, int line25)
{
    if (!dst || !dstsz)
    {
        return 0;
    }
    dst[0] = '\0';
    if (!s_running.load())
    {
        return 0;
    }
    h89host_lock();
    std::string text = s_machine ? s_machine->terminal().screenText(line25 != 0) : std::string();
    h89host_unlock();
    size_t n = text.size() < dstsz - 1 ? text.size() : dstsz - 1;
    memcpy(dst, text.data(), n);
    dst[n] = '\0';
    return n;
}

unsigned
h89host_h19_modes(void)
{
    unsigned m = 0;
    if (!s_running.load())
    {
        return 0;
    }
    h89host_lock();
    if (s_machine)
    {
        H19Terminal::Modes t = s_machine->terminal().modes();
        m = (t.offline ? 0x01u : 0) | (t.holdScreen ? 0x02u : 0) |
            (t.keypadShifted ? 0x04u : 0) | (t.altKeypad ? 0x08u : 0) |
            (t.keyboardDisabled ? 0x10u : 0) | (t.graphics ? 0x20u : 0) |
            (t.insert ? 0x40u : 0) | (t.reverseVideo ? 0x80u : 0);
    }
    h89host_unlock();
    return m;
}

size_t
h89host_text_before_cursor(char* dst, size_t dstsz)
{
    if (!dst || !dstsz)
    {
        return 0;
    }
    dst[0] = '\0';
    if (!s_running.load())
    {
        return 0;
    }
    size_t n = 0;
    h89host_lock();
    if (s_machine)
    {
        const H19Terminal& t   = s_machine->terminal();
        unsigned           row = t.cursorRow();
        for (unsigned col = 0; col < t.cursorCol() && n + 1 < dstsz; col++)
        {
            uint8_t c = t.cell(col, row) & 0x7f;
            dst[n++]  = (c < 0x20 || c == 0x7f) ? '?' : (char) c;
        }
    }
    h89host_unlock();
    dst[n] = '\0';
    return n;
}

void
h89host_set_warp(int on)
{
    s_warp.store(on != 0);
}

int
h89host_warp(void)
{
    return s_warp.load() ? 1 : 0;
}

uint64_t
h89host_slices(void)
{
    return s_slices.load();
}

void
h89host_get_pacing(h89host_pacing* out)
{
    *out = s_pacing;
}

int
h89host_card_link(void)
{
    if (!s_running.load())
    {
        return -1;
    }
    h89host_lock();
    int link = (s_machine && s_machine->card()) ? (int) s_machine->card()->link() : -1;
    h89host_unlock();
    return link;
}

void
h89host_lock(void)
{
    if (t_lockDepth++ == 0)
    {
        s_lockWaiters.fetch_add(1);
        s_runLock.lock();
        s_lockWaiters.fetch_sub(1);
        t_prevContext = MachineContext::exchange(s_machine ? &s_machine->context() : nullptr);
    }
}

void
h89host_unlock(void)
{
    if (t_lockDepth > 0 && --t_lockDepth == 0)
    {
        MachineContext::exchange(t_prevContext);
        s_runLock.unlock();
    }
}

void
h89host_park_release(void)
{
    s_runLock.unlock();
}

void
h89host_park_reacquire(void)
{
    s_runLock.lock();
}

int
h89host_on_machine_thread(void)
{
    return s_running.load() && std::this_thread::get_id() == s_machineTid ? 1 : 0;
}

void
h89host_set_paused(int paused)
{
    std::lock_guard<std::mutex> lk(s_pauseLock);
    s_paused.store(paused != 0);
    s_pauseCond.notify_all();
}

int
h89host_paused(void)
{
    return s_paused.load() ? 1 : 0;
}

} // extern "C"

Machine*
h89host_machine(void)
{
    return s_machine.get();
}
