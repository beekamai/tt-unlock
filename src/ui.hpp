#pragma once
#include "util.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <cmath>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <conio.h>
#endif

namespace ui {

// ── ANSI / theme ──────────────────────────────────────────────
namespace c {
    constexpr const char* reset   = "\x1b[0m";
    constexpr const char* bold    = "\x1b[1m";
    constexpr const char* dim     = "\x1b[2m";
    constexpr const char* italic  = "\x1b[3m";
    constexpr const char* hide    = "\x1b[?25l";
    constexpr const char* show    = "\x1b[?25h";
    constexpr const char* clear   = "\x1b[2J\x1b[H";
    constexpr const char* clearln = "\x1b[2K\r";

    constexpr const char* pink    = "\x1b[38;2;255;105;180m";
    constexpr const char* rose    = "\x1b[38;2;255;80;120m";
    constexpr const char* cyan    = "\x1b[38;2;100;220;255m";
    constexpr const char* mint    = "\x1b[38;2;80;255;180m";
    constexpr const char* gold    = "\x1b[38;2;255;200;80m";
    constexpr const char* violet  = "\x1b[38;2;180;140;255m";
    constexpr const char* white   = "\x1b[38;2;240;240;245m";
    constexpr const char* gray    = "\x1b[38;2;140;140;155m";
    constexpr const char* red     = "\x1b[38;2;255;90;90m";
    constexpr const char* green   = "\x1b[38;2;100;255;140m";
    constexpr const char* bg_dark = "\x1b[48;2;18;18;28m";
}

inline void enable_vt() {
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE hIn  = GetStdHandle(STD_INPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        GetConsoleMode(hOut, &mode);
        mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT;
        SetConsoleMode(hOut, mode);
    }
    if (hIn != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        GetConsoleMode(hIn, &mode);
        mode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
        SetConsoleMode(hIn, mode);
    }
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    // enlarge buffer a bit
    SMALL_RECT r{0, 0, 99, 39};
    SetConsoleWindowInfo(hOut, TRUE, &r);
#endif
}

inline void sleep_ms(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

inline void hide_cursor() { std::cout << c::hide << std::flush; }
inline void show_cursor() { std::cout << c::show << std::flush; }

inline int term_cols() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
        return info.srWindow.Right - info.srWindow.Left + 1;
    }
#endif
    return 100;
}

inline void clear() { std::cout << c::clear << std::flush; }

inline std::string repeat(const std::string& s, int n) {
    std::string r;
    r.reserve(s.size() * n);
    for (int i = 0; i < n; ++i) r += s;
    return r;
}

inline void print_center(const std::string& s, const char* color = c::white) {
    int cols = term_cols();
    // rough width: ignore ansi, assume ascii/utf8 display width ~ size for our strings
    int pad = std::max(0, (cols - static_cast<int>(s.size())) / 2);
    std::cout << std::string(pad, ' ') << color << s << c::reset << "\n";
}

inline void hr(const char* color = c::violet) {
    int cols = std::min(term_cols() - 4, 72);
    std::cout << "  " << color << repeat("─", cols) << c::reset << "\n";
}

inline void banner() {
    clear();
    hide_cursor();
    std::cout << "\n";
    const char* lines[] = {
        R"(  ████████╗████████╗      ██╗   ██╗███╗   ██╗██╗      ██████╗  ██████╗██╗  ██╗)",
        R"(  ╚══██╔══╝╚══██╔══╝      ██║   ██║████╗  ██║██║     ██╔═══██╗██╔════╝██║ ██╔╝)",
        R"(     ██║      ██║   █████╗██║   ██║██╔██╗ ██║██║     ██║   ██║██║     █████╔╝ )",
        R"(     ██║      ██║   ╚════╝██║   ██║██║╚██╗██║██║     ██║   ██║██║     ██╔═██╗ )",
        R"(     ██║      ██║         ╚██████╔╝██║ ╚████║███████╗╚██████╔╝╚██████╗██║  ██╗)",
        R"(     ╚═╝      ╚═╝          ╚═════╝ ╚═╝  ╚═══╝╚══════╝ ╚═════╝  ╚═════╝╚═╝  ╚═╝)",
    };
    for (auto* L : lines) {
        std::cout << c::pink << c::bold << L << c::reset << "\n";
        sleep_ms(28);
    }
    std::cout << "\n";
    print_center("TikTok SIM spoof  ·  DE Telekom  ·  adb DEX patcher", c::cyan);
    print_center("v0.2.0  ·  static Windows build  ·  educational tooling", c::gray);
    std::cout << "\n";
    hr();
    std::cout << "\n";
}

inline void box_line(const std::string& text, const char* color = c::white) {
    std::cout << "  " << c::violet << "│ " << c::reset << color << text << c::reset << "\n";
}

inline void section(const std::string& title) {
    std::cout << "\n  " << c::gold << c::bold << "▸ " << title << c::reset << "\n";
    hr(c::gray);
}

inline void ok(const std::string& msg) {
    std::cout << "  " << c::green << "✓ " << c::white << msg << c::reset << "\n";
}
inline void fail(const std::string& msg) {
    std::cout << "  " << c::red << "✗ " << c::white << msg << c::reset << "\n";
}
inline void warn(const std::string& msg) {
    std::cout << "  " << c::gold << "! " << c::white << msg << c::reset << "\n";
}
inline void info(const std::string& msg) {
    std::cout << "  " << c::cyan << "· " << c::white << msg << c::reset << "\n";
}
inline void dim(const std::string& msg) {
    std::cout << "  " << c::gray << "  " << msg << c::reset << "\n";
}

