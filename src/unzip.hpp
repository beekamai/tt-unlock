#pragma once
/* Zip extraction on top of the miniz already vendored for APK rewriting.
 *
 * Archive entries are attacker-controlled data (we fetch them over the
 * network), so every path is normalised and checked to stay under the
 * destination — a "../../windows/system32" entry must never escape.
 */
#include "util.hpp"
#include "miniz.h"

#include <string>

namespace zipx {

/* Rejects absolute paths, drive letters and any ".." that climbs out. */
inline bool safe_join(const fs::path& root, const std::string& entry, fs::path& out) {
    if (entry.empty()) return false;
    if (entry.find(':') != std::string::npos) return false;
    if (entry.front() == '/' || entry.front() == '\\') return false;

    fs::path rel = fs::path(entry).lexically_normal();
    if (rel.is_absolute()) return false;
    for (const auto& part : rel) {
        if (part == "..") return false;
    }

    fs::path candidate = (root / rel).lexically_normal();
    auto base = root.lexically_normal().string();
    auto cand = candidate.string();
    if (cand.size() < base.size() || cand.compare(0, base.size(), base) != 0) return false;

    out = candidate;
    return true;
}

/* Extracts into dest, creating it if needed. Returns false on the first
 * unsafe or unreadable entry — a half-extracted SDK is better detected here
 * than by a confusing failure three steps later. */
inline bool extract(const fs::path& archive, const fs::path& dest, std::string& error) {
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, archive.string().c_str(), 0)) {
        error = "битый архив: " + archive.filename().string();
        return false;
    }

    std::error_code ec;
    fs::create_directories(dest, ec);

    mz_uint n = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st{};
        if (!mz_zip_reader_file_stat(&zip, i, &st)) {
            mz_zip_reader_end(&zip);
            error = "не читается запись " + std::to_string(i);
            return false;
        }

        fs::path target;
        if (!safe_join(dest, st.m_filename, target)) {
            mz_zip_reader_end(&zip);
            error = std::string("небезопасный путь в архиве: ") + st.m_filename;
            return false;
        }

        if (mz_zip_reader_is_file_a_directory(&zip, i)) {
            fs::create_directories(target, ec);
            continue;
        }

        fs::create_directories(target.parent_path(), ec);
        if (!mz_zip_reader_extract_to_file(&zip, i, target.string().c_str(), 0)) {
            mz_zip_reader_end(&zip);
            error = std::string("не распаковалось: ") + st.m_filename;
            return false;
        }
    }

    mz_zip_reader_end(&zip);
    return true;
}

/* Finds a file by name anywhere under root — release archives wrap their
 * payload in a version-stamped folder (android-14, jdk-21.0.12.1+1-jre) that
 * changes with every upstream bump, so we never hardcode it. */
inline fs::path find_under(const fs::path& root, const std::string& filename) {
    if (!util::dir_exists(root)) return {};
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->is_regular_file(ec) && it->path().filename() == filename)
            return it->path();
    }
    return {};
}

} // namespace zipx
