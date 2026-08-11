#pragma once
/* Surgical DEX SIM spoof — ReVanced/Morphe-style, stock TikTok only.
 *
 * Default profile (DE Telekom):
 *   country ISO   → "de"  (TelephonyManager style)
 *   MCC+MNC       → "26201"  (numeric only — never "de")
 *   operator name → "Telekom"
 *   region hub    → "DE" when present (155y uppercases carrier ISO)
 *
 * What we patch (safe on 46.4.x ART):
 *  1. BPEA leaf wrappers tagged TelephonyManager_* / bpea-get* that are
 *     public static, return String, and have tries_size == 0.
 *  2. Region hub LX/155y (or C4936155y): static String getters (LIZ/LJ/…).
 *
 * What we deliberately skip (crash / CDN / search regressions):
 *  - Outer BPEA methods with try/catch (embed the same tag for findCert)
 *  - string_ids relocation / random digit string rewrite
 *  - Hardcoding store_region / app_region / account region query params
 *
 * MCC/name stubs only apply when the target string already exists in that dex.
 */
#include "util.hpp"
#include <cstring>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace dex {

struct PatchReport {
    int dex_files_scanned = 0;
    int methods_stubbed = 0;
    int classes_hit = 0;
    std::vector<std::string> notes;
    bool ok() const { return methods_stubbed > 0; }
};

inline uint32_t ru32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline uint16_t ru16(const uint8_t* p) {
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}
inline void wu16(uint8_t* p, uint16_t v) {
    p[0] = uint8_t(v & 0xff);
    p[1] = uint8_t((v >> 8) & 0xff);
}
inline void wu32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v & 0xff);
    p[1] = uint8_t((v >> 8) & 0xff);
    p[2] = uint8_t((v >> 16) & 0xff);
    p[3] = uint8_t((v >> 24) & 0xff);
}

inline uint32_t read_uleb128(const uint8_t* data, size_t size, size_t& off) {
    uint32_t result = 0;
    int shift = 0;
    while (off < size) {
        uint8_t b = data[off++];
        result |= uint32_t(b & 0x7f) << shift;
        if ((b & 0x80) == 0) break;
        shift += 7;
        if (shift > 35) break;
    }
    return result;
}

inline void write_uleb128(std::vector<uint8_t>& out, uint32_t v) {
    while (v > 0x7fu) {
        out.push_back(uint8_t((v & 0x7fu) | 0x80u));
        v >>= 7;
    }
    out.push_back(uint8_t(v));
}

inline std::string read_mutf8(const uint8_t* data, size_t size, size_t off) {
    if (off >= size) return {};
    size_t o = off;
    (void)read_uleb128(data, size, o);
    std::string s;
    while (o < size && data[o] != 0) {
        s.push_back(static_cast<char>(data[o++]));
        if (s.size() > 0x10000) break;
    }
    return s;
}

struct DexView {
    std::vector<uint8_t> data;
    uint32_t string_ids_size = 0, string_ids_off = 0;
    uint32_t type_ids_size = 0, type_ids_off = 0;
    uint32_t proto_ids_size = 0, proto_ids_off = 0;
    uint32_t method_ids_size = 0, method_ids_off = 0;
    uint32_t class_defs_size = 0, class_defs_off = 0;

    bool parse() {
        if (data.size() < 0x70) return false;
        if (std::memcmp(data.data(), "dex\n", 4) != 0) return false;
        string_ids_size = ru32(data.data() + 56);
        string_ids_off  = ru32(data.data() + 60);
        type_ids_size   = ru32(data.data() + 64);
        type_ids_off    = ru32(data.data() + 68);
        proto_ids_size  = ru32(data.data() + 72);
        proto_ids_off   = ru32(data.data() + 76);
        method_ids_size = ru32(data.data() + 88);
        method_ids_off  = ru32(data.data() + 92);
        class_defs_size = ru32(data.data() + 96);
        class_defs_off  = ru32(data.data() + 100);
        return true;
    }

