#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <cstdint>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <system_error>

namespace fs = std::filesystem;

namespace util {

inline fs::path exe_dir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (!n) return fs::current_path();
    return fs::path(buf).parent_path();
#else
    return fs::current_path();
#endif
}

inline bool file_exists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec) && fs::is_regular_file(p, ec);
}

inline bool dir_exists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec) && fs::is_directory(p, ec);
}

inline uint64_t file_size(const fs::path& p) {
    std::error_code ec;
    auto s = fs::file_size(p, ec);
    return ec ? 0 : static_cast<uint64_t>(s);
}

inline std::vector<uint8_t> read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    in.seekg(0, std::ios::end);
    auto sz = in.tellg();
    in.seekg(0, std::ios::beg);
    if (sz <= 0) return {};
    std::vector<uint8_t> data(static_cast<size_t>(sz));
    in.read(reinterpret_cast<char*>(data.data()), sz);
    return data;
}

inline bool write_file(const fs::path& p, const void* data, size_t len) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(len));
    return static_cast<bool>(out);
}

inline bool write_file(const fs::path& p, const std::vector<uint8_t>& data) {
    return write_file(p, data.data(), data.size());
}

inline std::string trim(std::string s) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

inline std::vector<std::string> split_lines(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream iss(s);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(line);
    }
    return out;
}

inline int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

inline std::string human_size(uint64_t n) {
    const char* u[] = {"B", "KB", "MB", "GB"};
    double v = static_cast<double>(n);
    int i = 0;
    while (v >= 1024.0 && i < 3) { v /= 1024.0; ++i; }
    char buf[64];
    std::snprintf(buf, sizeof(buf), i == 0 ? "%.0f %s" : "%.1f %s", v, u[i]);
    return buf;
}

inline fs::path temp_work_dir() {
    auto base = fs::temp_directory_path() / "tt-unlock";
    std::error_code ec;
    fs::create_directories(base, ec);
    return base;
}

inline bool env_path(const char* name, fs::path& out) {
#ifdef _WIN32
    wchar_t wname[128];
    MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, 128);
    DWORD n = GetEnvironmentVariableW(wname, nullptr, 0);
    if (!n) return false;
    std::wstring buf(n, L'\0');
    GetEnvironmentVariableW(wname, buf.data(), n);
    if (!buf.empty() && buf.back() == L'\0') buf.pop_back();
    out = buf;
    return !out.empty();
#else
    const char* v = std::getenv(name);
    if (!v || !*v) return false;
    out = v;
    return true;
#endif
}

} // namespace util
