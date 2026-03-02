/*
 * DECIMA-8 Source Code
 * This code is part of Decima-8 Core
 *
 * All rights belong to the ORDEN (c) 2026
 */

#include "d8/bake.hpp"

namespace d8::bake {

    static status fail(status::code c, u32 aux, const char* msg) noexcept {
        return status{ c, aux, msg };
    }

    static bool is_known_tlv(u16 t) noexcept {
        using namespace d8;
        switch (t) {
        case TLV_TOPOLOGY:
        case TLV_TILE_PARAMS_V2:
        case TLV_TILE_ROUTING_FLAGS16:
        case TLV_READOUT_POLICY:
        case TLV_RESET_ON_FIRE_MASK16:
        case TLV_TILE_WEIGHTS_PACKED:
        case TLV_TILE_FIELD_LIMIT:
        case TLV_CRC32:
            return true;
        default:
            return false;
        }
    }

    status parse_view(std::span<const u8> blob, bake_view& out) noexcept {
        if (blob.empty()) return fail(status::code::BakeNoBlob, 0, "No blob");
        if (blob.size() < 28) return fail(status::code::BakeBadLen, 0, "Header truncated");

        // Header LE parse
        const u8* p = blob.data();
        out.header.magic[0] = char(p[0]); out.header.magic[1] = char(p[1]);
        out.header.magic[2] = char(p[2]); out.header.magic[3] = char(p[3]);
        out.header.ver_major = read_le_u16(p + 4);
        out.header.ver_minor = read_le_u16(p + 6);
        out.header.flags = read_le_u32(p + 8);
        out.header.total_len = read_le_u32(p + 12);
        out.header.bake_id = read_le_u32(p + 16);
        out.header.profile_id = read_le_u32(p + 20);
        out.header.reserved0 = read_le_u32(p + 24);

        // defaults
        out.tile_params = {};
        out.tile_routing = {};
        out.weights_packed = {};
        out.reset_on_fire = {};
        out.crc32_stored = 0;
        out.crc_tlv_header_off = 0;
        out.tile_field_limit = 0;

        // TLVs
        tlv_cursor cur{ blob.subspan(28), 0 };
        status st{};

        bool seen_topo = false, seen_params = false, seen_routing = false, seen_readout = false;
        bool seen_reset = false, seen_weights = false, seen_crc = false;

        while (true) {
            tlv_view tv{};
            const bool has = cur.next(tv, st);
            if (!has) break;
            if (!st) return st;

            // tv.header_off is relative to TLV region; convert to absolute
            tv.header_off += 28;
            tv.value_off += 28;

            // strict: tflags must be 0 (RISK: контракт явно не сказал, но "reserved" логично)
            if (tv.tflags != 0) return fail(status::code::BakeReservedNonZero, tv.type, "TLV tflags must be 0");

            if (!is_known_tlv(tv.type)) return fail(status::code::BakeUnknownTLV, tv.type, "Unknown TLV type");

            if (tv.type == TLV_CRC32) {
                seen_crc = true;
                out.crc_tlv_header_off = tv.header_off;
                if (tv.len != 4) return fail(status::code::BakeBadTLVLen, tv.type, "CRC32 TLV len must be 4");
                out.crc32_stored = read_le_u32(tv.value.data());
                // must be last TLV: cursor must be at end now
                if (cur.off != cur.blob.size()) {
                    return fail(status::code::BakeBadLen, tv.type, "TLV_CRC32 must be last");
                }
                break;
            }

            // store raw spans
            switch (tv.type) {
            case TLV_TOPOLOGY:
                seen_topo = true;
                if (tv.len != LEN_TOPOLOGY) return fail(status::code::BakeBadTLVLen, tv.type, "Topology len must be 16");
                {
                    const u8* q = tv.value.data();
                    out.topo.tile_count = read_le_u32(q + 0);
                    out.topo.tile_w = read_le_u16(q + 4);
                    out.topo.tile_h = read_le_u16(q + 6);
                    out.topo.lanes = q[8];
                    out.topo.domains = q[9];
                    out.topo.reserved = read_le_u16(q + 10);
                    out.topo.reserved2 = read_le_u32(q + 12);
                }
                break;
            case TLV_TILE_PARAMS_V2:
                seen_params = true;
                out.tile_params = tv.value;
                break;
            case TLV_TILE_ROUTING_FLAGS16:
                seen_routing = true;
                out.tile_routing = tv.value;
                break;
            case TLV_READOUT_POLICY:
                seen_readout = true;
                if (tv.len != LEN_READOUT_POLICY) return fail(status::code::BakeBadTLVLen, tv.type, "ReadoutPolicy len must be 12");
                {
                    const u8* q = tv.value.data();
                    out.readout.mode = static_cast<readout_mode>(q[0]);
                    out.readout.reserved0 = q[1];
                    out.readout.winner_domain_mask = read_le_u16(q + 2);
                    out.readout.settle_ns = read_le_u16(q + 4);
                    out.readout.reserved1 = read_le_u16(q + 6);
                    out.readout.reserved2 = read_le_u32(q + 8);
                }
                break;
            case TLV_RESET_ON_FIRE_MASK16:
                seen_reset = true;
                out.reset_on_fire = tv.value;
                break;
            case TLV_TILE_WEIGHTS_PACKED:
                seen_weights = true;
                out.weights_packed = tv.value;
                break;
            case TLV_TILE_FIELD_LIMIT:
                if (tv.len != 4) return fail(status::code::BakeBadTLVLen, tv.type, "TileFieldLimit len must be 4");
                out.tile_field_limit = read_le_u32(tv.value.data());
                break;
            default:
                // known but not stored separately
                break;
            }
        }

        if (!seen_crc) return fail(status::code::BakeMissingTLV, TLV_CRC32, "Missing TLV_CRC32");

        // required TLVs presence check
        if (!seen_topo)   return fail(status::code::BakeMissingTLV, TLV_TOPOLOGY, "Missing TOPOLOGY");
        if (!seen_params) return fail(status::code::BakeMissingTLV, TLV_TILE_PARAMS_V2, "Missing TILE_PARAMS_V2");
        if (!seen_routing)return fail(status::code::BakeMissingTLV, TLV_TILE_ROUTING_FLAGS16, "Missing TILE_ROUTING_FLAGS16");
        if (!seen_readout)return fail(status::code::BakeMissingTLV, TLV_READOUT_POLICY, "Missing READOUT_POLICY");
        if (!seen_reset)  return fail(status::code::BakeMissingTLV, TLV_RESET_ON_FIRE_MASK16, "Missing RESET_ON_FIRE_MASK16");
        if (!seen_weights)return fail(status::code::BakeMissingTLV, TLV_TILE_WEIGHTS_PACKED, "Missing TILE_WEIGHTS_PACKED");

        return status{};
    }