    std::string string_at(uint32_t idx) const {
        if (idx >= string_ids_size) return {};
        size_t off = string_ids_off + idx * 4u;
        if (off + 4 > data.size()) return {};
        return read_mutf8(data.data(), data.size(), ru32(data.data() + off));
    }

    std::optional<uint32_t> find_string(const std::string& s) const {
        for (uint32_t i = 0; i < string_ids_size; ++i)
            if (string_at(i) == s) return i;
        return std::nullopt;
    }

    std::string type_name(uint32_t type_idx) const {
        if (type_idx >= type_ids_size) return {};
        size_t off = type_ids_off + type_idx * 4u;
        if (off + 4 > data.size()) return {};
        return string_at(ru32(data.data() + off));
    }

    bool proto_returns_string(uint32_t proto_idx) const {
        if (proto_idx >= proto_ids_size) return false;
        size_t off = proto_ids_off + proto_idx * 12u;
        if (off + 12 > data.size()) return false;
        return type_name(ru32(data.data() + off + 4)) == "Ljava/lang/String;";
    }

    struct MethodId {
        uint16_t class_idx;
        uint16_t proto_idx;
        uint32_t name_idx;
    };

    MethodId method_id(uint32_t idx) const {
        MethodId m{};
        if (idx >= method_ids_size) return m;
        size_t off = method_ids_off + idx * 8u;
        if (off + 8 > data.size()) return m;
        m.class_idx = ru16(data.data() + off);
        m.proto_idx = ru16(data.data() + off + 2);
        m.name_idx  = ru32(data.data() + off + 4);
        return m;
    }
};

// Find existing string, or repurpose a same-length all-digit string by
// overwriting its MUTF-8 payload in place (no string_ids relocation).
// Relocating string_ids to EOF made ART throw NoClassDefFoundError on 46.4.3.
inline bool ensure_string(DexView& dex, const std::string& s, uint32_t& out_idx) {
    if (auto id = dex.find_string(s)) {
        out_idx = *id;
        return true;
    }
    if (s.empty() || s.size() > 64) return false;

    // Prefer repurposing pure-digit strings of the same length (MCC-like).
    for (uint32_t i = 0; i < dex.string_ids_size; ++i) {
        auto cur = dex.string_at(i);
        if (cur.size() != s.size()) continue;
        bool digits = true;
        for (char c : cur) {
            if (c < '0' || c > '9') { digits = false; break; }
        }
        if (!digits) continue;

        size_t off = ru32(dex.data.data() + dex.string_ids_off + i * 4u);
        if (off >= dex.data.size()) continue;
        size_t o = off;
        (void)read_uleb128(dex.data.data(), dex.data.size(), o);
        if (o + s.size() >= dex.data.size()) continue;
        // same uleb length encoding for equal sizes < 128
        for (size_t k = 0; k < s.size(); ++k)
            dex.data[o + k] = static_cast<uint8_t>(s[k]);
        out_idx = i;
        return true;
    }
    return false;
}

