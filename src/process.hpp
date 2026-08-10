#pragma once
#include "util.hpp"
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <cstdio>

namespace proc {

struct Result {
    int exit_code = -1;
    std::string out;
    std::string err;
    bool ok() const { return exit_code == 0; }
    bool timed_out() const { return exit_code == -2; }
};

#ifdef _WIN32

inline std::wstring to_wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

inline std::string quote_arg(const std::string& a) {
    if (a.find_first_of(" \t\"") == std::string::npos) return a;
    std::string r = "\"";
    for (char c : a) {
        if (c == '"') r += "\\\"";
        else r += c;
    }
    r += '"';
    return r;
}

// Default 120s — never hang forever on adb/java.
inline Result run(const std::vector<std::string>& args, const fs::path& cwd = {},
                  int timeout_ms = 120000) {
    Result r;
    if (args.empty()) return r;

    std::string cmd;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) cmd.push_back(' ');
        cmd += quote_arg(args[i]);
    }

    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE out_r = nullptr, out_w = nullptr;
    HANDLE err_r = nullptr, err_w = nullptr;
    HANDLE in_r = nullptr, in_w = nullptr;
    if (!CreatePipe(&out_r, &out_w, &sa, 0) ||
        !CreatePipe(&err_r, &err_w, &sa, 0) ||
        !CreatePipe(&in_r, &in_w, &sa, 0)) {
        r.err = "CreatePipe failed";
        return r;
    }
    // Parent keeps read ends of out/err and write end of in; child inherits the other sides.
    SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(err_r, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0);
    // Close our write-to-child-stdin immediately so child sees EOF on stdin
    // (prevents adb/java from blocking on inherited console input).
    CloseHandle(in_w);
    in_w = nullptr;

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = out_w;
    si.hStdError = err_w;
    si.hStdInput = in_r; // empty pipe, EOF

    PROCESS_INFORMATION pi{};
    std::wstring wcmd = to_wide(cmd);
    std::wstring wcwd = cwd.empty() ? std::wstring() : cwd.wstring();

    BOOL ok = CreateProcessW(
        nullptr,
        wcmd.data(),
        nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP,
        nullptr,
        wcwd.empty() ? nullptr : wcwd.c_str(),
        &si, &pi);

    // Child has inherited these; parent must close write ends so ReadFile gets EOF.
    CloseHandle(out_w);
    CloseHandle(err_w);
    CloseHandle(in_r);

    if (!ok) {
        CloseHandle(out_r);
        CloseHandle(err_r);
        r.err = "CreateProcess failed: " + cmd;
        return r;
    }

    std::string out_acc, err_acc;
    std::atomic<bool> done{false};

    auto reader = [](HANDLE h, std::string* acc, std::atomic<bool>* done_flag) {
        char buf[4096];
        DWORD n = 0;
        while (ReadFile(h, buf, sizeof(buf), &n, nullptr) && n > 0) {
            acc->append(buf, buf + n);
        }
        (void)done_flag;
    };

    std::thread t_out(reader, out_r, &out_acc, &done);
    std::thread t_err(reader, err_r, &err_acc, &done);

    DWORD wait = timeout_ms > 0 ? static_cast<DWORD>(timeout_ms) : INFINITE;
    DWORD wr = WaitForSingleObject(pi.hProcess, wait);
    if (wr == WAIT_TIMEOUT) {
        // Kill process tree best-effort
        TerminateProcess(pi.hProcess, 1);
        r.err = "timeout after " + std::to_string(timeout_ms) + "ms: " + cmd;
        r.exit_code = -2;
    } else {
        DWORD code = 1;
        GetExitCodeProcess(pi.hProcess, &code);
        r.exit_code = static_cast<int>(code);
    }

    // Ensure readers unblock (process dead → pipes close)
    if (t_out.joinable()) t_out.join();
    if (t_err.joinable()) t_err.join();

    r.out = std::move(out_acc);
    if (!r.err.empty() && !err_acc.empty()) r.err += "\n";
    r.err += err_acc;

    CloseHandle(out_r);
    CloseHandle(err_r);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return r;
}

#else
inline Result run(const std::vector<std::string>& args, const fs::path& cwd = {}, int = 120000) {
    Result r;
    r.err = "unsupported platform";
    (void)args; (void)cwd;
    return r;
}
#endif

inline Result run_shell(const std::string& cmdline, const fs::path& cwd = {}, int timeout_ms = 120000) {
#ifdef _WIN32
    return run({"cmd.exe", "/C", cmdline}, cwd, timeout_ms);
#else
    return run({"/bin/sh", "-c", cmdline}, cwd, timeout_ms);
#endif
}

} // namespace proc