    static status validate_reserved_and_sizes(const bake_view& v) noexcept {
        // Header
        if (v.header.magic[0] != kMagic[0] || v.header.magic[1] != kMagic[1] ||
            v.header.magic[2] != kMagic[2] || v.header.magic[3] != kMagic[3]) {
            return fail(status::code::BakeBadMagic, 0, "Bad magic");
        }
        if (v.header.ver_major != kVerMajor || v.header.ver_minor != kVerMinor) {
            return fail(status::code::BakeBadVersion, 0, "Bad version");
        }
        if (v.header.reserved0 != 0) {
            return fail(status::code::BakeReservedNonZero, 0, "Header reserved must be 0");
        }
        const u32 allowed_flags = d8::BAKE_FLAG_DOUBLE_STRAIT;
        if ((v.header.flags & ~allowed_flags) != 0) {
            return fail(status::code::BakeReservedNonZero, 0, "Header flags contain unknown bits");
        }

        // Topology
        const auto& t = v.topo;
        if (t.lanes != kLanes || t.domains != kDomains) {
            return fail(status::code::TopologyMismatch, 0, "Topology lanes/domains mismatch");
        }
        if (t.reserved != 0 || t.reserved2 != 0) {
            return fail(status::code::BakeReservedNonZero, TLV_TOPOLOGY, "Topology reserved must be 0");
        }
        if (t.tile_count != u32(t.tile_w) * u32(t.tile_h)) {
            return fail(status::code::TopologyMismatch, 0, "tile_count != w*h");
        }

        // strict runtime/IDE topology (see explanation in response)
        if (t.tile_count != kTileCount || t.tile_w != kExpectedW || t.tile_h != kExpectedH) {
            return fail(status::code::TopologyMismatch, 0, "Expected 128x32 (4096)");
        }

        // Optional: tile field limit (<= kTileCount). 0 means "no limit".
        if (v.tile_field_limit != 0 && (v.tile_field_limit < 1 || v.tile_field_limit > kTileCount)) {
            return fail(status::code::BakeBadTLVLen, TLV_TILE_FIELD_LIMIT, "TileFieldLimit out of range");
        }

        // strict lengths of required TLVs
        const u32 tc = t.tile_count;
        if (v.tile_params.size() != tc * SZ_TILE_PARAMS_V2) return fail(status::code::BakeBadTLVLen, TLV_TILE_PARAMS_V2, "Bad TILE_PARAMS_V2 len");
        if (v.tile_routing.size() != tc * SZ_TILE_ROUTING_FLAGS16) return fail(status::code::BakeBadTLVLen, TLV_TILE_ROUTING_FLAGS16, "Bad TILE_ROUTING_FLAGS16 len");
        if (v.weights_packed.size() != tc * SZ_TILE_WEIGHTS_PACKED) return fail(status::code::BakeBadTLVLen, TLV_TILE_WEIGHTS_PACKED, "Bad WEIGHTS len");
        if (v.reset_on_fire.size() != tc * SZ_RESET_ON_FIRE_MASK16) return fail(status::code::BakeBadTLVLen, TLV_RESET_ON_FIRE_MASK16, "Bad RESET_ON_FIRE len");

        // ReadoutPolicy reserved checks
        if (v.readout.reserved0 != 0 || v.readout.reserved1 != 0 || v.readout.reserved2 != 0) {
            return fail(status::code::BakeReservedNonZero, TLV_READOUT_POLICY, "ReadoutPolicy reserved must be 0");
        }
        if (!(v.readout.mode == readout_mode::R0_RAW_BUS || v.readout.mode == readout_mode::R1_DOMAIN_WINNER_ID32)) {
            return fail(status::code::BakeReservedNonZero, TLV_READOUT_POLICY, "Unknown readout mode");
        }

        // Tile params validation (reserved fields, thr_lo/hi/decay)
        for (std::size_t t_id = 0; t_id < tc; ++t_id) {
            const u8* p = v.tile_params.data() + t_id * SZ_TILE_PARAMS_V2;
            const i16 thr_lo = static_cast<i16>(read_le_u16(p + 0));
            const i16 thr_hi = static_cast<i16>(read_le_u16(p + 2));
            const u16 decay = read_le_u16(p + 4);
            const u8 domain_id = p[6];
            const u8 flags8 = p[9];
            const u16 reserved = read_le_u16(p + 10);

            if (thr_lo > thr_hi) return fail(status::code::BakeReservedNonZero, u32(t_id), "thr_lo16 > thr_hi16");
            if (decay > 32767) return fail(status::code::BakeReservedNonZero, u32(t_id), "decay16 out of range");
            if ((domain_id & 0xF0) != 0) return fail(status::code::BakeReservedNonZero, u32(t_id), "domain_id high nibble must be 0");
            if (flags8 != 0 || reserved != 0) return fail(status::code::BakeReservedNonZero, u32(t_id), "TileParamsV2 reserved/flags non-zero");
        }

        // Routing flags reserved bits must be zero
        for (std::size_t t_id = 0; t_id < tc; ++t_id) {
            const u8* p = v.tile_routing.data() + t_id * SZ_TILE_ROUTING_FLAGS16;
            const u16 flags = read_le_u16(p);
            if ((flags & 0xFC00u) != 0) {
                return fail(status::code::BakeReservedNonZero, u32(t_id), "routing_flags16 reserved bits must be 0");
            }
        }

        return status{};
    }

