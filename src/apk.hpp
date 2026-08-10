#pragma once
#include "util.hpp"
#include "dex_patch.hpp"

#include "../third_party/miniz/miniz.h"

#include <map>
#include <cstring>

namespace apk {

struct PatchApkResult {
    bool ok = false;
    int methods = 0;
    int classes = 0;
    int dex_patched = 0;
    std::vector<std::string> log;
    fs::path out_apk;
};

// Rebuild zip: copy every entry BYTE-IDENTICAL via add_from_zip_reader,
// only re-encode the dex files we actually patched. Avoids recompressing
// resources.arsc / native libs (that broke the previous build).
inline bool replace_dex_in_zip(const fs::path& zip_path,
                               const std::map<std::string, std::vector<uint8_t>>& replacements,
                               const fs::path& out_path) {
    mz_zip_archive in{};
    std::memset(&in, 0, sizeof(in));
    if (!mz_zip_reader_init_file(&in, zip_path.string().c_str(), 0)) return false;

    mz_zip_archive out{};
    std::memset(&out, 0, sizeof(out));
    if (!mz_zip_writer_init_file(&out, out_path.string().c_str(), 0)) {
        mz_zip_reader_end(&in);
        return false;
    }

    mz_uint n = mz_zip_reader_get_num_files(&in);
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&in, i, &st)) continue;
        if (st.m_is_directory) {
            mz_zip_writer_add_mem(&out, st.m_filename, nullptr, 0, MZ_NO_COMPRESSION);
            continue;
        }

        std::string name = st.m_filename;
        auto it = replacements.find(name);
        if (it != replacements.end()) {
            // Keep original compression choice: STORED if original was stored
            mz_uint level = (st.m_method == 0) ? MZ_NO_COMPRESSION : MZ_DEFAULT_LEVEL;
            if (!mz_zip_writer_add_mem(&out, name.c_str(),
                                      it->second.data(), it->second.size(), level)) {
                mz_zip_writer_end(&out);
                mz_zip_reader_end(&in);
                return false;
            }
        } else {
            // Bit-exact clone of the local file (no recompress)
            if (!mz_zip_writer_add_from_zip_reader(&out, &in, i)) {
                mz_zip_writer_end(&out);
                mz_zip_reader_end(&in);
                return false;
            }
        }
    }

    mz_bool ok = mz_zip_writer_finalize_archive(&out);
    mz_zip_writer_end(&out);
    mz_zip_reader_end(&in);
    return ok == MZ_TRUE;
}

inline PatchApkResult patch_base_apk(const fs::path& base_apk, const fs::path& out_apk) {
    PatchApkResult r;
    r.out_apk = out_apk;

    mz_zip_archive zip{};
    std::memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, base_apk.string().c_str(), 0)) {
        r.log.push_back("cannot open base.apk");
        return r;
    }

    std::map<std::string, std::vector<uint8_t>> repl;
    mz_uint n = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
        std::string name = st.m_filename;
        // only root classes*.dex
        if (name.find('/') != std::string::npos) continue;
        if (name.rfind("classes", 0) != 0 || name.find(".dex") == std::string::npos) continue;

        size_t sz = 0;
        void* p = mz_zip_reader_extract_to_heap(&zip, i, &sz, 0);
        if (!p) continue;
        std::vector<uint8_t> buf(static_cast<uint8_t*>(p), static_cast<uint8_t*>(p) + sz);
        mz_free(p);

        auto rep = dex::patch_dex(buf, name);
        for (auto& note : rep.notes) r.log.push_back(note);
        if (rep.methods_stubbed > 0) {
            repl[name] = std::move(buf);
            r.methods += rep.methods_stubbed;
            r.classes += rep.classes_hit;
            r.dex_patched++;
        }
    }
    mz_zip_reader_end(&zip);

    if (repl.empty()) {
        r.log.push_back("no dex methods patched — TikTok layout may have changed");
        return r;
    }

    if (!replace_dex_in_zip(base_apk, repl, out_apk)) {
        r.log.push_back("failed to rebuild apk zip (add_from_zip_reader)");
        return r;
    }
    r.ok = true;
    r.log.push_back("patched " + std::to_string(r.dex_patched) + " dex, " +
                    std::to_string(r.methods) + " methods, " +
                    std::to_string(r.classes) + " classes");
    return r;
}

} // namespace apk
