#pragma once
/* Fetches the toolchain a clean Windows box is missing: adb, apksigner,
 * zipalign, java, keytool.
 *
 * Everything lands in %LOCALAPPDATA%\tt-unlock\sdk — no admin rights, no
 * installer, no PATH edits, nothing left behind on uninstall except that one
 * folder. Sources are the vendors' own hosts (dl.google.com, Adoptium API).
 *
 * We take the JRE rather than a full JDK: Temurin ships keytool.exe in it, so
 * signing works and the download drops from ~190 MB to ~47 MB.
 *
 * apksigner is used as lib/apksigner.jar via our own java.exe, never as
 * apksigner.bat — the .bat resolves `java` through PATH/JAVA_HOME, which is
 * exactly what is missing on the machines this feature exists for.
 */
#include "netfetch.hpp"
#include "unzip.hpp"
#include "ui.hpp"
#include "util.hpp"

#include <string>
#include <vector>

namespace boot {

struct Component {
    const char* key;     // subfolder under sdk root
    const char* title;   // shown to the user
    const char* url;
    const char* marker;  // file that proves the component is usable
    const char* size_hint;
};

inline const Component kPlatformTools{
    "platform-tools",
    "Android platform-tools (adb)",
    "https://dl.google.com/android/repository/platform-tools-latest-windows.zip",
    "adb.exe",
    "~8 MB"
};

inline const Component kBuildTools{
    "build-tools",
    "Android build-tools (apksigner, zipalign)",
    "https://dl.google.com/android/repository/build-tools_r34-windows.zip",
    "apksigner.jar",
    "~56 MB"
};

inline const Component kJava{
    "jre",
    "Java 21 JRE (Eclipse Temurin)",
    "https://api.adoptium.net/v3/binary/latest/21/ga/windows/x64/jre/hotspot/normal/eclipse",
    "java.exe",
    "~47 MB"
};

inline fs::path component_dir(const Component& c) {
    return util::sdk_root() / c.key;
}

inline fs::path locate(const Component& c) {
    return zipx::find_under(component_dir(c), c.marker);
}

inline bool installed(const Component& c) {
    return !locate(c).empty();
}

/* Downloads and unpacks one component. Safe to call when it is already
 * present — returns true immediately. */
inline bool install(const Component& c) {
    if (installed(c)) return true;

    fs::path dir = component_dir(c);
    fs::path cache = util::sdk_root() / "cache";
    fs::path zip = cache / (std::string(c.key) + ".zip");

    std::error_code ec;
    fs::create_directories(cache, ec);

    ui::info(std::string(c.title) + "  " + c.size_hint);
    std::string error;
    bool got = net::download(c.url, zip, [](double frac, uint64_t done, uint64_t total) {
        std::string label = total
            ? util::human_size(done) + " / " + util::human_size(total)
            : util::human_size(done);
        ui::progress_bar(frac < 0 ? 0.0 : frac, 34, label);
    }, error);
    std::cout << "\n";

    if (!got) {
        ui::fail("Скачать не вышло: " + error);
        fs::remove(zip, ec);
        return false;
    }

    ui::Spinner sp("Распаковываю " + std::string(c.key) + "…");
    /* A leftover half-extracted folder would make locate() lie. */
    fs::remove_all(dir, ec);
    bool unpacked = zipx::extract(zip, dir, error);
    fs::remove(zip, ec);

    if (!unpacked) {
        sp.stop(false, "Распаковка не удалась");
        ui::fail(error);
        fs::remove_all(dir, ec);
        return false;
    }

    auto found = zipx::find_under(dir, c.marker);
    if (found.empty()) {
        sp.stop(false, "В архиве нет " + std::string(c.marker));
        fs::remove_all(dir, ec);
        return false;
    }
    sp.stop(true, std::string(c.title) + " готов");
    return true;
}

/* Convenience lookups used to fill Tools after (or without) an install. */
inline fs::path find_adb()       { return locate(kPlatformTools); }
inline fs::path find_java()      { return locate(kJava); }
inline fs::path find_keytool()   { return zipx::find_under(component_dir(kJava), "keytool.exe"); }
inline fs::path find_apksigner() { return locate(kBuildTools); }
inline fs::path find_zipalign()  { return zipx::find_under(component_dir(kBuildTools), "zipalign.exe"); }

struct Missing {
    bool adb = false;
    bool java = false;
    bool build_tools = false;
    bool any() const { return adb || java || build_tools; }
    int count() const { return (adb ? 1 : 0) + (java ? 1 : 0) + (build_tools ? 1 : 0); }
};

/* Installs whatever the caller reports as missing. Returns true only when
 * every requested component ended up usable. */
inline bool install_missing(const Missing& m) {
    ui::section("Доустановка окружения");
    ui::box_line("Качаю в " + util::sdk_root().string());
    ui::box_line("Права администратора не нужны, PATH не трогаю.");
    std::cout << "\n";

    bool all = true;
    if (m.adb)         all &= install(kPlatformTools);
    if (m.java)        all &= install(kJava);
    if (m.build_tools) all &= install(kBuildTools);

    std::cout << "\n";
    if (all) ui::ok("Окружение готово.");
    else     ui::fail("Часть компонентов поставить не вышло — см. сообщения выше.");
    return all;
}

} // namespace boot
