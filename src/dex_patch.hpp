#pragma once
/* Surgical DEX region stubber — version-resilient.
 *
 * Strategy (in order):
 *  1. Find methods that const-string BPEA tags (bpea-getSimCountryIso, …)
 *     and stub those (ByteDance privacy wrappers around TelephonyManager).
 *  2. Stub any static method that directly invoke-virtual getSimCountryIso /
 *     getNetworkCountryIso and returns String (any arity).
 *  3. If LX/155y; still has static String() getters (older builds), stub them.
 *
 * Never mass-stubs huge utility classes — that crashed TikTok (ClassNotFound
 * after verifier killed classes14.dex).
 */
#include "util.hpp"
#include <cstring>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
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

// Early-return stub: overwrite only the first instructions with
//   const-string vN, "de" / return-object vN
// Leave the rest of the original bytecode, tries_size and debug_info_off
// intact so ART verification of try tables / debug still passes.
// (Zeroing tries/debug caused "Invalid debug_info_off" / class load failure.)
inline bool stub_code_item(std::vector<uint8_t>& data, uint32_t code_off, uint32_t de_str_idx) {
    if (code_off == 0 || code_off + 16 > data.size()) return false;
    uint8_t* base = data.data() + code_off;
    uint16_t registers_size = ru16(base + 0);
    uint16_t ins_size = ru16(base + 2);
    uint32_t insns_size = ru32(base + 12);
    const bool jumbo = de_str_idx > 0xFFFFu;
    const uint32_t need = jumbo ? 4u : 3u; // units to write
    if (insns_size < need) return false;
    if (code_off + 16 + insns_size * 2u > data.size()) return false;

    // Dalvik params occupy the last `ins_size` registers.
    // Prefer a free local at v0 when registers_size > ins_size; else reuse v0 anyway.
    if (registers_size < 1) {
        wu16(base + 0, static_cast<uint16_t>(ins_size + 1));
        registers_size = static_cast<uint16_t>(ins_size + 1);
    }
    const uint8_t v = 0; // always v0 (local or first param — both OK before return)

    uint8_t* insns = base + 16;
    if (jumbo) {
        // const-string/jumbo vAA, string@BBBBBBBB  (3 units) + return-object (1)
        wu16(insns + 0, static_cast<uint16_t>(0x001b | (uint16_t(v) << 8)));
        wu16(insns + 2, static_cast<uint16_t>(de_str_idx & 0xffff));
        wu16(insns + 4, static_cast<uint16_t>((de_str_idx >> 16) & 0xffff));
        wu16(insns + 6, static_cast<uint16_t>(0x0011 | (uint16_t(v) << 8)));
    } else {
        wu16(insns + 0, static_cast<uint16_t>(0x001a | (uint16_t(v) << 8)));
        wu16(insns + 2, static_cast<uint16_t>(de_str_idx & 0xffff));
        wu16(insns + 4, static_cast<uint16_t>(0x0011 | (uint16_t(v) << 8)));
    }
    // Do NOT nop-fill, do NOT touch tries_size / debug_info_off.
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
        if (op == 0x1a) { // const-string
            if (string_idxs.count(insns[i + 1])) return true;
        } else if (op == 0x1b && i + 2 < insns_size) { // const-string/jumbo
            uint32_t idx = uint32_t(insns[i + 1]) | (uint32_t(insns[i + 2]) << 16);
            if (string_idxs.count(idx)) return true;
        }
    }
    return false;
}

