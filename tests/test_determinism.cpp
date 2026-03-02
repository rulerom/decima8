/*
 * DECIMA-8 Source Code
 * This code is part of Decima-8 Core
 *
 * All rights belong to the ORDEN (c) 2026
 */

#include "d8/swarm.hpp"
#include "bake_gen.hpp"
#include "d8/logger.hpp"
#include <cstring>
#include <array>

static int expect(bool cond, const char* msg) {
	if (!cond) { spdlog::error("EXPECT failed: {}", msg); return 1; }
	return 0;
}

int test_determinism() {
	using namespace d8;

	auto blob = testgen::make_valid_bake_blob();

	std::array<u8, kLanes> ingress{};
	ingress.fill(15);

	swarm a, b;

	auto sa = a.ev_bake(blob);
	if (expect(bool(sa), "a.ev_bake must succeed")) return 1;
	auto sb = b.ev_bake(blob);
	if (expect(bool(sb), "b.ev_bake must succeed")) return 1;

	auto ra = a.ev_flash(123, ingress);
	if (expect(bool(ra.st), "a.ev_flash must succeed")) return 1;

	auto rb = b.ev_flash(123, ingress);
	if (expect(bool(rb.st), "b.ev_flash must succeed")) return 1;

	if (expect(ra.readout16 == rb.readout16, "readout must match")) return 1;
	if (expect(ra.flags32_last == rb.flags32_last, "flags must match")) return 1;

	// Snapshot must be byte-identical
	std::array<u8, sizeof(d8_view_snapshot)> sa_bytes{};
	std::array<u8, sizeof(d8_view_snapshot)> sb_bytes{};
	std::memcpy(sa_bytes.data(), a.view_snapshot_ptr(), sa_bytes.size());
	std::memcpy(sb_bytes.data(), b.view_snapshot_ptr(), sb_bytes.size());

	if (expect(std::memcmp(sa_bytes.data(), sb_bytes.data(), sa_bytes.size()) == 0, "snapshot must match byte-for-byte")) return 1;

	return 0;
}
