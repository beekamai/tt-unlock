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
  <img alt="version" src="https://img.shields.io/badge/version-0.2.0-informational">
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
| [platform-tools](https://developer.android.com/tools/releases/platform-tools) (`adb`) | on `PATH` or under `%LOCALAPPDATA%\Android\Sdk\platform-tools` |
| Android **build-tools** (`apksigner`, `zipalign`) | via Android SDK / `ANDROID_HOME` |
| **Java 17+** (`java`, `keytool`) | on `PATH` or `JAVA_HOME` |
| USB debugging enabled | accept the RSA prompt on the phone |

Optional for building from source: MSYS2 MinGW64 (`g++` / `gcc`).

### Quick start (release binary)

1. Download `tt-unlock.exe` from [Releases](../../releases) (or use `release/tt-unlock.exe` from this repo).
2. Connect the phone, enable USB debugging.
3. Install a **full** TikTok from Play, open it once (scroll + open a profile) so dynamic modules like `df_player` download.
4. Turn **VPN DE (or non-RU exit) ON before** first open of the patched app.
5. Run:

```text
tt-unlock.exe
```

6. Menu → **[1] Full auto-patch**.
7. After install: set birthday (18+), guest or email login. Google login usually fails (Play Integrity).

First run creates `tt-unlock.jks` next to the exe (local signing key). Signed APKs are copied to `output/`.

### Important

- Operator codes stay **numeric** (`26201`). Spoofing MCC as `"de"` breaks CDN.
- If the tool reports **few splits / no `df_player`**, stop — install a complete TikTok first, then patch again.
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
  src/                    # C++17 sources
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
- `adb` (platform-tools)  
- build-tools: `apksigner`, `zipalign`  
- Java 17+  
- USB-отладка  

### Запуск

1. Скачай `tt-unlock.exe` из Releases (или `release/tt-unlock.exe`).  
2. Полный TikTok из Play → открой, скролл, зайди в профиль (докачка `df_player`).  
3. **VPN DE включи до** первого запуска патченого клиента.  
4. `tt-unlock.exe` → **[1] Full auto-patch**.  
5. ДР 18+, гость или почта (Google login обычно мёртв).

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
