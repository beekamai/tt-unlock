#include "ui.hpp"
#include "adb.hpp"
#include "apk.hpp"
#include "bootstrap.hpp"
#include "process.hpp"
#include "util.hpp"

#include <iostream>
#include <cstdlib>

static const char* kPkg = "com.zhiliaoapp.musically";

struct Tools {
    fs::path adb;
    fs::path java;
    fs::path keytool;
    fs::path apksigner;
    fs::path zipalign;
    fs::path work;
    fs::path keystore;
};

/* System install first, our own %LOCALAPPDATA% copy as fallback — a real
 * Android SDK on the machine keeps winning, so nothing is downloaded twice. */
static void detect_tools(Tools& t) {
    t.adb = adb::find_adb();
    if (t.adb.empty()) t.adb = boot::find_adb();

    t.java = adb::find_java();
    if (t.java.empty()) t.java = boot::find_java();

    t.keytool = adb::find_keytool(t.java);
    if (t.keytool.empty()) t.keytool = boot::find_keytool();

    t.apksigner = adb::find_apksigner();
    if (t.apksigner.empty()) t.apksigner = boot::find_apksigner();
    /* apksigner.bat resolves `java` through PATH/JAVA_HOME. With a portable
     * JRE that is empty, so prefer the jar and drive it with our own java. */
    if (t.apksigner.extension() == ".bat") {
        auto jar = t.apksigner.parent_path() / "lib" / "apksigner.jar";
        if (util::file_exists(jar)) t.apksigner = jar;
    }

    t.zipalign = adb::find_zipalign();
    if (t.zipalign.empty()) t.zipalign = boot::find_zipalign();
}

static boot::Missing missing_of(const Tools& t) {
    boot::Missing m;
    m.adb = t.adb.empty();
    /* keytool ships with java; if only one of them showed up the install is
     * broken enough that our own JRE is the simpler answer. */
    m.java = t.java.empty() || t.keytool.empty();
    m.build_tools = t.apksigner.empty() || t.zipalign.empty();
    return m;
}

static void list_missing(const boot::Missing& m) {
    if (m.adb)         ui::warn("нет adb (Android platform-tools)");
    if (m.java)        ui::warn("нет Java / keytool");
    if (m.build_tools) ui::warn("нет apksigner / zipalign (Android build-tools)");
}

/* Offers the download instead of doing it silently: it is ~110 MB and the
 * user may be on mobile data. */
static bool offer_bootstrap(Tools& t) {
    auto m = missing_of(t);
    if (!m.any()) return true;

    ui::section("Не хватает инструментов");
    list_missing(m);
    std::cout << "\n";
    ui::dim("Могу скачать их сам с серверов Google и Adoptium (~110 MB суммарно),");
    ui::dim("положить в " + util::sdk_root().string() + " и больше об этом не вспоминать.");

    int choice = ui::menu("Что делаем", {
        "Скачать и поставить недостающее",
        "Не сейчас (поставлю сам)"
    });
    if (choice != 1) return false;

    bool ok = boot::install_missing(m);
    detect_tools(t);
    if (ok && !missing_of(t).any()) {
        ui::ok("Всё на месте, можно патчить.");
        return true;
    }
    list_missing(missing_of(t));
    return false;
}

static bool ensure_keystore(Tools& t) {
    t.keystore = util::exe_dir() / "tt-unlock.jks";
    if (util::file_exists(t.keystore)) return true;
    if (t.keytool.empty()) return false;
    ui::info("Генерирую ключ подписи tt-unlock.jks …");
    auto r = proc::run({
        t.keytool.string(),
        "-genkeypair", "-v",
        "-keystore", t.keystore.string(),
        "-storepass", "ttunlock",
        "-keypass", "ttunlock",
        "-alias", "ttunlock",
        "-keyalg", "RSA",
        "-keysize", "2048",
        "-validity", "10000",
        "-dname", "CN=TTUnlock,O=Local,C=DE"
    });
    return r.ok() && util::file_exists(t.keystore);
}

