// Exercise the stack-overflow capture: its handler's filter, the thread roles,
// the unwind and the opt-out in this process; then, in a child process of this
// same test, a real overflow on a thread of its own, and the dump, report and
// log line the capture leaves behind. Including this TU exposes internal seams
// only inside this test executable.
#include "../src/dll/OverflowCapture.cpp"
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include "DllTestSupport.h"
#include "TestSupport.h"

// The log: captured, and in the child also appended to PreyVR.log beside the
// dump, where the parent reads it.
namespace {
bool gChild = false;
std::filesystem::path gLog;
} // namespace
void preyvr::lifecycle::Log(std::string_view line)
{
    preyvr::test::gLines.emplace_back(line);
    if (gChild) {
        std::ofstream(gLog, std::ios::binary | std::ios::app) << line << "\n";
    }
}
std::filesystem::path preyvr::lifecycle::LogPath() { return gLog; }

namespace {
using namespace preyvr::dll;
using preyvr::test::gLines;
using preyvr::test::Logged;
using preyvr::test::Require;

// Never reached: it only keeps the compiler from proving the recursion endless.
volatile unsigned gStopDepth = 0xFFFFFFFF;

__declspec(noinline) bool Recurse(unsigned depth)
{
    volatile char pad[512];
    pad[0] = static_cast<char>(depth);
    if (depth == gStopDepth) return true;
    const bool deeper = Recurse(depth + 1);
    return deeper && pad[0] != 1; // used after the call: no tail call
}

DWORD WINAPI Overflow(void*)
{
    NoteCrashThread(CrashThread::render);
    return Recurse(1) ? 0 : 1;
}

// Ends the child with the exception's code, so no error report is made.
LONG WINAPI EndWithCode(EXCEPTION_POINTERS* pointers)
{
    TerminateProcess(GetCurrentProcess(), pointers->ExceptionRecord->ExceptionCode);
    return EXCEPTION_EXECUTE_HANDLER;
}

int RunChild(const std::filesystem::path& dir)
{
    gChild = true;
    gLog = dir / L"PreyVR.log";
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(&EndWithCode);
    ArmOverflowCapture();
    NoteCrashThread(CrashThread::main);
    // A small stack, as a job worker's might be.
    HANDLE thread = CreateThread(nullptr, 128 * 1024, &Overflow, nullptr, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (thread != nullptr) WaitForSingleObject(thread, 60000);
    return 3; // the overflow ends the process before this
}

std::filesystem::path SelfPath()
{
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return path;
}

std::string ReadAll(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

void TestFilter()
{
    Require(OnException(nullptr) == EXCEPTION_CONTINUE_SEARCH, "no pointers: passed on");
    EXCEPTION_RECORD record{};
    CONTEXT context{};
    EXCEPTION_POINTERS pointers{&record, &context};
    for (const DWORD code : {static_cast<DWORD>(EXCEPTION_ACCESS_VIOLATION), 0xE06D7363ul, 0x406D1388ul}) {
        record.ExceptionCode = code;
        Require(OnException(&pointers) == EXCEPTION_CONTINUE_SEARCH && gFired == 0 && gFault.thread == 0,
                "anything but a stack overflow is passed on untouched");
    }
}

void TestRoles()
{
    Require(std::string(RoleName(GetCurrentThreadId())) == "other", "unnamed: other");
    NoteCrashThread(CrashThread::worker);
    Require(std::string(RoleName(GetCurrentThreadId())) == "worker", "named by the first call");
    std::thread([] { NoteCrashThread(CrashThread::worker); }).join();
    Require(std::string(RoleName(GetCurrentThreadId())) == "worker", "a later thread does not take the role");
    Require(std::string(RoleName(0)) == "other", "no thread: other");
    gRoles[static_cast<std::size_t>(CrashThread::worker)].store(0);
}

void TestNames()
{
    Modules modules;
    const auto self = Utf8(SelfPath().filename().wstring());
    const auto inside = modules.Name(reinterpret_cast<std::uintptr_t>(&Recurse));
    Require(inside.rfind(self + "+0x", 0) == 0, "an address in a module: module+0xRVA");
    Require(modules.Name(0x10) == "0x10", "outside any module: the address");
    Require(Kilobytes(0x30000, 0x10000) == "128" && Kilobytes(0x10000, 0) == "?" && Kilobytes(1, 2) == "?",
            "sizes in KB, ? when unknown");
}

__declspec(noinline) std::size_t UnwindHere(bool& truncated)
{
    CONTEXT context{};
    RtlCaptureContext(&context);
    const auto* tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
    const auto low = *reinterpret_cast<const std::uintptr_t*>(reinterpret_cast<const std::uint8_t*>(NtCurrentTeb()) +
                                                              kTebDeallocationStack);
    return Unwind(context, low, reinterpret_cast<std::uintptr_t>(tib->StackBase), &truncated);
}

void TestUnwind()
{
    bool truncated = true;
    const std::size_t depth = UnwindHere(truncated);
    Require(depth >= 3 && depth < 64 && !truncated, "this thread's frames, to the stack's end");
    Modules modules;
    Require(modules.Name(gFrames[0]).find("+0x") != std::string::npos, "the first frame is in a module");
}

void TestOptOut()
{
    SetEnvironmentVariableW(L"PREYVR_OVERFLOW_CAPTURE", L"0");
    ArmOverflowCapture();
    Require(Logged("preyvr_crash result=0 detail=off reason=PREYVR_OVERFLOW_CAPTURE") && gHandler == nullptr,
            "opted out: nothing armed");
    SetEnvironmentVariableW(L"PREYVR_OVERFLOW_CAPTURE", nullptr);
    gLines.clear();
    ArmOverflowCapture();
    Require(gLines.empty() && gHandler == nullptr, "and only ever once");
}

void TestRealOverflow()
{
    const auto self = SelfPath();
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    const auto dir = std::filesystem::path(temp) / (L"preyvr-overflow-test-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code error;
    std::filesystem::remove_all(dir, error);
    std::filesystem::create_directories(dir);

    std::wstring command = L"\"" + self.wstring() + L"\" --overflow \"" + dir.wstring() + L"\"";
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    Require(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                           &startup, &process) != FALSE,
            "the child starts");
    Require(WaitForSingleObject(process.hProcess, 120000) == WAIT_OBJECT_0, "and ends");
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    Require(code == EXCEPTION_STACK_OVERFLOW, "with the overflow's code, as Prey would");

    std::filesystem::path dump, report;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        const auto name = entry.path().filename().wstring();
        if (name.rfind(L"preyvr-overflow-", 0) != 0) continue;
        if (entry.path().extension() == L".dmp") dump = entry.path();
        if (entry.path().extension() == L".txt") report = entry.path();
    }
    Require(!dump.empty() && std::filesystem::file_size(dump) > 4096 && ReadAll(dump).rfind("MDMP", 0) == 0,
            "a minidump beside the log");
    const std::string text = ReadAll(report);
    Require(!report.empty() && text.find("role=render") != std::string::npos &&
                text.find("exception=0xC00000FD") != std::string::npos,
            "a report naming the thread and the exception");
    Require(text.find("stack reserveKb=128 ") != std::string::npos, "the faulting thread's own stack size");
    const auto repeating = text.find("repeating:\n  ");
    const auto selfName = Utf8(self.filename().wstring());
    Require(repeating != std::string::npos && text.compare(repeating + 13, selfName.size(), selfName) == 0,
            "the recursion's return address repeats most, in this module");
    const auto times = text.find(" x", repeating);
    Require(times != std::string::npos && std::stoul(text.substr(times + 2)) > 100, "hundreds of times");
    const std::string log = ReadAll(dir / L"PreyVR.log");
    Require(log.find("preyvr_crash result=0 detail=armed on=stack_overflow dbghelp=1") != std::string::npos,
            "armed, with dbghelp");
    Require(log.find("preyvr_crash detail=stack_overflow thread=") != std::string::npos &&
                log.find(" role=render reserveKb=128 ") != std::string::npos &&
                log.find(" dump=ok report=") != std::string::npos,
            "and one line in the log");
    std::filesystem::remove_all(dir, error);
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
    if (argc == 3 && std::wstring(argv[1]) == L"--overflow") {
        return RunChild(argv[2]);
    }
    TestFilter();
    TestRoles();
    TestNames();
    TestUnwind();
    TestOptOut();
    TestRealOverflow();
    std::cout << "overflow capture tests passed\n";
    return 0;
}