inline bool code_refs_methods(const std::vector<uint8_t>& data, uint32_t code_off,
                              const std::set<uint32_t>& targets) {
    if (code_off == 0 || code_off + 16 > data.size() || targets.empty()) return false;
    const uint8_t* base = data.data() + code_off;
    uint32_t insns_size = ru32(base + 12);
    if (code_off + 16 + insns_size * 2u > data.size()) return false;
    const uint16_t* insns = reinterpret_cast<const uint16_t*>(base + 16);
    for (uint32_t i = 0; i + 1 < insns_size; ++i) {
        uint8_t op = insns[i] & 0xff;
        if ((op >= 0x6e && op <= 0x72) || (op >= 0x74 && op <= 0x78)) {
            if (targets.count(insns[i + 1])) return true;
        }
    }
    return false;
}

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

    auto de = dex.find_string("DE");
    if (!de) de = dex.find_string("de");
    if (!de) de = dex.find_string("US");
    if (!de) de = dex.find_string("GB");
    if (!de) {
        for (uint32_t i = 0; i < std::min(dex.string_ids_size, 300000u); ++i) {
            auto s = dex.string_at(i);
            if (s.size() == 2 && s[0] >= 'A' && s[0] <= 'Z' && s[1] >= 'A' && s[1] <= 'Z') {
                de = i;
                break;
            }
        }
    }
    if (!de) {
        rep.notes.push_back(label + ": no country string");
        buf.swap(dex.data);
        return rep;
    }

    // ONLY country-ISO BPEA tags. Do NOT touch getSimOperator / operator name —
    // those must stay numeric MCC+MNC (e.g. "25062"). Returning "de" there
    // breaks mcc_mnc / CDN / profile media after device register.
    static const char* kCountryBpeaTags[] = {
        "bpea-getSimCountryIso",
        "bpea-getNetworkCountryIso",
        "TelephonyManager_getSimCountryIso",
        "TelephonyManager_getNetworkCountryIso",
    };
    std::set<uint32_t> bpea_strings;
    for (auto* tag : kCountryBpeaTags) {
        if (auto id = dex.find_string(tag)) bpea_strings.insert(*id);
    }

    // Region hub (older builds): static String getters LIZ/LJ/LJFF…
    std::set<uint32_t> force_classes;
    for (uint32_t i = 0; i < dex.type_ids_size; ++i) {
        auto t = dex.type_name(i);
        if (t == "LX/155y;" || t == "LX/C4936155y;")
            force_classes.insert(i);
    }

    std::set<uint32_t> code_offs_to_stub;
    std::set<uint32_t> classes_touched;

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
            uint32_t method_idx;
            bool returns_string;
            std::string name;
        };
        std::vector<Meth> meths;
        int country_bpea_hits = 0;

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
                meths.push_back({code_off, access, mi, rs, mname});

                // Strict: only methods that literally contain country-ISO BPEA tags
                if (rs && (access & 0x8) &&
                    code_has_const_string(dex.data, code_off, bpea_strings)) {
                    code_offs_to_stub.insert(code_off);
                    classes_touched.insert(class_idx);
                    country_bpea_hits++;
                }
            }
        };
        walk(dm);
        walk(vm);

        // Old 155y hub (if it still has real static getters, not an empty shell)
        if (force_classes.count(class_idx) && meths.size() >= 4 && meths.size() <= 40) {
            static const char* kNames[] = {
                "LIZ", "LIZIZ", "LIZJ", "LIZLLL", "LJ", "LJFF", "getSimCountry",
                "getCarrierRegion", "getSysRegion", "getRegion", "getOpRegion"
            };
            for (auto& m : meths) {
                if (!(m.access & 0x8) || !m.returns_string) continue;
                for (auto* kn : kNames) {
                    if (m.name == kn) {
                        code_offs_to_stub.insert(m.code_off);
                        classes_touched.insert(class_idx);
                        break;
                    }
                }
            }
        }
        (void)country_bpea_hits;
    }

    int stubbed = 0;
    for (uint32_t code_off : code_offs_to_stub) {
        if (stub_code_item(dex.data, code_off, *de)) stubbed++;
    }

    // ART rejects modified dex with "Bad checksum" if we don't fix the header.
    // checksum @8: adler32 of bytes [12 .. end)
    // signature @12: SHA-1 of bytes [32 .. end) — optional on many devices,
    // but recompute both for safety (adler is mandatory on Xiaomi/ART here).
    if (stubbed > 0 && dex.data.size() > 32) {
        // Adler-32
        uint32_t a = 1, b = 0;
        const uint8_t* p = dex.data.data() + 12;
        size_t n = dex.data.size() - 12;
        for (size_t i = 0; i < n; ++i) {
            a = (a + p[i]) % 65521;
            b = (b + a) % 65521;
        }
        uint32_t sum = (b << 16) | a;
        wu32(dex.data.data() + 8, sum);

        // SHA-1 of data[32..] into signature field at [12..32)
        {
            uint32_t h0=0x67452301,h1=0xEFCDAB89,h2=0x98BADCFE,h3=0x10325476,h4=0xC3D2E1F0;
            auto rol=[](uint32_t x,int n){return (x<<n)|(x>>(32-n));};
            const uint8_t* data = dex.data.data() + 32;
            size_t len = dex.data.size() - 32;
            size_t padlen = ((len + 8) / 64 + 1) * 64;
            std::vector<uint8_t> msg(padlen, 0);
            std::memcpy(msg.data(), data, len);
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
                dex.data[12+o]=(v>>24)&0xff; dex.data[13+o]=(v>>16)&0xff;
                dex.data[14+o]=(v>>8)&0xff; dex.data[15+o]=v&0xff;
            };
            put_be(h0,0); put_be(h1,4); put_be(h2,8); put_be(h3,12); put_be(h4,16);
        }

        // checksum AFTER signature (signature sits inside the hashed range for adler)
        a = 1; b = 0;
        p = dex.data.data() + 12;
        n = dex.data.size() - 12;
        for (size_t i = 0; i < n; ++i) {
            a = (a + p[i]) % 65521;
            b = (b + a) % 65521;
        }
        sum = (b << 16) | a;
        wu32(dex.data.data() + 8, sum);
    }

    buf.swap(dex.data);
    rep.methods_stubbed = stubbed;
    rep.classes_hit = static_cast<int>(classes_touched.size());
    if (stubbed) {
        rep.notes.push_back(label + ": stubbed " + std::to_string(stubbed) +
                            " method(s) in " + std::to_string(classes_touched.size()) +
                            " class(es) [checksum fixed]");
    } else {
        rep.notes.push_back(label + ": no targets");
    }
    return rep;
}

} // namespace dex