static bool sign_apk(Tools& t, const fs::path& in, const fs::path& out) {
    fs::path aligned = in;
    fs::path tmp_aligned = t.work / (in.stem().string() + "_aligned.apk");
    if (!t.zipalign.empty()) {
        auto zr = proc::run({
            t.zipalign.string(), "-f", "-p", "4",
            in.string(), tmp_aligned.string()
        });
        if (zr.ok() && util::file_exists(tmp_aligned))
            aligned = tmp_aligned;
    }

    if (!t.apksigner.empty()) {
        std::vector<std::string> args;
        if (t.apksigner.extension() == ".jar") {
            args = {
                t.java.string(), "-jar", t.apksigner.string(), "sign",
                "--ks", t.keystore.string(),
                "--ks-pass", "pass:ttunlock",
                "--key-pass", "pass:ttunlock",
                "--ks-key-alias", "ttunlock",
                "--out", out.string(),
                aligned.string()
            };
        } else {
            // apksigner.bat
            args = {
                t.apksigner.string(), "sign",
                "--ks", t.keystore.string(),
                "--ks-pass", "pass:ttunlock",
                "--key-pass", "pass:ttunlock",
                "--ks-key-alias", "ttunlock",
                "--out", out.string(),
                aligned.string()
            };
        }
        auto r = proc::run(args, {}, 300000);
        return r.ok() && util::file_exists(out);
    }

    // jarsigner fallback (v1 only — may fail on modern Android)
    fs::copy_file(aligned, out, fs::copy_options::overwrite_existing);
    auto r = proc::run({
        t.keytool.string().find("keytool") != std::string::npos
            ? (t.java.parent_path() / "jarsigner.exe").string()
            : "jarsigner",
        "-keystore", t.keystore.string(),
        "-storepass", "ttunlock",
        "-keypass", "ttunlock",
        out.string(),
        "ttunlock"
    });
    return util::file_exists(out);
}