    status validate(std::span<const u8> blob, bake_view* out_view) noexcept {
        if (blob.empty()) return fail(status::code::BakeNoBlob, 0, "No blob");
        if (blob.size() < 28) return fail(status::code::BakeBadLen, 0, "Header truncated");

        // Early header checks for correct error codes
        if (!(blob[0] == kMagic[0] && blob[1] == kMagic[1] && blob[2] == kMagic[2] && blob[3] == kMagic[3])) {
            return fail(status::code::BakeBadMagic, 0, "Bad magic");
        }
        const u16 ver_major = read_le_u16(blob.data() + 4);
        const u16 ver_minor = read_le_u16(blob.data() + 6);
        if (ver_major != kVerMajor || ver_minor != kVerMinor) {
            return fail(status::code::BakeBadVersion, 0, "Bad version");
        }

        // total_len must match actual blob size
        const u32 total_len = read_le_u32(blob.data() + 12);
        if (total_len != blob.size()) return fail(status::code::BakeBadLen, total_len, "total_len mismatch");

        bake_view v{};
        auto st = parse_view(blob, v);
        if (!st) return st;

        st = validate_reserved_and_sizes(v);
        if (!st) return st;

        // CRC32 over [0 .. crc_tlv_header_off)
        if (v.crc_tlv_header_off == 0 || v.crc_tlv_header_off > blob.size()) {
            return fail(status::code::BakeBadLen, 0, "CRC TLV offset invalid");
        }
        const u32 crc_calc = crc32_ieee::compute(blob.subspan(0, v.crc_tlv_header_off));
        if (crc_calc != v.crc32_stored) {
            return fail(status::code::BakeCRCFail, crc_calc, "CRC mismatch");
        }

        if (out_view) *out_view = v;
        return status{};
    }

