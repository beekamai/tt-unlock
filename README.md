# TT-UNLOCK

<p align="center">
  <b>TikTok region unlock</b> — static Windows tool (adb + pure C++ DEX rewrite)<br/>
  Spoofs SIM identity → <b>DE Telekom</b> (<code>de</code> / <code>26201</code> / <code>Telekom</code>)
</p>

<p align="center">
  🇬🇧 <a href="#english">English</a> · 🇷🇺 <a href="#русский">Русский</a>
</p>

<p align="center">
  <img alt="platform" src="https://img.shields.io/badge/platform-Windows%20x64-blue">
  <img alt="lang" src="https://img.shields.io/badge/C%2B%2B-17-orange">
  <img alt="version" src="https://img.shields.io/badge/version-0.4.0-informational">
  <img alt="license" src="https://img.shields.io/badge/license-MIT-green">
</p>

---

## English

### What it does

On devices with a **RU SIM** (MCC 250), the TikTok Android client often reports `carrier_region=ru` from `TelephonyManager` even when traffic exits via a non-RU VPN. That signal is enough for a restricted / frozen feed.

**TT-UNLOCK** (stock TikTok only — no third-party mod APKs):

1. Pulls installed TikTok split-APKs over **adb**
2. Scans `classes*.dex` for stable **BPEA** anchors (`bpea-getSimCountryIso`, `TelephonyManager_getSimOperator`, …)
3. Early-returns a consistent DE SIM profile on **safe leaf** methods only:
   - country ISO → `de`
   - MCC+MNC → `26201` (numeric only — never spoof operator as `"de"`)
   - operator name → `Telekom` (when the string already exists in that dex)
4. Also stubs region hub **`X.155y`** static String getters → `DE` when present
5. Fixes DEX checksum / SHA-1 after patch
6. Re-signs every split with one local keystore
7. Uninstalls stock TikTok and `install-multiple` the patched set

**Not patched** (intentionally — caused ART crashes or search regressions in testing):

- Outer BPEA methods with `try/catch`
- `store_region` / account region / hard query-param overrides
- DEX `string_ids` relocation / random digit rewrite

### Requirements (any PC)

| Need | Notes |
|------|--------|
| Windows 10/11 **x64** | prebuilt static `.exe` needs no MinGW DLLs |
| USB debugging enabled | accept the RSA prompt on the phone |
| Internet on first run | only if the toolchain below is missing |

**The toolchain installs itself.** On a fresh machine with no Android SDK and no
Java, the tool offers to fetch what it needs (~110 MB total) and unpacks it into
`%LOCALAPPDATA%\tt-unlock\sdk`:

| Component | Source | Size |
|-----------|--------|------|
| platform-tools (`adb`) | `dl.google.com` | ~8 MB |
| build-tools (`apksigner`, `zipalign`) | `dl.google.com` | ~56 MB |
| Java 21 JRE (`java`, `keytool`) | Adoptium API | ~47 MB |

No admin rights, no installer, no `PATH` changes. An SDK or JDK already on the
machine is detected first and reused, so nothing is downloaded twice. To undo
everything, delete that one folder.

Optional for building from source: MSYS2 MinGW64 (`g++` / `gcc`).

### Quick start (release binary)

**On the phone, once:**

1. Install the **original** TikTok from Play. Open it, scroll the feed, watch
   2–3 videos to the end, open someone's profile, tap `+` (camera).
   This pulls the dynamic modules (`df_player`, `df_camera_biz`, …). They only
   come from Play, and only while the client is unmodified — after the resign
   Play will not deliver them, and profile videos stay broken for good.
2. Settings → About phone → tap **Build number** 7 times (Xiaomi: *HyperOS/MIUI version*).
3. Settings → Developer options → enable **both**:
   - **USB debugging**
   - **Install via USB** ← installs fail without it
   On Xiaomi this second toggle needs a Mi account, a SIM and internet, otherwise it flips back.
