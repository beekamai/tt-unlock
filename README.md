# TT-UNLOCK

<p align="center">
  <b>TikTok region unlock</b> — static Windows tool (adb + pure C++ DEX rewrite)<br/>
  Spoofs <code>carrier_region</code> from SIM country → <b>DE</b>
</p>

<p align="center">
  🇬🇧 <a href="#english">English</a> · 🇷🇺 <a href="#русский">Русский</a>
</p>

<p align="center">
  <img alt="platform" src="https://img.shields.io/badge/platform-Windows%20x64-blue">
  <img alt="lang" src="https://img.shields.io/badge/C%2B%2B-17-orange">
  <img alt="license" src="https://img.shields.io/badge/license-MIT-green">
</p>

---

## English

### What it does

On devices with a **RU SIM** (MCC 250), the TikTok Android client can report `carrier_region=ru` from `TelephonyManager.getSimCountryIso()` even when traffic exits via a non-RU VPN. That signal is enough for a restricted / frozen feed.

**TT-UNLOCK**:

1. Pulls installed TikTok split-APKs over **adb**
2. Finds region helpers via stable BPEA anchors (`bpea-getSimCountryIso`, …)
3. Early-returns country ISO as **`de`** (does **not** fake MCC/MNC operator codes)
4. Fixes DEX checksum / SHA-1 after patch
5. Re-signs every split with one local keystore
6. Uninstalls stock TikTok and `install-multiple` the patched set

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
4. Run:

```text
tt-unlock.exe
```

5. Menu → **[1] Full auto-patch**.
6. After install: set birthday (18+), use guest or email login. Google login usually fails (Play Integrity).

First run creates `tt-unlock.jks` next to the exe (local signing key). Signed APKs are copied to `output/`.

### Important

- Patch **only country ISO** getters. Operator / `mcc_mnc` stay real so media/CDN keep working.
- If the tool reports **few splits / no `df_player`**, stop — install a complete TikTok first, then patch again.
- Play updates restore stock code; re-run the tool after updates.
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

С **российской SIM** клиент TikTok часто шлёт `carrier_region=ru` из SIM, даже через VPN. Лента в приложении может оставаться «замороженной», хотя веб на том же IP уже нормальный.

### Что делает

1. `adb` → снимает split-APK  
2. Патчит только **country ISO** (BPEA `getSimCountryIso` / network country → `de`)  
3. **Не** подменяет `getSimOperator` / MCC+MNC  
4. Чинит checksum DEX, подписывает все splits, ставит через `install-multiple`

### Нужно на любом ПК

- Windows 10/11 x64  
- `adb` (platform-tools)  
- build-tools: `apksigner`, `zipalign`  
- Java 17+  
- USB-отладка  

### Запуск

1. Скачай `tt-unlock.exe` из Releases (или `release/tt-unlock.exe`).  
2. Полный TikTok из Play → открой, скролл, зайди в профиль (докачка `df_player`).  
3. `tt-unlock.exe` → пункт **[1]**.  
4. После установки: ДР 18+, гость или почта (Google login обычно нет).

Если тул пишет «мало splits / нет player» — сначала докачай модули на stock, потом патчь снова.

### Сборка

```powershell
.\build.ps1
```

---

## License

MIT — see [LICENSE](LICENSE).  
TikTok / ByteDance trademarks and APKs remain their owners’.
