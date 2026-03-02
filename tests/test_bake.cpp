/*
 * DECIMA-8 Source Code
 * This code is part of Decima-8 Core
 *
 * All rights belong to the ORDEN (c) 2026
 */

#include "d8/bake.hpp"
#include "bake_gen.hpp"
#include "d8/logger.hpp"
#include <array>
#include <cstring>
#include <vector>

static int expect(bool cond, const char* msg) {
    if (!cond) {
        spdlog::error("EXPECT failed: {}", msg);
        return 1;
    }
    return 0;
}

int test_bake() {
    using namespace d8;

    // 1) Empty -> BakeNoBlob
    {
        auto st = bake::validate({});
        if (expect(!st && st.c == status::code::BakeNoBlob, "empty blob must be BakeNoBlob")) return 1;
    }

    // 2) Too short -> BakeBadLen
    {
        std::array<u8, 10> buf{};
        auto st = bake::validate(std::span<const u8>(buf.data(), buf.size()));
        if (expect(!st && st.c == status::code::BakeBadLen, "short blob must be BakeBadLen")) return 1;
    }

    // 3) BakeBadMagic
    {
        std::array<u8, 28> buf{};
        buf[0] = 'X'; buf[1] = 'X'; buf[2] = 'X'; buf[3] = 'X'; // wrong magic
        d8::write_le_u16(buf.data() + 4, d8::kVerMajor);
        d8::write_le_u16(buf.data() + 6, d8::kVerMinor);
        d8::write_le_u32(buf.data() + 8, 0);
        d8::write_le_u32(buf.data() + 12, 28);
        auto st = bake::validate(std::span<const u8>(buf.data(), buf.size()));
        if (expect(!st && st.c == status::code::BakeBadMagic, "wrong magic must be BakeBadMagic")) return 1;
    }

    // 4) BakeBadVersion
    {
        auto blob = testgen::make_valid_bake_blob();
        blob[4] = 0xFF; blob[5] = 0xFF; // wrong ver_major
        auto st = bake::validate(blob);
        if (expect(!st && st.c == status::code::BakeBadVersion, "wrong version must be BakeBadVersion")) return 1;
    }

    // 5) BakeMissingTLV (header only)
    {
        std::array<u8, 28> buf{};
        buf[0] = 'D'; buf[1] = '8'; buf[2] = 'B'; buf[3] = 'K';
        d8::write_le_u16(buf.data() + 4, d8::kVerMajor);
        d8::write_le_u16(buf.data() + 6, d8::kVerMinor);
        d8::write_le_u32(buf.data() + 8, 0);
        d8::write_le_u32(buf.data() + 12, 28);
        d8::write_le_u32(buf.data() + 16, 1);
        d8::write_le_u32(buf.data() + 20, 2);
        d8::write_le_u32(buf.data() + 24, 0);
        auto st = bake::validate(std::span<const u8>(buf.data(), buf.size()));
        if (expect(!st && st.c == status::code::BakeMissingTLV, "header-only must be BakeMissingTLV")) return 1;
    }

    // 6) BakeBadTLVLen (TLV_TILE_PARAMS_V2 wrong size)
    {
        auto blob = testgen::make_valid_bake_blob();
        // Find TLV_TILE_PARAMS_V2 and corrupt its len
        for (std::size_t i = 28; i + 8 <= blob.size();) {
            u16 type = d8::read_le_u16(blob.data() + i);
            if (type == TLV_TILE_PARAMS_V2) {
                d8::write_le_u32(blob.data() + i + 4, 1); // wrong len
                break;
            }
            u32 len = d8::read_le_u32(blob.data() + i + 4);
            std::size_t pad = (4 - (len & 3u)) & 3u;
            i += 8 + len + pad;
        }
        auto st = bake::validate(blob);
        if (expect(!st && (st.c == status::code::BakeBadTLVLen || st.c == status::code::BakeBadLen), 
                   "wrong TLV len must fail")) return 1;
    }

    // 7) BakeCRCFail
    {
        auto blob = testgen::make_valid_bake_blob();
        // Corrupt CRC32 value (last 4 bytes before TLV_CRC32)
        if (blob.size() >= 4) {
            blob[blob.size() - 5] ^= 0xFF; // flip CRC byte
        }
        auto st = bake::validate(blob);
        if (expect(!st && st.c == status::code::BakeCRCFail, "corrupted CRC must be BakeCRCFail")) return 1;
    }

    // 8) BakeBadPadding (non-zero padding)
    {
        auto blob = testgen::make_valid_bake_blob();
        // Find a TLV with padding and corrupt it
        for (std::size_t i = 28; i + 8 < blob.size();) {
            // u16 type = d8::read_le_u16(blob.data() + i); // Не используется
            u32 len = d8::read_le_u32(blob.data() + i + 4);
            std::size_t tlv_end = i + 8 + len;
            std::size_t pad = (4 - (len & 3u)) & 3u;
            if (pad > 0 && tlv_end + pad < blob.size()) {
                blob[tlv_end] = 0xFF; // non-zero padding
                break;
            }
            i = tlv_end + pad;
        }
        auto st = bake::validate(blob);
        if (expect(!st && st.c == status::code::BakeBadPadding, "non-zero padding must be BakeBadPadding")) return 1;
    }

    // 9) TopologyMismatch (tile_count != tile_w * tile_h)
    {
        auto blob = testgen::make_valid_bake_blob();
        // Find TLV_TOPOLOGY and corrupt tile_count
        for (std::size_t i = 28; i + 8 <= blob.size();) {
            u16 type = d8::read_le_u16(blob.data() + i);
            if (type == TLV_TOPOLOGY) {
                d8::write_le_u32(blob.data() + i + 8, 9999); // wrong tile_count
                break;
            }
            u32 len = d8::read_le_u32(blob.data() + i + 4);
            std::size_t pad = (4 - (len & 3u)) & 3u;
            i += 8 + len + pad;
        }
        auto st = bake::validate(blob);
        if (expect(!st && st.c == status::code::TopologyMismatch, "topology mismatch must fail")) return 1;
    }

    // 10) BakeReservedNonZero
    {
        auto blob = testgen::make_valid_bake_blob();
        // Corrupt reserved0 in header
        d8::write_le_u32(blob.data() + 24, 0xDEADBEEF);
        auto st = bake::validate(blob);
        if (expect(!st && st.c == status::code::BakeReservedNonZero, "non-zero reserved must fail")) return 1;
    }

    // 11) RoundtripStable (parse → serialize → parse → identical)
    {
        auto blob1 = testgen::make_valid_bake_blob();
        bake::bake_view v1{};
        auto st1 = bake::parse_view(blob1, v1);
        if (expect(bool(st1), "parse must succeed")) return 1;

        std::vector<u8> buf2(blob1.size() * 2);
        std::size_t len2 = 0;
        auto st2 = bake::serialize_canonical(v1, buf2, len2);
        if (expect(bool(st2), "serialize must succeed")) return 1;

        bake::bake_view v2{};
        auto st3 = bake::parse_view(std::span<const u8>(buf2.data(), len2), v2);
        if (expect(bool(st3), "re-parse must succeed")) return 1;

        // Compare key fields
        if (expect(v1.header.bake_id == v2.header.bake_id, "bake_id must match")) return 1;
        if (expect(v1.topo.tile_count == v2.topo.tile_count, "tile_count must match")) return 1;
    }

    return 0;
}
