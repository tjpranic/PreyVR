#include "diagnostics/capture/OverflowCapture.h"
#include "base/EnvSwitch.h"
#include "base/Logger.h"

#include <dbghelp.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace preyvr::dll {
namespace {
// How long the faulting thread waits for the capture. A dump of Prey takes a
// few seconds; if it takes longer something is stuck (a lock the faulting
// thread holds), and Prey's handler gets the exception as it would have.
constexpr DWORD kCaptureWaitMs = 60000;
constexpr SIZE_T kCaptureStack = 256 * 1024;
// x64 TEB.DeallocationStack: the bottom of the thread's whole stack reservation.
constexpr std::size_t kTebDeallocationStack = 0x1478;
// Frames unwound from the fault; the first and the last of them listed, and the
// return addresses that repeat most.
constexpr std::size_t kMaxFrames = 16384, kListedFrames = 64, kBottomFrames = 16, kListedRepeats = 8;
constexpr MINIDUMP_TYPE kDumpType = static_cast<MINIDUMP_TYPE>(
    MiniDumpWithThreadInfo | MiniDumpWithProcessThreadData | MiniDumpWithFullMemoryInfo |
    MiniDumpWithUnloadedModules | MiniDumpWithHandleData | MiniDumpWithIndirectlyReferencedMemory);

using WriteDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                  PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
using GetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PWSTR*);

// What the handler records, before it wakes the capture thread.
struct Fault {
    DWORD thread = 0;
    EXCEPTION_POINTERS* pointers = nullptr;
    std::uintptr_t stackBase = 0, stackLimit = 0, reservation = 0;
};

std::atomic<bool> gArmed{false};
std::atomic<long> gFired{0};
HANDLE gWake = nullptr, gDone = nullptr;
PVOID gHandler = nullptr;
Fault gFault; // written by the handler before gWake, read by the capture thread after
WriteDumpFn gWriteDump = nullptr;
std::filesystem::path gDumpPath, gReportPath; // fixed at arming
std::array<std::atomic<DWORD>, 4> gRoles{};
// The capture thread's unwind, kept off its stack.
std::array<std::uintptr_t, kMaxFrames> gFrames{};

void Log(const std::string& line)
{
    lifecycle::Log("preyvr_crash " + line);
}

// Never throws, unlike path::string() on a name outside the code page.
std::string Utf8(const std::wstring& text)
{
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return {};
    std::string out(static_cast<std::size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, out.data(), size, nullptr, nullptr);
    return out;
}

// **On what is left of the overflowed stack.** No allocation, no logging, no
// C++ objects: a few stores, then two waits.
LONG CALLBACK OnException(EXCEPTION_POINTERS* pointers)
{
    if (pointers == nullptr || pointers->ExceptionRecord == nullptr ||
        pointers->ExceptionRecord->ExceptionCode != EXCEPTION_STACK_OVERFLOW) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    long none = 0;
    if (!gFired.compare_exchange_strong(none, 1)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const auto* tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
    gFault.thread = GetCurrentThreadId();
    gFault.pointers = pointers;
    gFault.stackBase = reinterpret_cast<std::uintptr_t>(tib->StackBase);
    gFault.stackLimit = reinterpret_cast<std::uintptr_t>(tib->StackLimit);
    gFault.reservation =
        *reinterpret_cast<const std::uintptr_t*>(reinterpret_cast<const std::uint8_t*>(NtCurrentTeb()) +
                                                 kTebDeallocationStack);
    SetEvent(gWake);
    WaitForSingleObject(gDone, kCaptureWaitMs);
    return EXCEPTION_CONTINUE_SEARCH;
}

// The faulting thread's frames, unwound from its context into gFrames. Plain
// data only, for the __try. Returns how many; *truncated when it stopped at
// kMaxFrames rather than at the stack's end.
std::size_t Unwind(const CONTEXT& fault, std::uintptr_t low, std::uintptr_t high, bool* truncated)
{
    CONTEXT context = fault;
    std::size_t count = 0;
    *truncated = false;
    __try {
        while (count < kMaxFrames) {
            const DWORD64 pc = context.Rip;
            if (pc == 0) break;
            gFrames[count++] = static_cast<std::uintptr_t>(pc);
            DWORD64 imageBase = 0;
            PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(pc, &imageBase, nullptr);
            if (function != nullptr) {
                PVOID handlerData = nullptr;
                DWORD64 establisher = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, pc, function, &context, &handlerData, &establisher,
                                 nullptr);
            } else {
                // A leaf: its return address is where the stack points.
                context.Rip = *reinterpret_cast<const DWORD64*>(context.Rsp);
                context.Rsp += 8;
            }
            if (context.Rsp < low || context.Rsp >= high) break;
        }
        *truncated = count == kMaxFrames;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return count;
}

// "module+0xRVA", or the bare address outside any module.
class Modules {
public:
    std::string Name(std::uintptr_t address)
    {
        PVOID base = nullptr;
        RtlPcToFileHeader(reinterpret_cast<PVOID>(address), &base);
        std::ostringstream out;
        if (base == nullptr) {
            out << "0x" << std::hex << std::uppercase << address;
            return out.str();
        }
        const auto key = reinterpret_cast<std::uintptr_t>(base);
        auto found = names_.find(key);
        if (found == names_.end()) {
            wchar_t path[MAX_PATH]{};
            const DWORD length = GetModuleFileNameW(static_cast<HMODULE>(base), path, MAX_PATH);
            const std::string name = length != 0 ? Utf8(std::filesystem::path(path).filename().wstring()) : "module";
            found = names_.emplace(key, name).first;
        }
        out << found->second << "+0x" << std::hex << std::uppercase << address - key;
        return out.str();
    }

private:
    std::map<std::uintptr_t, std::string> names_;
};

const char* RoleName(DWORD thread)
{
    static constexpr const char* names[] = {"main", "render", "worker", "hold"};
    for (std::size_t i = 0; i < gRoles.size(); ++i) {
        if (thread != 0 && gRoles[i].load(std::memory_order_relaxed) == thread) return names[i];
    }
    return "other";
}

// The thread's own name, where whoever started it set one.
std::string ThreadName(DWORD thread)
{
    const auto get = reinterpret_cast<GetThreadDescriptionFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription")));
    HANDLE handle = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, thread);
    std::string name;
    if (get != nullptr && handle != nullptr) {
        PWSTR text = nullptr;
        if (SUCCEEDED(get(handle, &text)) && text != nullptr) {
            name = Utf8(text);
            LocalFree(text);
        }
    }
    if (handle != nullptr) CloseHandle(handle);
    return name;
}