static int run_full_patch(Tools& t) {
    using namespace ui;

    section("1/6  Устройство");
    Spinner sp("Жду adb devices…");
    auto devs = adb::devices(t.adb);
    adb::Device* pick = nullptr;
    adb::Device local;
    for (auto& d : devs) {
        if (d.state == "device") { local = d; pick = &local; break; }
    }
    if (!pick) {
        sp.stop(false, "Телефон не готов");
        bool unauthorized = false, offline = false;
        for (auto& d : devs) {
            if (d.state.find("unauthorized") != std::string::npos) unauthorized = true;
            if (d.state.find("offline") != std::string::npos) offline = true;
            dim(d.serial + "  [" + d.state + "]");
        }
        if (unauthorized)
            fail("На телефоне висит окно «Разрешить отладку по USB?» — нажми «Разрешить»\n"
                 "     и поставь галочку «Всегда разрешать с этого компьютера».");
        else if (offline)
            fail("Телефон в состоянии offline: перевоткни кабель или переключи режим USB\n"
                 "     на «Передача файлов».");
        else
            fail("Телефон не виден. Включи «Отладка по USB» в меню «Для разработчиков»\n"
                 "     и подключи кабелем (не только для зарядки).");
        return 1;
    }
    sp.stop(true, "Устройство: " + pick->serial);

    section("2/6  Проверяю TikTok на телефоне");
    Spinner sp2("pm path " + std::string(kPkg));
    auto paths = adb::pm_paths(t.adb, kPkg);
    if (paths.empty()) {
        sp2.stop(false, "TikTok не установлен");
        fail("Поставь оригинальный TikTok из Play Маркета, открой его,");
        fail("полистай ленту и зайди в чей-нибудь профиль — потом запусти патч снова.");
        return 1;
    }
    sp2.stop(true, "Найдено splits: " + std::to_string(paths.size()));

    auto installer = adb::installer_of(t.adb, kPkg);
    if (installer == "com.android.vending") ok("Установлен из Play Маркета");
    else if (!installer.empty() && installer != "null")
        warn("Установлен не из Play (" + installer + ") — модули могут не докачаться.");

    /* A full TikTok ships 30-50 splits. A thin one installs and unlocks the
     * feed just fine — the patch lives in base.apk, which is always there.
     * What it costs is features: profile videos and the camera need df_player
     * and df_camera_biz, and after the resign Play will not deliver them.
     * So this is a choice to present, not a reason to refuse. */
    bool has_player = false;
    for (auto& p : paths) {
        if (p.find("df_player") != std::string::npos) has_player = true;
    }
    if (paths.size() < 25 || !has_player) {
        std::cout << "\n";
        warn("TikTok установлен не полностью: " + std::to_string(paths.size()) +
             " модулей" + (has_player ? "" : ", нет df_player") + ".");
        std::cout << "\n";
        info("Установку это не ломает: лента и регион-анлок заработают.");
        info("Не будут работать: видео в чужих профилях, камера и съёмка.");
        info("Докачать потом не выйдет — после патча Play модули не отдаёт.");
        std::cout << "\n";
        box_line("Докачать сейчас (5 минут):", c::white);
        box_line("  открыть TikTok → полистать ленту → досмотреть 2-3 видео", c::gray);
        box_line("  → зайти в чужой профиль → нажать «+» (камера) → Wi-Fi минуту", c::gray);
        std::cout << "\n";

        int c = menu("Что делаем", {
            "Продолжить — нужна только лента",
            "Выйти, докачаю модули и вернусь"
        });
        if (c == 2) {
            info("Ок, ничего не трогала. Телефон как был.");
            return 0;
        }
    }

    section("3/6  Скачиваю APK");
    fs::path pull_dir = t.work / "pulled";
    fs::create_directories(pull_dir);
    std::vector<fs::path> local_apks;
    int i = 0;
    for (auto& remote : paths) {
        ++i;
        std::string name = fs::path(remote).filename().string();
        // base.apk naming from pm path is base.apk / split_*.apk
        fs::path dest = pull_dir / name;
        Spinner spi("pull " + name + "  (" + std::to_string(i) + "/" +
                    std::to_string(paths.size()) + ")");
        if (!adb::pull(t.adb, remote, dest)) {
            spi.stop(false, "fail " + name);
            return 1;
        }
        spi.stop(true, name + "  " + util::human_size(util::file_size(dest)));
        local_apks.push_back(dest);
    }

    fs::path base;
    for (auto& p : local_apks) {
        if (p.filename() == "base.apk") base = p;
    }
    if (base.empty()) {
        fail("base.apk не найден среди splits");
        return 1;
    }

    section("4/6  Патчу SIM spoof (DEX)");
    fs::path patched_base = t.work / "base_patched.apk";
    {
        Spinner sp3("Сканирую classes*.dex (BPEA + region hub → DE Telekom)…");
        auto pr = apk::patch_base_apk(base, patched_base);
        sp3.stop(pr.ok, pr.ok
            ? ("Готово: " + std::to_string(pr.methods) + " methods / " +
               std::to_string(pr.classes) + " classes / " +
               std::to_string(pr.dex_patched) + " dex")
            : "Патч не применился");
        for (auto& L : pr.log) dim(L);
        if (!pr.ok) {
            fail("Не нашли SIM/region-методы. Версия TikTok слишком новая/старая — нужен update эвристик.");
            return 1;
        }
        // Sanity: patched apk must not be wildly smaller than original
        auto osz = util::file_size(base);
        auto psz = util::file_size(patched_base);
        if (osz > 0 && psz < osz / 2) {
            fail("patched base.apk too small — zip rewrite looks broken");
            return 1;
        }
        info("base " + util::human_size(osz) + " → patched " + util::human_size(psz));
    }

    section("5/6  Подпись splits");
    if (!ensure_keystore(t)) {
        fail("Нет keytool / не удалось создать keystore");
        return 1;
    }
    ok("keystore: " + t.keystore.string());

    fs::path signed_dir = t.work / "signed";
    fs::create_directories(signed_dir);
    std::vector<fs::path> signed_apks;

    for (size_t idx = 0; idx < local_apks.size(); ++idx) {
        auto& src = local_apks[idx];
        fs::path to_sign = src;
        if (src.filename() == "base.apk") to_sign = patched_base;

        fs::path out = signed_dir / ("signed_" + src.filename().string());
        Spinner sps("sign " + src.filename().string() + "  (" +
                    std::to_string(idx + 1) + "/" + std::to_string(local_apks.size()) + ")");
        if (!sign_apk(t, to_sign, out)) {
            sps.stop(false, "sign fail " + src.filename().string());
            return 1;
        }
        sps.stop(true, out.filename().string());
        signed_apks.push_back(out);
    }

    section("6/6  Установка");
    warn("Сейчас удалю stock TikTok — данные и вход в аккаунт сбросятся.");
    info("СМОТРИ НА ТЕЛЕФОН: появится окно «Установить приложение?» — нажми «Установить».");
    info("Если ничего не нажать, Android отменит установку сам.");
    std::cout << "\n";
    {
        Spinner spu("uninstall " + std::string(kPkg));
        adb::uninstall(t.adb, kPkg);
        spu.stop(true, "stock удалён");
    }
    {
        Spinner spi("install-multiple (" + std::to_string(signed_apks.size()) + " apk)…");
        auto r = adb::install_multiple(t.adb, signed_apks);
        if (!adb::install_ok(r)) {
            spi.stop(false, "установка не прошла");

            /* Show what adb actually said — guessing at the reason is how the
             * old build sent people to the wrong settings screen. */
            auto raw = util::trim(r.out + "\n" + r.err);
            if (!raw.empty()) {
                for (auto& L : util::split_lines(raw)) {
                    auto s = util::trim(L);
                    if (!s.empty()) dim(s);
                }
            }
            std::cout << "\n";
            auto hint = adb::install_hint(r);
            if (!hint.empty()) for (auto& L : util::split_lines(hint)) fail(L);
            else fail("Причина выше. Чаще всего — «Установка через USB» в меню «Для разработчиков».");

            /* The stock splits are still on disk and still carry TikTok's own
             * signature, so we can undo the uninstall instead of leaving the
             * phone with no TikTok at all. */
            std::cout << "\n";
            Spinner spr("Возвращаю стоковый TikTok…");
            auto back = adb::install_multiple(t.adb, local_apks);
            if (adb::install_ok(back)) {
                spr.stop(true, "Стоковый TikTok вернулся на место");
                info("Телефон в исходном состоянии. Почини настройку выше и запусти патч снова.");
            } else {
                spr.stop(false, "Откат не удался");
                warn("TikTok сейчас не установлен. Поставь его из Play Маркета.");
            }
            return 1;
        }
        spi.stop(true, "Установлено");
    }

    // copy durable artifacts next to exe
    fs::path out_keep = util::exe_dir() / "output";
    fs::create_directories(out_keep);
    for (auto& p : signed_apks) {
        std::error_code ec;
        fs::copy_file(p, out_keep / p.filename(), fs::copy_options::overwrite_existing, ec);
    }
    info("Копия signed APK → " + out_keep.string());

    success_finale();
    return 0;
}