    static inline std::size_t tile_xy_to_id(std::size_t x, std::size_t y) noexcept {
        return y * kExpectedW + x;
    }

    status compile(std::span<const u8> blob, compiled_image& out_img) noexcept {
        bake_view v{};
        auto st = validate(blob, &v);
        if (!st) return st;

        out_img.topo = v.topo;
        out_img.bake_id = v.header.bake_id;
        out_img.profile_id = v.header.profile_id;
        out_img.bake_flags = v.header.flags;
        out_img.readout = v.readout;
        out_img.tile_field_limit = v.tile_field_limit;

        // parse TileParamsV2
        for (std::size_t t = 0; t < kTileCount; ++t) {
            const u8* p = v.tile_params.data() + t * SZ_TILE_PARAMS_V2;
            const i16 thr_lo = static_cast<i16>(read_le_u16(p + 0));
            const i16 thr_hi = static_cast<i16>(read_le_u16(p + 2));
            const u16 decay = read_le_u16(p + 4);
            const u8 domain_id = p[6] & 0x0F;
            const u8 priority8 = p[7];
            const u16 pattern_id = read_le_u16(p + 8);  // pattern_id16

            out_img.thr_lo[t] = thr_lo;
            out_img.thr_hi[t] = thr_hi;
            out_img.decay16[t] = decay;
            out_img.domain_id[t] = domain_id;
            out_img.priority[t] = priority8;
            out_img.pattern_id[t] = pattern_id;  // store full u16 value
        }

        // routing
        for (std::size_t t = 0; t < kTileCount; ++t) {
            const u8* p = v.tile_routing.data() + t * SZ_TILE_ROUTING_FLAGS16;
            const u16 flags = read_le_u16(p);
            out_img.maskN[t] = (flags & 0x0001u) ? 1 : 0;
            out_img.maskE[t] = (flags & 0x0002u) ? 1 : 0;
            out_img.maskS[t] = (flags & 0x0004u) ? 1 : 0;
            out_img.maskW[t] = (flags & 0x0008u) ? 1 : 0;
            out_img.maskNE[t] = (flags & 0x0010u) ? 1 : 0;
            out_img.maskSE[t] = (flags & 0x0020u) ? 1 : 0;
            out_img.maskSW[t] = (flags & 0x0040u) ? 1 : 0;
            out_img.maskNW[t] = (flags & 0x0080u) ? 1 : 0;
            out_img.bus_r[t] = (flags & 0x0100u) ? 1 : 0;
            out_img.bus_w[t] = (flags & 0x0200u) ? 1 : 0;
        }

        // reset_on_fire_mask16
        for (std::size_t t = 0; t < kTileCount; ++t) {
            out_img.reset_on_fire_mask16[t] = read_le_u16(v.reset_on_fire.data() + t * 2);
        }

        // weights expand: 32B mag (nibbles), 8B sign bits LSB-first
        for (std::size_t t = 0; t < kTileCount; ++t) {
            const u8* base = v.weights_packed.data() + t * SZ_TILE_WEIGHTS_PACKED;
            const u8* mag32 = base;         // 0..31
            const u8* sign8 = base + 32;    // 32..39

            // mags: mag_byte[j] = wmag_{2j} | (wmag_{2j+1} << 4)
            for (std::size_t j = 0; j < 32; ++j) {
                const u8 b = mag32[j];
                const u8 m0 = b & 0x0F;
                const u8 m1 = (b >> 4) & 0x0F;
                out_img.w_mag[t * 64 + (2 * j + 0)] = m0;
                out_img.w_mag[t * 64 + (2 * j + 1)] = m1;
            }

            // signs: 64 bits row-major, LSB-first within each byte
            for (std::size_t k = 0; k < 64; ++k) {
                const std::size_t byte_index = k >> 3;
                const std::size_t bit_index = k & 7;
                const u8 bit = (sign8[byte_index] >> bit_index) & 1u;
                out_img.w_sign[t * 64 + k] = bit;
            }
        }

        return status{};
    }