// Writes the dump; returns 0 or the error.
DWORD WriteDump()
{
    if (gWriteDump == nullptr) return ERROR_PROC_NOT_FOUND;
    HANDLE file = CreateFileW(gDumpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) return GetLastError();
    MINIDUMP_EXCEPTION_INFORMATION exception{gFault.thread, gFault.pointers, FALSE};
    const BOOL written = gWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, kDumpType, &exception,
                                    nullptr, nullptr);
    const DWORD error = written ? 0 : GetLastError();
    CloseHandle(file);
    return written ? 0 : (error != 0 ? error : ERROR_GEN_FAILURE);
}

std::string Kilobytes(std::uintptr_t high, std::uintptr_t low)
{
    return high > low && low != 0 ? std::to_string((high - low) / 1024) : "?";
}

// The capture thread's work, once the handler has woken it: the dump first,
// while nothing else has run, then the report and the log line.
void Capture()
{
    const DWORD dumpError = WriteDump();
    const CONTEXT& fault = *gFault.pointers->ContextRecord;
    bool truncated = false;
    const std::size_t depth = Unwind(fault, gFault.reservation, gFault.stackBase, &truncated);
    Modules modules;
    std::map<std::uintptr_t, std::size_t> counts;
    for (std::size_t i = 0; i < depth; ++i) ++counts[gFrames[i]];
    std::vector<std::pair<std::size_t, std::uintptr_t>> repeats;
    for (const auto& [address, n] : counts) {
        if (n > 1) repeats.emplace_back(n, address);
    }
    std::sort(repeats.begin(), repeats.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    if (repeats.size() > kListedRepeats) repeats.resize(kListedRepeats);

    const char* role = RoleName(gFault.thread);
    const std::string name = ThreadName(gFault.thread);
    const std::string at =
        modules.Name(reinterpret_cast<std::uintptr_t>(gFault.pointers->ExceptionRecord->ExceptionAddress));
    const std::string reserveKb = Kilobytes(gFault.stackBase, gFault.reservation);
    const std::string usedKb = Kilobytes(gFault.stackBase, static_cast<std::uintptr_t>(fault.Rsp));

    std::ostringstream report;
    report << "preyvr stack overflow capture\n"
           << "pid=" << GetCurrentProcessId() << " thread=" << gFault.thread << " role=" << role << " name=\""
           << name << "\"\n"
           << "exception=0x" << std::hex << std::uppercase << EXCEPTION_STACK_OVERFLOW << std::dec << " at=" << at
           << "\n"
           << "stack reserveKb=" << reserveKb << " committedKb=" << Kilobytes(gFault.stackBase, gFault.stackLimit)
           << " usedKb=" << usedKb << " rsp=0x" << std::hex << fault.Rsp << std::dec << "\n"
           << "dump=\"" << Utf8(gDumpPath.wstring()) << "\" result=" << dumpError << "\n"
           << "frames=" << depth << (truncated ? "+" : "") << "\n";
    // Runs of one address (a function calling itself) as one line.
    const auto listFrames = [&](const char* title, std::size_t from, std::size_t to) {
        report << title << ":\n";
        for (std::size_t i = from; i < to;) {
            std::size_t run = i + 1;
            while (run < to && gFrames[run] == gFrames[i]) ++run;
            report << "  #" << i;
            if (run - i > 1) report << "-#" << run - 1;
            report << ' ' << modules.Name(gFrames[i]) << "\n";
            i = run;
        }
    };
    // The fault's end, then the thread's own start, which says what the thread is.
    const std::size_t top = (std::min)(depth, kListedFrames);
    listFrames("top", 0, top);
    if (depth > top) listFrames(truncated ? "deepest unwound" : "bottom", (std::max)(top, depth - kBottomFrames), depth);
    report << "repeating:\n";
    for (const auto& [n, address] : repeats) report << "  " << modules.Name(address) << " x" << n << "\n";
    {
        std::ofstream file(gReportPath, std::ios::binary | std::ios::trunc);
        file << report.str();
    }

    std::ostringstream line;
    line << "detail=stack_overflow thread=" << gFault.thread << " role=" << role << " reserveKb=" << reserveKb
         << " usedKb=" << usedKb << " frames=" << depth << (truncated ? "+" : "") << " at=" << at;
    if (!repeats.empty()) line << " loop=" << modules.Name(repeats.front().second) << 'x' << repeats.front().first;
    line << " dump=" << (dumpError == 0 ? "ok" : "failed:" + std::to_string(dumpError)) << " report=\""
         << Utf8(gReportPath.wstring()) << "\"";
    Log(line.str());
}

DWORD WINAPI CaptureThread(void*)
{
    WaitForSingleObject(gWake, INFINITE);
    // Whatever fails after the dump, the faulting thread is let go.
    try {
        Capture();
    } catch (...) {
    }
    SetEvent(gDone);
    return 0;
}

// The dump and report beside `log`, the handler, and the capture thread.
// Returns 0, or the step that failed.
DWORD Arm(const std::filesystem::path& log)
{
    HMODULE dbghelp = GetModuleHandleW(L"dbghelp.dll");
    if (dbghelp == nullptr) dbghelp = LoadLibraryExW(L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    gWriteDump = dbghelp != nullptr
        ? reinterpret_cast<WriteDumpFn>(reinterpret_cast<void*>(GetProcAddress(dbghelp, "MiniDumpWriteDump")))
        : nullptr;
    const std::wstring stem = L"preyvr-overflow-" + std::to_wstring(GetCurrentProcessId());
    gDumpPath = log.parent_path() / (stem + L".dmp");
    gReportPath = log.parent_path() / (stem + L".txt");
    gWake = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    gDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (gWake == nullptr || gDone == nullptr) return 1;
    HANDLE thread = CreateThread(nullptr, kCaptureStack, &CaptureThread, nullptr, STACK_SIZE_PARAM_IS_A_RESERVATION,
                                 nullptr);
    if (thread == nullptr) return 2;
    CloseHandle(thread);
    gHandler = AddVectoredExceptionHandler(1, &OnException);
    return gHandler != nullptr ? 0 : 3;
}
} // namespace

void ArmOverflowCapture()
{
    if (gArmed.exchange(true)) {
        return;
    }
    if (EnvSwitchOff(L"PREYVR_OVERFLOW_CAPTURE")) {
        Log("result=0 detail=off reason=PREYVR_OVERFLOW_CAPTURE");
        return;
    }
    const DWORD armed = Arm(lifecycle::LogPath());
    if (armed != 0) {
        Log("result=failed detail=arm step=" + std::to_string(armed));
        return;
    }
    Log(std::string("result=0 detail=armed on=stack_overflow dbghelp=") + (gWriteDump != nullptr ? "1" : "0") +
        " dump=\"" + Utf8(gDumpPath.wstring()) + "\"");
}

void NoteCrashThread(CrashThread role)
{
    auto& slot = gRoles[static_cast<std::size_t>(role)];
    if (slot.load(std::memory_order_relaxed) != 0) {
        return;
    }
    DWORD none = 0;
    slot.compare_exchange_strong(none, GetCurrentThreadId(), std::memory_order_relaxed);
}
} // namespace preyvr::dll