4. Plug in the cable, set USB mode to **File transfer**, accept *Allow USB debugging* (tick *Always*).
5. Turn on a **non-RU VPN** (DE preferred) before first launch of the patched app.

**On the PC:**

```text
tt-unlock.exe
```

Menu → **[1]** (*Пропатчить TikTok* — patch TikTok, fully automatic; the UI is
Russian). The tool checks the device, the install source, the module set and the
toolchain on its own, then pulls, patches, signs and installs.

**Watch the phone during install** — Android shows *Install app?* and waits.
Nothing pressed, nothing installed.

Afterwards: birthday 18+, guest or email login. Google login usually fails
(Play Integrity).

First run creates `tt-unlock.jks` next to the exe (local signing key). Signed
APKs are copied to `output/`.

### If install fails

The tool prints adb's own error and reinstalls the stock TikTok it pulled, so
the phone never ends up with no TikTok at all. What the codes mean:

| adb says | Fix |
|----------|-----|
| `INSTALL_FAILED_USER_RESTRICTED` | enable **Install via USB** in Developer options |
| `INSTALL_FAILED_ABORTED` | the *Install app?* dialog on the phone was not confirmed |
| `INSTALL_FAILED_VERIFICATION_FAILURE` | turn Play Protect off for the install |
| `INSTALL_FAILED_MISSING_SPLIT` | TikTok was incomplete — reinstall from Play, use it, patch again |
| `INSTALL_FAILED_UPDATE_INCOMPATIBLE` | another TikTok with a different signature is still on the device (work profile / second user) |
| `INSTALL_FAILED_INSUFFICIENT_STORAGE` | free up ~1 GB |

### Important

- Operator codes stay **numeric** (`26201`). Spoofing MCC as `"de"` breaks CDN.
- **Few splits / no `df_player`** does not break the install — the feed and the region unlock still work, because the patch lives in `base.apk`. What breaks is profile videos and the camera, and Play will not deliver those modules after the resign. The tool says so and lets you choose.
- Play updates restore stock code; re-run the tool after updates.
- Server-side `device_id` / account history can still limit some surfaces (e.g. search) even with a clean SIM spoof.
- Modified client → ban / policy risk. Educational tooling; you are responsible for use.
- This repo does **not** redistribute TikTok APKs.

### Build from source

```powershell
# MinGW g++ on PATH, or MSYS2 mingw64
.\build.ps1
# → build\tt-unlock.exe  and  release\tt-unlock.exe
```

```bash
make release
```

### Layout

```text
tt-unlock/
  release/tt-unlock.exe   # static prebuild
  src/
    main.cpp              # menu, pipeline, tool detection
    dex_patch.hpp         # the SIM spoof itself
    adb.hpp  apk.hpp      # device I/O, apk repack
    bootstrap.hpp         # auto-install of adb / build-tools / JRE
    netfetch.hpp          # WinHTTP download
    unzip.hpp             # miniz extraction (zip-slip safe)
    ui.hpp  util.hpp  process.hpp
  third_party/miniz/      # zip + deflate
  build.ps1  Makefile
  README.md  LICENSE
```

---

## Русский

### Зачем

С **российской SIM** (MCC 250) клиент TikTok часто шлёт `carrier_region=ru` из `TelephonyManager`, даже через VPN. Лента в приложении может оставаться «замороженной», хотя веб на том же IP уже нормальный.

### Что делает

1. `adb` → снимает split-APK  
2. Патчит **BPEA leaf**-обёртки TelephonyManager:  
   - ISO → `de`  
   - MCC+MNC → `26201` (только цифры)  
   - имя оператора → `Telekom` (если строка уже есть в dex)  
3. Патчит region hub **`X.155y`** → `DE`  
4. **Не** трогает try/catch outers, `store_region` и account-поля  
5. Чинит checksum DEX, подписывает все splits, ставит через `install-multiple`

### Нужно на любом ПК