static int doctor(Tools& t) {
    using namespace ui;
    section("Диагностика окружения");
    if (t.adb.empty()) fail("adb не найден");
    else ok("adb: " + t.adb.string());

    if (t.java.empty()) fail("java не найден (нужна 17+)");
    else {
        ok("java: " + t.java.string());
        auto v = proc::run({t.java.string(), "-version"}, {}, 10000);
        auto ver = util::trim(v.err.empty() ? v.out : v.err);
        if (ver.size() > 120) ver.resize(120);
        if (!ver.empty()) dim(ver);
        if (v.timed_out()) warn("java -version timeout");
    }

    if (t.keytool.empty()) warn("keytool не найден");
    else ok("keytool: " + t.keytool.string());

    if (t.apksigner.empty()) warn("apksigner не найден (поставлю через jarsigner v1 — хуже)");
    else ok("apksigner: " + t.apksigner.string());

    if (t.zipalign.empty()) warn("zipalign не найден (не критично)");
    else ok("zipalign: " + t.zipalign.string());

    if (!t.adb.empty()) {
        info("опрашиваю adb devices (до 15с)…");
        std::cout << std::flush;
        auto devs = adb::devices(t.adb);
        if (devs.empty()) warn("нет устройств adb");
        for (auto& d : devs) {
            if (d.state == "device") ok("device " + d.serial);
            else warn(d.serial + " [" + d.state + "]");
        }
        if (!devs.empty()) {
            info("смотрю pm path TikTok (до 30с)…");
            std::cout << std::flush;
            auto paths = adb::pm_paths(t.adb, kPkg);
            if (paths.empty()) warn("TikTok не установлен (или adb shell timeout)");
            else ok("TikTok splits: " + std::to_string(paths.size()));
        }
    }

    std::cout << "\n";
    ok("Диагностика завершена");
    return 0;
}