// Early-return String: const-string v0, X / return-object v0
// Leave rest of bytecode / tries / debug intact (ART verifier).
inline bool stub_return_string(std::vector<uint8_t>& data, uint32_t code_off, uint32_t str_idx) {
    if (code_off == 0 || code_off + 16 > data.size()) return false;
    uint8_t* base = data.data() + code_off;
    uint16_t registers_size = ru16(base + 0);
    uint16_t ins_size = ru16(base + 2);
    uint32_t insns_size = ru32(base + 12);
    const bool jumbo = str_idx > 0xFFFFu;
    const uint32_t need = jumbo ? 4u : 3u;
    if (insns_size < need) return false;
    if (code_off + 16 + insns_size * 2u > data.size()) return false;

    if (registers_size < 1)
        wu16(base + 0, static_cast<uint16_t>(ins_size + 1));

    const uint8_t v = 0;
    uint8_t* insns = base + 16;
    if (jumbo) {
        wu16(insns + 0, static_cast<uint16_t>(0x001b | (uint16_t(v) << 8)));
        wu16(insns + 2, static_cast<uint16_t>(str_idx & 0xffff));
        wu16(insns + 4, static_cast<uint16_t>((str_idx >> 16) & 0xffff));
        wu16(insns + 6, static_cast<uint16_t>(0x0011 | (uint16_t(v) << 8)));
    } else {
        wu16(insns + 0, static_cast<uint16_t>(0x001a | (uint16_t(v) << 8)));
        wu16(insns + 2, static_cast<uint16_t>(str_idx & 0xffff));
        wu16(insns + 4, static_cast<uint16_t>(0x0011 | (uint16_t(v) << 8)));
    }
    return true;
}

inline bool code_has_const_string(const std::vector<uint8_t>& data, uint32_t code_off,
                                  const std::set<uint32_t>& string_idxs) {
    if (code_off == 0 || code_off + 16 > data.size() || string_idxs.empty()) return false;
    const uint8_t* base = data.data() + code_off;
    uint32_t insns_size = ru32(base + 12);
    if (code_off + 16 + insns_size * 2u > data.size()) return false;
    const uint16_t* insns = reinterpret_cast<const uint16_t*>(base + 16);
    for (uint32_t i = 0; i + 1 < insns_size; ++i) {
        uint8_t op = insns[i] & 0xff;
        if (op == 0x1a) {
            if (string_idxs.count(insns[i + 1])) return true;
        } else if (op == 0x1b && i + 2 < insns_size) {
            uint32_t idx = uint32_t(insns[i + 1]) | (uint32_t(insns[i + 2]) << 16);
            if (string_idxs.count(idx)) return true;
        }
    }
    return false;
}

inline void fix_dex_checksum(std::vector<uint8_t>& data) {
    if (data.size() <= 32) return;
    // SHA-1 of data[32..] → signature [12..32)
    {
        uint32_t h0=0x67452301,h1=0xEFCDAB89,h2=0x98BADCFE,h3=0x10325476,h4=0xC3D2E1F0;
        auto rol=[](uint32_t x,int n){return (x<<n)|(x>>(32-n));};
        const uint8_t* p = data.data() + 32;
        size_t len = data.size() - 32;
        size_t padlen = ((len + 8) / 64 + 1) * 64;
        std::vector<uint8_t> msg(padlen, 0);
        std::memcpy(msg.data(), p, len);
        msg[len] = 0x80;
        uint64_t bl = static_cast<uint64_t>(len) * 8ull;
        for (int i = 0; i < 8; ++i)
            msg[padlen - 8 + i] = static_cast<uint8_t>((bl >> (56 - 8 * i)) & 0xff);
        for (size_t chunk = 0; chunk < padlen; chunk += 64) {
            uint32_t w[80];
            for (int i = 0; i < 16; ++i)
                w[i] = (uint32_t(msg[chunk+4*i])<<24)|(uint32_t(msg[chunk+4*i+1])<<16)|
                       (uint32_t(msg[chunk+4*i+2])<<8)|uint32_t(msg[chunk+4*i+3]);
            for (int i = 16; i < 80; ++i)
                w[i] = rol(w[i-3]^w[i-8]^w[i-14]^w[i-16], 1);
            uint32_t A=h0,B=h1,C=h2,D=h3,E=h4;
            for (int i = 0; i < 80; ++i) {
                uint32_t f,k;
                if (i < 20) { f=(B&C)|((~B)&D); k=0x5A827999; }
                else if (i < 40) { f=B^C^D; k=0x6ED9EBA1; }
                else if (i < 60) { f=(B&C)|(B&D)|(C&D); k=0x8F1BBCDC; }
                else { f=B^C^D; k=0xCA62C1D6; }
                uint32_t t = rol(A,5)+f+E+k+w[i];
                E=D; D=C; C=rol(B,30); B=A; A=t;
            }
            h0+=A; h1+=B; h2+=C; h3+=D; h4+=E;
        }
        auto put_be=[&](uint32_t v, int o){
            data[12+o]=(v>>24)&0xff; data[13+o]=(v>>16)&0xff;
            data[14+o]=(v>>8)&0xff; data[15+o]=v&0xff;
        };
        put_be(h0,0); put_be(h1,4); put_be(h2,8); put_be(h3,12); put_be(h4,16);
    }
    // Adler-32 of [12..end) → checksum @8
    uint32_t a = 1, b = 0;
    const uint8_t* p = data.data() + 12;
    size_t n = data.size() - 12;
    for (size_t i = 0; i < n; ++i) {
        a = (a + p[i]) % 65521;
        b = (b + a) % 65521;
    }
    wu32(data.data() + 8, (b << 16) | a);
}

