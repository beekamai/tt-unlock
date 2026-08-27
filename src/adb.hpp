#pragma once
#include "process.hpp"
#include "util.hpp"
#include "ui.hpp"

namespace adb {

inline fs::path find_adb() {
    auto exe = util::exe_dir();
    const fs::path next_to_exe[] = {
        exe / "tools" / "adb.exe",
        exe / "adb.exe",
        exe / "platform-tools" / "adb.exe",
    };
    for (auto& c : next_to_exe) if (util::file_exists(c)) return c;

    fs::path sdk;
    if (util::env_path("ANDROID_HOME", sdk) || util::env_path("ANDROID_SDK_ROOT", sdk)) {
        auto p = sdk / "platform-tools" / "adb.exe";
        if (util::file_exists(p)) return p;
    }
    if (const char* la = std::getenv("LOCALAPPDATA")) {
        fs::path p = fs::path(la) / "Android" / "Sdk" / "platform-tools" / "adb.exe";
        if (util::file_exists(p)) return p;
    }
    if (const char* u = std::getenv("USERPROFILE")) {
        fs::path p = fs::path(u) / "AppData" / "Local" / "Android" / "Sdk" / "platform-tools" / "adb.exe";
        if (util::file_exists(p)) return p;
    }

    auto r = proc::run({"where.exe", "adb"});
    if (r.ok()) {
        for (auto& L : util::split_lines(r.out)) {
            if (util::file_exists(L)) return L;
        }
    }
    return {};
}

inline fs::path find_java() {
    auto r = proc::run({"where.exe", "java"});
    if (r.ok()) {
        for (auto& L : util::split_lines(r.out)) {
            if (util::file_exists(L)) return L;
        }
    }
    if (const char* jh = std::getenv("JAVA_HOME")) {
        fs::path p = fs::path(jh) / "bin" / "java.exe";
        if (util::file_exists(p)) return p;
    }
    // Common Windows install roots (no machine-specific versions)
    const char* roots[] = {
        "C:/Program Files/Eclipse Adoptium",
        "C:/Program Files/Java",
        "C:/Program Files/Microsoft",
        "C:/Program Files/Amazon Corretto",
    };
    for (auto* root : roots) {
        if (!util::dir_exists(root)) continue;
        for (auto& e : fs::directory_iterator(root)) {
            if (!e.is_directory()) continue;
            auto p = e.path() / "bin" / "java.exe";
            if (util::file_exists(p)) return p;
        }
    }
    return {};
}

inline fs::path find_keytool(const fs::path& java) {
    if (!java.empty()) {
        auto kt = java.parent_path() / "keytool.exe";
        if (util::file_exists(kt)) return kt;
    }
    auto r = proc::run({"where.exe", "keytool"});
    if (r.ok()) {
        for (auto& L : util::split_lines(r.out))
            if (util::file_exists(L)) return L;
    }
    return {};
}

inline fs::path find_apksigner() {
    auto try_bt = [](const fs::path& bt) -> fs::path {
        auto bat = bt / "apksigner.bat";
        if (util::file_exists(bat)) return bat;
        auto jar = bt / "lib" / "apksigner.jar";
        if (util::file_exists(jar)) return jar;
        return {};
    };
    fs::path sdk;
    if (util::env_path("ANDROID_HOME", sdk) || util::env_path("ANDROID_SDK_ROOT", sdk)) {
        fs::path bt = sdk / "build-tools";
        if (util::dir_exists(bt)) {
            // pick newest
            std::vector<fs::path> vers;
            for (auto& e : fs::directory_iterator(bt)) {
                if (e.is_directory()) vers.push_back(e.path());
            }
            std::sort(vers.begin(), vers.end());
            std::reverse(vers.begin(), vers.end());
            for (auto& v : vers) {
                auto p = try_bt(v);
                if (!p.empty()) return p;
            }
        }
    }
    if (const char* la = std::getenv("LOCALAPPDATA")) {
        fs::path bt = fs::path(la) / "Android" / "Sdk" / "build-tools";
        if (util::dir_exists(bt)) {
            std::vector<fs::path> vers;
            for (auto& e : fs::directory_iterator(bt))
                if (e.is_directory()) vers.push_back(e.path());
            std::sort(vers.begin(), vers.end());
            std::reverse(vers.begin(), vers.end());
            for (auto& v : vers) {
                auto p = try_bt(v);
                if (!p.empty()) return p;
            }
        }
    }
    // jarsigner fallback marker
    return {};
}

inline fs::path find_zipalign() {
    auto find_in = [](const fs::path& btroot) -> fs::path {
        if (!util::dir_exists(btroot)) return {};
        std::vector<fs::path> vers;
        for (auto& e : fs::directory_iterator(btroot))
            if (e.is_directory()) vers.push_back(e.path());
        std::sort(vers.begin(), vers.end());
        std::reverse(vers.begin(), vers.end());
        for (auto& v : vers) {
            auto z = v / "zipalign.exe";
            if (util::file_exists(z)) return z;
        }
        return {};
    };
    fs::path sdk;
    if (util::env_path("ANDROID_HOME", sdk) || util::env_path("ANDROID_SDK_ROOT", sdk)) {
        auto z = find_in(sdk / "build-tools");
        if (!z.empty()) return z;
    }
    if (const char* la = std::getenv("LOCALAPPDATA")) {
        auto z = find_in(fs::path(la) / "Android" / "Sdk" / "build-tools");
        if (!z.empty()) return z;
    }
    return {};
}

struct Device {
    std::string serial;
    std::string state;
};

inline std::vector<Device> devices(const fs::path& adb) {
    std::vector<Device> out;
    // short timeout — devices is instant or adb is wedged
    auto r = proc::run({adb.string(), "devices"}, {}, 15000);
    auto lines = util::split_lines(r.out + "\n" + r.err);
    for (size_t i = 0; i < lines.size(); ++i) {
        auto L = util::trim(lines[i]);
        if (L.empty() || L.find("List of devices") != std::string::npos) continue;
        auto tab = L.find('\t');
        if (tab == std::string::npos) tab = L.find(' ');
        if (tab == std::string::npos) continue;
        Device d;
        d.serial = util::trim(L.substr(0, tab));
        d.state = util::trim(L.substr(tab + 1));
        if (d.serial == "adb" || d.serial.empty()) continue;
        out.push_back(d);
    }
    return out;
}

inline proc::Result shell(const fs::path& adb, const std::string& cmd, int timeout_ms = 30000) {
    // -n: don't read stdin (critical — otherwise adb shell can block forever)
    return proc::run({adb.string(), "shell", "-n", cmd}, {}, timeout_ms);
}

inline std::vector<std::string> pm_paths(const fs::path& adb, const std::string& pkg) {
    // Use single shell string + -n to avoid stdin hang
    auto r = proc::run({adb.string(), "shell", "-n", "pm", "path", pkg}, {}, 30000);
    std::vector<std::string> paths;
    for (auto& L : util::split_lines(r.out)) {
        auto t = util::trim(L);
        if (t.rfind("package:", 0) == 0) paths.push_back(t.substr(8));
    }
    // fallback without -n if old adb
    if (paths.empty() && !r.timed_out()) {
        r = proc::run({adb.string(), "shell", "pm path " + pkg}, {}, 30000);
        for (auto& L : util::split_lines(r.out)) {
            auto t = util::trim(L);
            if (t.rfind("package:", 0) == 0) paths.push_back(t.substr(8));
        }
    }
    return paths;
}

inline bool pull(const fs::path& adb, const std::string& remote, const fs::path& local) {
    fs::create_directories(local.parent_path());
    auto r = proc::run({adb.string(), "pull", remote, local.string()}, {}, 600000);
    return r.ok() && util::file_exists(local);
}

inline bool uninstall(const fs::path& adb, const std::string& pkg) {
    auto r = proc::run({adb.string(), "uninstall", pkg}, {}, 120000);
    return r.ok() || r.out.find("Success") != std::string::npos ||
           r.err.find("Success") != std::string::npos ||
           r.out.find("DELETE_FAILED") != std::string::npos;
}

inline proc::Result install_multiple(const fs::path& adb, const std::vector<fs::path>& apks) {
    std::vector<std::string> args = {adb.string(), "install-multiple", "-r"};
    for (auto& p : apks) args.push_back(p.string());
    return proc::run(args, {}, 600000);
}

inline bool install_ok(const proc::Result& r) {
    return r.ok() || r.out.find("Success") != std::string::npos;
}

/* Turns adb's INSTALL_FAILED_* into the one thing the user has to go and do.
 * Without this the tool used to guess "allow unknown sources", which is the
 * wrong setting for adb installs and sent people looking in the wrong menu. */
inline std::string install_hint(const proc::Result& r) {
    const std::string t = r.out + "\n" + r.err;
    auto has = [&t](const char* s) { return t.find(s) != std::string::npos; };

    if (has("INSTALL_FAILED_USER_RESTRICTED"))
        return "Телефон запретил установку с ПК.\n"
               "Настройки → Для разработчиков → включи «Установка через USB».\n"
               "На Xiaomi нужен вход в Mi-аккаунт, SIM и интернет, иначе тумблер отскакивает.";
    if (has("INSTALL_FAILED_ABORTED") || has("abandon") || has("Session was abandoned"))
        return "Установку отклонили на экране телефона.\n"
               "Повтори и нажми «Установить» в окне, которое появится на телефоне.";
    if (has("INSTALL_FAILED_VERIFICATION_FAILURE") || has("INSTALL_FAILED_VERIFICATION_TIMEOUT"))
        return "Заблокировал Play Protect.\n"
               "Play Маркет → профиль → Play Защита → выключи проверку на время установки.";
    if (has("INSTALL_FAILED_MISSING_SPLIT"))
        return "Набору не хватает модулей: TikTok на телефоне был неполный.\n"
               "Поставь TikTok из Play, полистай ленту и открой чей-нибудь профиль, потом патчь заново.";
    if (has("INSTALL_FAILED_UPDATE_INCOMPATIBLE") || has("signatures do not match"))
        return "На телефоне остался TikTok с другой подписью — его надо удалить.\n"
               "Проверь, что удалены все профили (рабочий профиль / вторая учётка).";
    if (has("INSTALL_FAILED_INSUFFICIENT_STORAGE"))
        return "На телефоне не хватает места — нужно ~1 GB свободного.";
    if (has("INSTALL_FAILED_INVALID_APK") || has("INSTALL_PARSE_FAILED"))
        return "Android не принял APK. Скорее всего набор splits неполный или битый.";
    return {};
}

/* Which store installed the package: com.android.vending == Google Play. */
inline std::string installer_of(const fs::path& adb, const std::string& pkg) {
    auto r = proc::run({adb.string(), "shell", "-n",
                        "pm list packages -i " + pkg}, {}, 30000);
    for (auto& L : util::split_lines(r.out)) {
        auto pos = L.find("installer=");
        if (pos != std::string::npos) return util::trim(L.substr(pos + 10));
    }
    return {};
}

} // namespace adb