    // Canonical serializer: simplest implementation writes into out_buf (caller provides capacity).
    status serialize_canonical(const bake_view& view, std::span<u8> out_buf, std::size_t& out_len) noexcept {
        // Minimal: only works if view already validated and contains raw TLV slices.
        // For IDE canonicalization this is enough: we emit TLVs in fixed order and recompute CRC.
        // RISK: if IDE wants to preserve unknown TLVs — contract v0.1 doesn't define them; we reject unknown on parse.

        // compute expected total length upfront:
        auto pad4 = [](std::size_t n) { return (4 - (n & 3u)) & 3u; };
        const std::size_t hdr = 28;

        struct tlv_item { u16 type; std::span<const u8> val; };
        tlv_item items[7];
        std::size_t n = 0;

        auto add = [&](u16 type, std::span<const u8> val) {
            items[n++] = tlv_item{ type,val };
            };

        add(TLV_TOPOLOGY, std::span<const u8>(reinterpret_cast<const u8*>(&view.topo), 0)); // special
        if (view.tile_field_limit != 0) {
            add(TLV_TILE_FIELD_LIMIT, std::span<const u8>(reinterpret_cast<const u8*>(&view.tile_field_limit), 0)); // special
        }
        add(TLV_TILE_PARAMS_V2, view.tile_params);
        add(TLV_TILE_ROUTING_FLAGS16, view.tile_routing);
        add(TLV_READOUT_POLICY, std::span<const u8>(reinterpret_cast<const u8*>(&view.readout), 0)); // special
        add(TLV_RESET_ON_FIRE_MASK16, view.reset_on_fire);
        add(TLV_TILE_WEIGHTS_PACKED, view.weights_packed);

        // size calc (with topology/readout special pack)
        std::size_t size = hdr;
        for (std::size_t i = 0; i < n; ++i) {
            std::size_t vlen = items[i].val.size();
            if (items[i].type == TLV_TOPOLOGY) vlen = LEN_TOPOLOGY;
            if (items[i].type == TLV_READOUT_POLICY) vlen = LEN_READOUT_POLICY;
            if (items[i].type == TLV_TILE_FIELD_LIMIT) vlen = 4;
            size += 8 + vlen + pad4(vlen);
        }
        // CRC TLV
        size += 8 + 4 + pad4(4);

        if (out_buf.size() < size) return fail(status::code::BakeBadLen, u32(size), "Output buffer too small");
        out_len = size;

        // write header
        auto* out = out_buf.data();
        out[0] = u8(kMagic[0]); out[1] = u8(kMagic[1]); out[2] = u8(kMagic[2]); out[3] = u8(kMagic[3]);
        write_le_u16(out + 4, kVerMajor);
        write_le_u16(out + 6, kVerMinor);
        write_le_u32(out + 8, view.header.flags);
        write_le_u32(out + 12, u32(size));
        write_le_u32(out + 16, view.header.bake_id);
        write_le_u32(out + 20, view.header.profile_id);
        write_le_u32(out + 24, 0);

        std::size_t off = hdr;

        auto write_tlv = [&](u16 type, std::span<const u8> val, const u8* special_pack, std::size_t special_len) {
            write_le_u16(out + off + 0, type);
            write_le_u16(out + off + 2, 0);
            const std::size_t vlen = (special_pack ? special_len : val.size());
            write_le_u32(out + off + 4, u32(vlen));
            if (special_pack) {
                for (std::size_t i = 0; i < vlen; ++i) out[off + 8 + i] = special_pack[i];
            }
            else {
                for (std::size_t i = 0; i < vlen; ++i) out[off + 8 + i] = val[i];
            }
            const std::size_t pad = pad4(vlen);
            for (std::size_t i = 0; i < pad; ++i) out[off + 8 + vlen + i] = 0;
            off += 8 + vlen + pad;
            };

        // pack topology (LE) into temp 16 bytes
        u8 topo_pack[16]{};
        write_le_u32(topo_pack + 0, view.topo.tile_count);
        write_le_u16(topo_pack + 4, view.topo.tile_w);
        write_le_u16(topo_pack + 6, view.topo.tile_h);
        topo_pack[8] = view.topo.lanes;
        topo_pack[9] = view.topo.domains;
        write_le_u16(topo_pack + 10, 0);
        write_le_u32(topo_pack + 12, 0);

        u8 tile_limit_pack[4]{};
        write_le_u32(tile_limit_pack, view.tile_field_limit);

        u8 readout_pack[12]{};
        readout_pack[0] = u8(view.readout.mode);
        readout_pack[1] = 0;
        write_le_u16(readout_pack + 2, view.readout.winner_domain_mask);
        write_le_u16(readout_pack + 4, view.readout.settle_ns);
        write_le_u16(readout_pack + 6, 0);
        write_le_u32(readout_pack + 8, 0);

        // write TLVs in canonical order
        write_tlv(TLV_TOPOLOGY, {}, topo_pack, 16);
        if (view.tile_field_limit != 0) {
            write_tlv(TLV_TILE_FIELD_LIMIT, {}, tile_limit_pack, 4);
        }
        write_tlv(TLV_TILE_PARAMS_V2, view.tile_params, nullptr, 0);
        write_tlv(TLV_TILE_ROUTING_FLAGS16, view.tile_routing, nullptr, 0);
        write_tlv(TLV_READOUT_POLICY, {}, readout_pack, 12);
        write_tlv(TLV_RESET_ON_FIRE_MASK16, view.reset_on_fire, nullptr, 0);
        write_tlv(TLV_TILE_WEIGHTS_PACKED, view.weights_packed, nullptr, 0);

        // CRC TLV header offset is current off
        const std::size_t crc_header_off = off;
        const u32 crc = crc32_ieee::compute(out_buf.subspan(0, crc_header_off));
        u8 crc_pack[4]{};
        write_le_u32(crc_pack, crc);
        write_tlv(TLV_CRC32, {}, crc_pack, 4);

        return status{};
    }
    
