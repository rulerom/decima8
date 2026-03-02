/*
 * DECIMA-8 Source Code
 * This code is part of Decima-8 Core
 *
 * All rights belong to the ORDEN (c) 2026
 */

#pragma once

#include <array>
#include <span>
#include <vector>
#include <cstddef>

#include "contract.hpp"
#include "common.hpp"
#include "tlv.hpp"
#include "crc32_ieee.hpp"

namespace d8::bake {

	struct header_v0 {
		char magic[4];
		u16 ver_major;
		u16 ver_minor;
		u32 flags;
		u32 total_len;
		u32 bake_id;
		u32 profile_id;
		u32 reserved0;
	};

	struct topology_v0 {
		u32 tile_count;
		u16 tile_w;
		u16 tile_h;
		u8  lanes;
		u8  domains;
		u16 reserved;
		u32 reserved2;
	};

	struct readout_policy_v0 {
		readout_mode mode;
		u8  reserved0;
		u16 winner_domain_mask;
		u16 settle_ns;
		u16 reserved1;
		u32 reserved2;
	};

	struct bake_view {
		header_v0 header{};
		topology_v0 topo{};
		readout_policy_v0 readout{};
		u32 tile_field_limit{};

		// TLV raw slices (point into original blob)
		std::span<const u8> tile_params;
		std::span<const u8> tile_routing;
		std::span<const u8> weights_packed;
		std::span<const u8> reset_on_fire;

		u32 crc32_stored{};
		std::size_t crc_tlv_header_off{}; // start offset of TLV_CRC32 header
	};

	struct compiled_image {
		// baked constants per tile (flat arrays for cache)
		std::array<i16, kTileCount> thr_lo{};
		std::array<i16, kTileCount> thr_hi{};
		std::array<u16, kTileCount> decay16{};
		std::array<u8, kTileCount> domain_id{};
		std::array<u8, kTileCount> priority{};
		std::array<u16, kTileCount> pattern_id{}; // Pattern ID output (0..32767), stored in bytes 8-9 of TileParams
		std::array<u16, kTileCount> reset_on_fire_mask16{};

		std::array<u8, kTileCount> maskN{};
		std::array<u8, kTileCount> maskE{};
		std::array<u8, kTileCount> maskS{};
		std::array<u8, kTileCount> maskW{};
		std::array<u8, kTileCount> maskNE{};  // Diagonal: North-East
		std::array<u8, kTileCount> maskSE{};  // Diagonal: South-East
		std::array<u8, kTileCount> maskSW{};  // Diagonal: South-West
		std::array<u8, kTileCount> maskNW{};  // Diagonal: North-West
		std::array<u8, kTileCount> bus_w{};  // BUS_W flag (PHASE_WRITE)
		std::array<u8, kTileCount> bus_r{};  // BUS_R flag (ACTIVE seed)

		readout_policy_v0 readout{};
		u32 tile_field_limit{};

		// weights expanded for speed: 64 per tile
		std::array<u8, kTileCount * 64> w_mag{};  // 0..15
		std::array<u8, kTileCount * 64> w_sign{}; // 0/1 (1=plus, 0=minus)

		// topology (should match expected)
		topology_v0 topo{};
		u32 bake_id{};
		u32 profile_id{};
        u32 bake_flags{};
	};

	status parse_view(std::span<const u8> blob, bake_view& out) noexcept;
	status validate(std::span<const u8> blob, bake_view* out_view = nullptr) noexcept;
	status compile(std::span<const u8> blob, compiled_image& out_img) noexcept;

	// Canonical serialization: deterministic TLV ordering + CRC recalculation.
	// Allocations are allowed here (IDE tooling), runtime will not call it.
	status serialize_canonical(const bake_view& view, std::span<u8> out_buf, std::size_t& out_len) noexcept;

	// IDE: convert compiled_image to bake_view (for serialization)
	// Allocates memory for temporary buffers
	status compiled_image_to_view(const compiled_image& img, bake_view& out_view, std::vector<u8>& temp_buffers) noexcept;

	// IDE: layer_id_effective removed in v0.2 (activation graph allows cycles)

} // namespace d8::bake
