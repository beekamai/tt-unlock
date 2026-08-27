#pragma once
/* HTTPS download over WinHTTP — no curl, no DLLs, no cert pinning holes.
 *
 * Redirects are followed by WinHTTP itself, except https -> http, which stays
 * blocked (default policy). Server certificates are validated; we never set
 * SECURITY_FLAG_IGNORE_* — a MITM must not be able to feed us a patched SDK.
 *
 * Download is streamed to disk through a .part file and only renamed on a
 * clean finish, so an aborted transfer can never look like a complete tool.
 */
#include "process.hpp"
#include "util.hpp"

#include <winhttp.h>
#include <functional>
#include <string>

namespace net {

/* fraction 0..1, or -1 when the server sent no Content-Length */
using ProgressFn = std::function<void(double fraction, uint64_t got, uint64_t total)>;

struct Handle {
    HINTERNET h = nullptr;
    explicit Handle(HINTERNET x = nullptr) : h(x) {}
    ~Handle() { if (h) WinHttpCloseHandle(h); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    operator HINTERNET() const { return h; }
    explicit operator bool() const { return h != nullptr; }
};

inline bool download(const std::string& url, const fs::path& dest,
                     const ProgressFn& on_progress, std::string& error) {
    std::wstring wurl = proc::to_wide(url);

    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256]{}, path[2048]{};
    uc.lpszHostName = host;      uc.dwHostNameLength = 255;
    uc.lpszUrlPath  = path;      uc.dwUrlPathLength  = 2047;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        error = "bad url: " + url;
        return false;
    }

    Handle session(WinHttpOpen(L"tt-unlock/0.3",
                               WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) { error = "WinHttpOpen failed"; return false; }

    /* Slow mobile tethering is common here; be patient but never infinite. */
    WinHttpSetTimeouts(session, 15000, 15000, 60000, 60000);

    Handle conn(WinHttpConnect(session, host, uc.nPort, 0));
    if (!conn) { error = "connect failed: " + std::string(url); return false; }

    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    Handle req(WinHttpOpenRequest(conn, L"GET", path, nullptr,
                                  WINHTTP_NO_REFERER,
                                  WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!req) { error = "request failed"; return false; }

    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(req, nullptr)) {
        error = "no response (нет сети или блокировка): " + url;
        return false;
    }

    DWORD status = 0, len = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) {
        error = "HTTP " + std::to_string(status) + " для " + url;
        return false;
    }

    uint64_t total = 0;
    {
        wchar_t buf[64]{};
        DWORD blen = sizeof(buf);
        if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH,
                                WINHTTP_HEADER_NAME_BY_INDEX, buf, &blen,
                                WINHTTP_NO_HEADER_INDEX)) {
            total = _wcstoui64(buf, nullptr, 10);
        }
    }

    std::error_code ec;
    fs::create_directories(dest.parent_path(), ec);
    fs::path part = dest;
    part += ".part";

    std::ofstream out(part, std::ios::binary | std::ios::trunc);
    if (!out) { error = "не могу писать " + part.string(); return false; }

    std::vector<char> buf(256 * 1024);
    uint64_t got = 0;
    int64_t last_tick = 0;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail)) { error = "обрыв соединения"; return false; }
        if (avail == 0) break;
        if (avail > buf.size()) avail = static_cast<DWORD>(buf.size());

        DWORD read = 0;
        if (!WinHttpReadData(req, buf.data(), avail, &read) || read == 0) {
            error = "обрыв при чтении";
            return false;
        }
        out.write(buf.data(), static_cast<std::streamsize>(read));
        if (!out) { error = "ошибка записи на диск (место кончилось?)"; return false; }
        got += read;

        int64_t now = util::now_ms();
        if (on_progress && now - last_tick > 120) {
            last_tick = now;
            on_progress(total ? static_cast<double>(got) / static_cast<double>(total) : -1.0,
                        got, total);
        }
    }
    out.close();

    if (total && got != total) {
        error = "недокачано: " + util::human_size(got) + " из " + util::human_size(total);
        fs::remove(part, ec);
        return false;
    }
    if (on_progress) on_progress(1.0, got, total ? total : got);

    fs::remove(dest, ec);
    fs::rename(part, dest, ec);
    if (ec) { error = "не могу переименовать " + part.string(); return false; }
    return true;
}

} // namespace net