    // IDE: преобразовать compiled_image в bake_view (для сериализации)
    status compiled_image_to_view(const compiled_image& img, bake_view& out_view, std::vector<u8>& temp_buffers) noexcept {
        using namespace d8;
        
        // Очистить out_view
        out_view = bake_view{};
        
        // Заполнить header
        out_view.header.bake_id = img.bake_id;
        out_view.header.profile_id = img.profile_id;
        out_view.header.flags = img.bake_flags;
        out_view.header.reserved0 = 0;
        
        // Topology
        out_view.topo = img.topo;
        out_view.tile_field_limit = img.tile_field_limit;
        
        // Readout policy
        out_view.readout = img.readout;
        
        // Вычислить размеры буферов
        const std::size_t tile_params_size = kTileCount * SZ_TILE_PARAMS_V2;
        const std::size_t tile_routing_size = kTileCount * SZ_TILE_ROUTING_FLAGS16;
        const std::size_t weights_packed_size = kTileCount * SZ_TILE_WEIGHTS_PACKED;
        const std::size_t reset_on_fire_size = kTileCount * SZ_RESET_ON_FIRE_MASK16;
        
        // Выделить память для всех буферов
        temp_buffers.clear();
        temp_buffers.resize(tile_params_size + tile_routing_size + weights_packed_size +
                           reset_on_fire_size);
        
        u8* tile_params_buf = temp_buffers.data();
        u8* tile_routing_buf = tile_params_buf + tile_params_size;
        u8* weights_packed_buf = tile_routing_buf + tile_routing_size;
        u8* reset_on_fire_buf = weights_packed_buf + weights_packed_size;
        
        // Заполнить TileParamsV2
        // Формат: thr_lo16 (i16), thr_hi16 (i16), decay16 (u16), domain_id4 (u8), priority8 (u8),
        // pattern_id16 (u16), flags8 (u8), reserved (u16)
        for (std::size_t t = 0; t < kTileCount; ++t) {
            u8* p = tile_params_buf + t * SZ_TILE_PARAMS_V2;
            write_le_u16(p + 0, static_cast<u16>(img.thr_lo[t]));
            write_le_u16(p + 2, static_cast<u16>(img.thr_hi[t]));
            write_le_u16(p + 4, img.decay16[t]);
            p[6] = img.domain_id[t] & 0x0F;
            p[7] = img.priority[t];
            write_le_u16(p + 8, static_cast<u16>(img.pattern_id[t]));  // pattern_id16
            p[10] = 0; // flags8
            write_le_u16(p + 12, 0);
        }
        
        // Заполнить TileRoutingFlags16
        for (std::size_t t = 0; t < kTileCount; ++t) {
            u16 flags = 0;
            if (img.maskN[t])  flags |= 0x0001u;
            if (img.maskE[t])  flags |= 0x0002u;
            if (img.maskS[t])  flags |= 0x0004u;
            if (img.maskW[t])  flags |= 0x0008u;
            if (img.maskNE[t]) flags |= 0x0010u;
            if (img.maskSE[t]) flags |= 0x0020u;
            if (img.maskSW[t]) flags |= 0x0040u;
            if (img.maskNW[t]) flags |= 0x0080u;
            if (img.bus_r[t]) flags |= 0x0100u;
            if (img.bus_w[t]) flags |= 0x0200u;
            write_le_u16(tile_routing_buf + t * SZ_TILE_ROUTING_FLAGS16, flags);
        }
        
        // Заполнить Weights (упаковать обратно)
        for (std::size_t t = 0; t < kTileCount; ++t) {
            u8* base = weights_packed_buf + t * SZ_TILE_WEIGHTS_PACKED;
            u8* mag32 = base;         // 0..31
            u8* sign8 = base + 32;    // 32..39
            
            // mags: mag_byte[j] = wmag_{2j} | (wmag_{2j+1} << 4)
            for (std::size_t j = 0; j < 32; ++j) {
                const u8 m0 = img.w_mag[t * 64 + (2 * j + 0)] & 0x0F;
                const u8 m1 = img.w_mag[t * 64 + (2 * j + 1)] & 0x0F;
                mag32[j] = u8(m0 | (m1 << 4));
            }
            
            // signs: 64 bits row-major, LSB-first within each byte
            for (std::size_t k = 0; k < 64; ++k) {
                const std::size_t byte_index = k >> 3;
                const std::size_t bit_index = k & 7;
                const u8 bit = img.w_sign[t * 64 + k] & 1u;
                if (bit) {
                    sign8[byte_index] |= u8(1u << bit_index);
                }
            }
        }
        
        // Заполнить ResetOnFire
        for (std::size_t t = 0; t < kTileCount; ++t) {
            u8* p = reset_on_fire_buf + t * SZ_RESET_ON_FIRE_MASK16;
            write_le_u16(p, img.reset_on_fire_mask16[t]);
        }
        
        // Установить spans в out_view
        out_view.tile_params = std::span<const u8>(tile_params_buf, tile_params_size);
        out_view.tile_routing = std::span<const u8>(tile_routing_buf, tile_routing_size);
        out_view.weights_packed = std::span<const u8>(weights_packed_buf, weights_packed_size);
        out_view.reset_on_fire = std::span<const u8>(reset_on_fire_buf, reset_on_fire_size);
        
        return status{};
    }
    
} // namespace d8::bake