int main() {
    ui::enable_vt();
    ui::banner();

    Tools t;
    t.work = util::temp_work_dir() / std::to_string(util::now_ms());
    fs::create_directories(t.work);

    {
        ui::Spinner sp("Ищу adb / java / build-tools…");
        detect_tools(t);
        sp.stop(true, "Окружение просканировано");
    }

    if (missing_of(t).any()) {
        offer_bootstrap(t);
        ui::pause();
        ui::banner();
    }

    for (;;) {
        int choice = ui::menu("Меню", {
            "Пропатчить TikTok — всё автоматически",
            "Доустановить окружение (adb / Java / build-tools)",
            "Инструкция: что сделать на телефоне",
            "Диагностика (doctor)",
            "Только справка / about",
            "Выход"
        });

        if (choice == 6) {
            ui::show_cursor();
            std::cout << "\n  " << ui::c::gray << "bye." << ui::c::reset << "\n";
            break;
        }
        if (choice == 2) {
            auto m = missing_of(t);
            if (!m.any()) {
                ui::ok("Всё уже на месте — ставить нечего.");
                ui::dim("adb: " + t.adb.string());
                ui::dim("java: " + t.java.string());
                ui::dim("apksigner: " + t.apksigner.string());
            } else {
                offer_bootstrap(t);
            }
            ui::pause();
            ui::banner();
            continue;
        }
        if (choice == 3) {
            ui::checklist_permissions();
            ui::show_steps_overview();
            ui::pause();
            ui::banner();
            continue;
        }
        if (choice == 4) {
            doctor(t);
            ui::pause();
            ui::banner();
            continue;
        }
        if (choice == 5) {
            ui::section("About");
            ui::box_line("TT-UNLOCK — client SIM spoof for stock TikTok");
            ui::box_line("Profile: DE Telekom  (de / 26201 / Telekom)");
            ui::box_line("BPEA leaves (static, no try/catch) + hub 155y");
            ui::box_line("Does not hardcode store_region / account region");
            ui::box_line("Google login usually dies (Play Integrity / resign)");
            ui::box_line("Educational tooling. Ban risk is on you.");
            ui::box_line("Auto-installs adb / build-tools / JRE when missing");
            ui::box_line("Static MinGW · C++17 · miniz · WinHTTP  ·  v0.4.0");
            ui::pause();
            ui::banner();
            continue;
        }
        if (choice == 1) {
            if (missing_of(t).any() && !offer_bootstrap(t)) {
                ui::fail("Без adb / java / build-tools патчить нечем.");
                ui::pause();
                ui::banner();
                continue;
            }
            ui::checklist_permissions();
            ui::pause("Enter = поехали патчить…");
            int rc = run_full_patch(t);
            if (rc != 0) ui::fail("Пайплайн завершился с ошибкой.");
            ui::pause();
            ui::banner();
            continue;
        }
        ui::warn("Неверный выбор");
        ui::sleep_ms(400);
        ui::banner();
    }

    ui::show_cursor();
    return 0;
}