// Default spoof profile: DE Telekom (same as TikTokSimSpoof / Morphe presets).
struct SimSpoofProfile {
    const char* country_iso = "de";   // TelephonyManager style lowercase
    const char* mcc_mnc     = "26201";
    const char* op_name     = "Telekom";
};

inline PatchReport patch_dex(std::vector<uint8_t>& buf, const std::string& label) {
    PatchReport rep;
    rep.dex_files_scanned = 1;
    DexView dex;
    dex.data.swap(buf);
    if (!dex.parse()) {
        rep.notes.push_back(label + ": not a dex");
        buf.swap(dex.data);
        return rep;
    }

    SimSpoofProfile prof;

    // Resolve spoof strings. Prefer exact profile; fall back to common ISO tokens.
    uint32_t iso_idx = 0, iso_upper_idx = 0, mcc_idx = 0, name_idx = 0;
    bool have_iso = false, have_iso_upper = false, have_mcc = false, have_name = false;

    if (auto id = dex.find_string(prof.country_iso)) {
        iso_idx = *id; have_iso = true;
    } else if (auto id = dex.find_string("DE")) {
        iso_idx = *id; have_iso = true;
    } else if (auto id = dex.find_string("US")) {
        iso_idx = *id; have_iso = true;
    }

    if (auto id = dex.find_string("DE")) {
        iso_upper_idx = *id; have_iso_upper = true;
    } else if (have_iso) {
        iso_upper_idx = iso_idx; have_iso_upper = true;
    }

    if (!have_iso) {
        rep.notes.push_back(label + ": no country ISO string");
        buf.swap(dex.data);
        return rep;
    }

    // Tag → which spoof value to return
    enum class SpoofKind { Iso, Mcc, Name };
    std::vector<std::pair<const char*, SpoofKind>> tag_map = {
        // country ISO
        {"bpea-getSimCountryIso", SpoofKind::Iso},
        {"bpea-getNetworkCountryIso", SpoofKind::Iso},
        {"TelephonyManager_getSimCountryIso", SpoofKind::Iso},
        {"TelephonyManager_getNetworkCountryIso", SpoofKind::Iso},
        // MCC+MNC (numeric — never "de")
        {"bpea-getSimOperator", SpoofKind::Mcc},
        {"bpea-getNetworkOperator", SpoofKind::Mcc},
        {"TelephonyManager_getSimOperator", SpoofKind::Mcc},
        {"TelephonyManager_getNetworkOperator", SpoofKind::Mcc},
        // operator display name
        {"bpea-getSimOperatorName", SpoofKind::Name},
        {"bpea-getNetworkOperatorName", SpoofKind::Name},
        {"TelephonyManager_getSimOperatorName", SpoofKind::Name},
        {"TelephonyManager_getNetworkOperatorName", SpoofKind::Name},
    };

    std::set<uint32_t> tags_iso, tags_mcc, tags_name;
    bool need_mcc = false, need_name = false;
    for (auto& [tag, kind] : tag_map) {
        if (auto id = dex.find_string(tag)) {
            if (kind == SpoofKind::Iso) tags_iso.insert(*id);
            else if (kind == SpoofKind::Mcc) { tags_mcc.insert(*id); need_mcc = true; }
            else if (kind == SpoofKind::Name) { tags_name.insert(*id); need_name = true; }
        }
    }

    // MCC/name only when the exact (or known-good) string already lives in this dex.
    // In-place digit rewrite / string_ids inject caused NoClassDefFoundError on 46.4.3.
    if (need_mcc) {
        static const char* kMccCandidates[] = {
            "26201", "26202", "26203", "26207", "31026", "310260", "23415", "20801"
        };
        for (auto* cand : kMccCandidates) {
            if (auto id = dex.find_string(cand)) {
                // Prefer exact profile length (5 for DE Telekom 26201).
                if (std::string(cand) == prof.mcc_mnc || std::strlen(cand) == std::strlen(prof.mcc_mnc)) {
                    mcc_idx = *id; have_mcc = true;
                    if (std::string(cand) == prof.mcc_mnc) break;
                }
            }
        }
        if (!have_mcc) {
            if (auto id = dex.find_string(prof.mcc_mnc)) {
                mcc_idx = *id; have_mcc = true;
            }
        }
    }
    if (!have_mcc) tags_mcc.clear();

    if (need_name) {
        static const char* kNameCandidates[] = {
            "Telekom", "T-Mobile", "Vodafone", "O2", "Orange"
        };
        for (auto* cand : kNameCandidates) {
            if (auto id = dex.find_string(cand)) {
                name_idx = *id; have_name = true;
                if (std::string(cand) == prof.op_name) break;
            }
        }
    }
    if (!have_name) tags_name.clear();

    // Region hub class (carrier_region cascade)
    std::set<uint32_t> force_classes;
    for (uint32_t i = 0; i < dex.type_ids_size; ++i) {
        auto t = dex.type_name(i);
        if (t == "LX/155y;" || t == "LX/C4936155y;")
            force_classes.insert(i);
    }

    // code_off → string index to return
    std::vector<std::pair<uint32_t, uint32_t>> stubs;
    std::set<uint32_t> classes_touched;
    int n_iso = 0, n_mcc = 0, n_name = 0, n_hub = 0;

    auto add_stub = [&](uint32_t code_off, uint32_t class_idx, uint32_t str_idx, int* counter) {
        stubs.emplace_back(code_off, str_idx);
        classes_touched.insert(class_idx);
        if (counter) (*counter)++;
    };

    for (uint32_t c = 0; c < dex.class_defs_size; ++c) {
        size_t coff = dex.class_defs_off + c * 32u;
        if (coff + 32 > dex.data.size()) break;
        uint32_t class_idx = ru32(dex.data.data() + coff + 0);
        uint32_t class_data_off = ru32(dex.data.data() + coff + 24);
        if (!class_data_off) continue;

        size_t o = class_data_off;
        uint32_t sf = read_uleb128(dex.data.data(), dex.data.size(), o);
        uint32_t inf = read_uleb128(dex.data.data(), dex.data.size(), o);
        uint32_t dm = read_uleb128(dex.data.data(), dex.data.size(), o);
        uint32_t vm = read_uleb128(dex.data.data(), dex.data.size(), o);
        for (uint32_t i = 0; i < sf + inf; ++i) {
            read_uleb128(dex.data.data(), dex.data.size(), o);
            read_uleb128(dex.data.data(), dex.data.size(), o);
        }

        struct Meth {
            uint32_t code_off;
            uint32_t access;
            bool returns_string;
            std::string name;
        };
        std::vector<Meth> meths;

        auto walk = [&](uint32_t n) {
            uint32_t mi = 0;
            for (uint32_t i = 0; i < n; ++i) {
                mi += read_uleb128(dex.data.data(), dex.data.size(), o);
                uint32_t access = read_uleb128(dex.data.data(), dex.data.size(), o);
                uint32_t code_off = read_uleb128(dex.data.data(), dex.data.size(), o);
                if (!code_off) continue;
                auto mid = dex.method_id(mi);
                bool rs = dex.proto_returns_string(mid.proto_idx);
                std::string mname = dex.string_at(mid.name_idx);
                meths.push_back({code_off, access, rs, mname});

                // BPEA leaf helpers only:
                //  - must return String
                //  - must be static (wrappers are static)
                //  - tries_size == 0  (outer methods with try/catch also embed the
                //    tag string for findCert(); early-return stubs there crash ART
                //    with ClassNotFound on unrelated types — verified on 46.4.3)
                if (!rs) continue;
                if ((access & 0x8) == 0) continue; // ACC_STATIC
                if (code_off + 16 > dex.data.size()) continue;
                uint16_t tries = ru16(dex.data.data() + code_off + 6);
                if (tries != 0) continue;

                if (code_has_const_string(dex.data, code_off, tags_iso)) {
                    add_stub(code_off, class_idx, iso_idx, &n_iso);
                } else if (have_mcc && code_has_const_string(dex.data, code_off, tags_mcc)) {
                    add_stub(code_off, class_idx, mcc_idx, &n_mcc);
                } else if (have_name && code_has_const_string(dex.data, code_off, tags_name)) {
                    add_stub(code_off, class_idx, name_idx, &n_name);
                }
            }
        };
        walk(dm);
        walk(vm);

        // 155y region hub: force country ISO on static String getters (no try/catch).
        if (force_classes.count(class_idx) && meths.size() >= 4 && meths.size() <= 40) {
            static const char* kNames[] = {
                "LIZ", "LIZIZ", "LIZJ", "LIZLLL", "LJ", "LJFF", "getSimCountry",
                "getCarrierRegion", "getSysRegion", "getRegion", "getOpRegion"
            };
            uint32_t hub_str = have_iso_upper ? iso_upper_idx : iso_idx;
            for (auto& m : meths) {
                if (!(m.access & 0x8) || !m.returns_string) continue;
                if (m.code_off + 16 > dex.data.size()) continue;
                if (ru16(dex.data.data() + m.code_off + 6) != 0) continue; // tries_size
                for (auto* kn : kNames) {
                    if (m.name == kn) {
                        add_stub(m.code_off, class_idx, hub_str, &n_hub);
                        break;
                    }
                }
            }
        }
    }

    // Dedup code_offs (same method may match twice)
    std::set<uint32_t> seen;
    int stubbed = 0;
    for (auto& [code_off, str_idx] : stubs) {
        if (!seen.insert(code_off).second) continue;
        if (stub_return_string(dex.data, code_off, str_idx)) stubbed++;
    }

    if (stubbed > 0)
        fix_dex_checksum(dex.data);

    buf.swap(dex.data);
    rep.methods_stubbed = stubbed;
    rep.classes_hit = static_cast<int>(classes_touched.size());
    if (stubbed) {
        rep.notes.push_back(
            label + ": SIM spoof stubbed " + std::to_string(stubbed) +
            " method(s) in " + std::to_string(classes_touched.size()) +
            " class(es) [iso=" + std::to_string(n_iso) +
            " mcc=" + std::to_string(n_mcc) +
            " name=" + std::to_string(n_name) +
            " hub=" + std::to_string(n_hub) +
            " mcc_str=" + (have_mcc ? "1" : "0") +
            " name_str=" + (have_name ? "1" : "0") +
            "]");
    } else {
        rep.notes.push_back(label + ": no SIM targets");
    }
    return rep;
}

} // namespace dex