inline void progress_bar(double t, int width = 40, const std::string& label = "") {
    t = std::clamp(t, 0.0, 1.0);
    int fill = static_cast<int>(t * width);
    std::cout << c::clearln << "  " << c::violet << "[" << c::pink;
    for (int i = 0; i < width; ++i) {
        if (i < fill) std::cout << "█";
        else if (i == fill) std::cout << c::cyan << "▓" << c::gray;
        else std::cout << c::gray << "░";
    }
    std::cout << c::violet << "] " << c::white;
    char pct[16];
    std::snprintf(pct, sizeof(pct), "%3.0f%%", t * 100.0);
    std::cout << pct;
    if (!label.empty()) std::cout << c::gray << "  " << label;
    std::cout << c::reset << std::flush;
}

class Spinner {
public:
    explicit Spinner(std::string label) : label_(std::move(label)), run_(true) {
        th_ = std::thread([this] {
            static const char* frames[] = {"⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏"};
            int i = 0;
            while (run_) {
                {
                    std::lock_guard<std::mutex> g(mu_);
                    std::cout << c::clearln << "  " << c::pink << frames[i % 10]
                              << c::white << " " << label_ << c::reset << std::flush;
                }
                i++;
                sleep_ms(70);
            }
        });
    }
    void set_label(std::string l) {
        std::lock_guard<std::mutex> g(mu_);
        label_ = std::move(l);
    }
    void stop(bool success = true, const std::string& final_msg = {}) {
        run_ = false;
        if (th_.joinable()) th_.join();
        std::cout << c::clearln;
        if (!final_msg.empty()) {
            if (success) ok(final_msg);
            else fail(final_msg);
        }
    }
    ~Spinner() {
        if (run_) stop(true, {});
    }
private:
    std::string label_;
    std::atomic<bool> run_;
    std::thread th_;
    std::mutex mu_;
};

inline void typewrite(const std::string& s, const char* color = c::white, int delay = 12) {
    std::cout << "  " << color;
    for (char ch : s) {
        std::cout << ch << std::flush;
        if (ch != ' ') sleep_ms(delay);
    }
    std::cout << c::reset << "\n";
}

inline int menu(const std::string& title, const std::vector<std::string>& items) {
    section(title);
    for (size_t i = 0; i < items.size(); ++i) {
        std::cout << "  " << c::pink << c::bold << " [" << (i + 1) << "] "
                  << c::reset << c::white << items[i] << c::reset << "\n";
        sleep_ms(40);
    }
    std::cout << "\n  " << c::gray << "Выбор (1-" << items.size() << "), Enter: " << c::reset
              << std::flush;
    show_cursor();
    std::string line;
    std::getline(std::cin, line);
    hide_cursor();
    line = util::trim(line);
    if (line.empty()) return 1;
    try {
        int v = std::stoi(line);
        if (v >= 1 && v <= static_cast<int>(items.size())) return v;
    } catch (...) {}
    return -1;
}

inline void pause(const std::string& msg = "Нажми Enter, чтобы продолжить…") {
    show_cursor();
    std::cout << "\n  " << c::gold << c::bold << ">>> " << c::white << msg
              << c::reset << std::endl << std::flush;
    // Drop any leftover chars in the buffer, then wait for a real Enter.
#ifdef _WIN32
    // If stdin is not a console (piped), don't block forever.
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    if (hIn != INVALID_HANDLE_VALUE && GetConsoleMode(hIn, &mode)) {
        std::string line;
        std::getline(std::cin, line);
    } else {
        // non-console: brief pause
        sleep_ms(300);
    }
#else
    std::string line;
    std::getline(std::cin, line);
#endif
    hide_cursor();
}

inline void checklist_permissions() {
    section("Перед стартом — чеклист");
    box_line("1. На телефоне: Настройки → Для разработчиков");
    box_line("2. Включи «Отладка по USB»");
    box_line("3. (MIUI) «Отладка USB (параметры безопасности)» — если есть");
    box_line("4. Подключи кабель, на телефоне «Разрешить отладку» → OK");
    box_line("5. VPN (DE/не-RU exit) включён — Happ / свой клиент");
    box_line("6. Java 17+ в PATH (для apksigner / keytool)");
    box_line("7. TikTok можно снести — данные/сессия сбросятся");
    std::cout << "\n";
    warn("Переподпись убивает Play Integrity → Google login обычно НЕ работает.");
    info("Вход по почте / username+password — ок (проверено).");
    info("SIM spoof: ISO de + MCC 26201 + Telekom (+ hub 155y → DE)");
}

inline void show_steps_overview() {
    section("Что сделает патчер");
    const char* steps[] = {
        "Найдёт adb и устройство",
        "Снимет split-APK TikTok (com.zhiliaoapp.musically)",
        "Найдёт BPEA TelephonyManager wrappers + hub 155y",
        "Заглушит ISO/MCC/name → de / 26201 / Telekom",
        "Подпишет все splits одним ключом",
        "Удалит stock TikTok и поставит патч",
    };
    for (int i = 0; i < 6; ++i) {
        sleep_ms(80);
        std::cout << "  " << c::cyan << "  " << (i + 1) << "/6  "
                  << c::white << steps[i] << c::reset << "\n";
    }
}

inline void success_finale() {
    std::cout << "\n";
    hr(c::mint);
    print_center("✦  ПАТЧ УСТАНОВЛЕН  ✦", c::mint);
    hr(c::mint);
    std::cout << "\n";
    ok("TikTok переустановлен с SIM spoof → DE Telekom");
    info("Открой приложение → день рождения 18+ → гость или почта");
    info("VPN DE включи ДО первого открытия (иначе server device_id)");
    warn("Не обновляй TikTok из Play — снесёт патч. Гоняй этот тул снова.");
    std::cout << "\n";
}

} // namespace ui