- Windows 10/11 x64  
- USB-отладка  
- интернет при первом запуске — только если чего-то из тулчейна нет  

**Тулчейн ставится сам.** На чистой машине без Android SDK и Java тул предложит
скачать недостающее (~110 MB) и распакует в `%LOCALAPPDATA%\tt-unlock\sdk`:

| Компонент | Откуда | Размер |
|-----------|--------|--------|
| platform-tools (`adb`) | `dl.google.com` | ~8 MB |
| build-tools (`apksigner`, `zipalign`) | `dl.google.com` | ~56 MB |
| Java 21 JRE (`java`, `keytool`) | Adoptium API | ~47 MB |

Без прав администратора, без инсталлятора, `PATH` не трогается. Уже стоящие в
системе SDK/JDK находятся первыми и переиспользуются — второй раз ничего не
качается. Чтобы откатить всё — удалить эту папку.

### Запуск

**На телефоне, один раз:**

1. Поставь **оригинальный** TikTok из Play Маркета. Открой, полистай ленту,
   досмотри 2–3 видео до конца, зайди в чужой профиль, нажми `+` (камера).
   Так докачаются модули (`df_player`, `df_camera_biz`, …). Они приходят только
   из Play и только пока клиент оригинальный — после переподписи Play их уже не
   отдаст, и видео в профилях останутся нерабочими навсегда.
2. Настройки → О телефоне → 7 раз по **«Номер сборки»** (Xiaomi — по *«Версия HyperOS/MIUI»*).
3. Настройки → Для разработчиков → включи **оба** пункта:
   - **Отладка по USB**
   - **Установка через USB** ← без неё установка упадёт
   На Xiaomi второй тумблер требует Mi-аккаунт, SIM и интернет, иначе отскакивает.
4. Подключи кабель, режим USB — **«Передача файлов»**, подтверди *«Разрешить отладку»* (галочка *«Всегда»*).
5. Включи **VPN не из РФ** (лучше DE) — до первого запуска патченого клиента.

**На ПК:**

```text
tt-unlock.exe
```

Меню → **[1] Пропатчить TikTok — всё автоматически**. Тул сам проверит телефон,
откуда установлен TikTok, полный ли набор модулей и своё окружение, потом снимет
APK, пропатчит, подпишет и поставит.

**Смотри на телефон во время установки** — Android покажет окно *«Установить
приложение?»* и будет ждать. Не нажать = не установится.

Дальше: ДР 18+, гость или вход по почте (Google-вход обычно мёртв).

### Если установка упала

Тул печатает то, что реально сказал adb, и возвращает стоковый TikTok, который
сам же и снял — телефон не остаётся без приложения. Что значат коды:

| adb пишет | Что делать |
|-----------|------------|
| `INSTALL_FAILED_USER_RESTRICTED` | включить **«Установка через USB»** в меню «Для разработчиков» |
| `INSTALL_FAILED_ABORTED` | на телефоне не подтвердили окно *«Установить приложение?»* |
| `INSTALL_FAILED_VERIFICATION_FAILURE` | выключить Play Protect на время установки |
| `INSTALL_FAILED_MISSING_SPLIT` | TikTok был неполный — переставить из Play, попользоваться, патчить снова |
| `INSTALL_FAILED_UPDATE_INCOMPATIBLE` | на устройстве остался TikTok с другой подписью (рабочий профиль / вторая учётка) |
| `INSTALL_FAILED_INSUFFICIENT_STORAGE` | освободить ~1 GB |

### Важно

- MCC только **числовой** — `"de"` вместо `getSimOperator` ломает CDN.  
- После обновления TikTok из Play — патч слетает, гоняй тул снова.  
- Серверный `device_id` / история аккаунта могут резать отдельные экраны (поиск) даже при живой ленте.  
- Мод-клиент = риск бана. Educational tooling.  
- В репо **нет** APK TikTok.

### Сборка

```powershell
.\build.ps1
```

---

## License

MIT — see [LICENSE](LICENSE).
