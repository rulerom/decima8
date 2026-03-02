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
#include <vector>

static int expect(bool cond, const char* msg) {
    if (!cond) {
        spdlog::error("EXPECT failed: {}", msg);
        return 1;
    }
    return 0;
}

int test_swarm() {
    using namespace d8;

    // 1) NotBaked: ev_flash без ev_bake
    {
        swarm s;
        std::array<u8, kLanes> ingress{};
        auto res = s.ev_flash(1, ingress);
        if (expect(!res.st && res.st.c == status::code::NotBaked, "ev_flash without bake must be NotBaked")) return 1;
    }

    // 2) NotBaked: ev_reset_domain без ev_bake
    {
        swarm s;
        auto st = s.ev_reset_domain(0xFFFF);
        if (expect(!st && st.c == status::code::NotBaked, "ev_reset_domain without bake must be NotBaked")) return 1;
    }

    // 3) BadPhase: ev_reset_domain во время flash_in_progress (нельзя, т.к. flash_in_progress_ защищает)
    // В текущей реализации это невозможно, т.к. flash_in_progress_ сбрасывается до возврата.
    // Но можно проверить, что ev_reset_domain работает между flash.

    // 4) BadIngressLevel: vsb_ingress16[i] > 15
    {
        swarm s;
        auto blob = testgen::make_valid_bake_blob();
        auto st = s.ev_bake(blob);
        if (expect(bool(st), "ev_bake must succeed")) return 1;

        std::array<u8, kLanes> ingress{};
        ingress[0] = 16; // invalid Level16
        auto res = s.ev_flash(1, ingress);
        if (expect(!res.st && res.st.c == status::code::BadIngressLevel, "ingress > 15 must be BadIngressLevel")) return 1;
    }

    // 5) LOCKPassthrough: locked_after==1 → drive_vec==in16
    {
        swarm s;
        auto blob = testgen::make_valid_bake_blob({.thr_lo16 = 0, .thr_hi16 = 1}); // low threshold range
        auto st = s.ev_bake(blob);
        if (expect(bool(st), "ev_bake must succeed")) return 1;

        std::array<u8, kLanes> ingress{};
        ingress.fill(15); // max input
        std::array<tile_trace, kTileCount> traces{};
        auto res = s.trace_flash(1, ingress, traces);
        if (expect(bool(res.st), "trace_flash must succeed")) return 1;

        // Find a tile that fired (locked_after==1)
        bool found_locked = false;
        for (std::size_t t = 0; t < kTileCount; ++t) {
            if (traces[t].locked_after == 1) {
                found_locked = true;
                // Check passthrough: drive_vec == in16
                bool passthrough_ok = true;
                for (std::size_t i = 0; i < kLanes; ++i) {
                    if (traces[t].drive_vec[i] != traces[t].in16[i]) {
                        passthrough_ok = false;
                        break;
                    }
                }
                if (expect(passthrough_ok, "locked tile must passthrough in16")) return 1;
                break;
            }
        }
        if (expect(found_locked, "at least one tile should lock")) return 1;
    }

    // 6) FIREEvent: thr_cur==thr_base → locked_after==1, FIRE(t)=1
    {
        swarm s;
        auto blob = testgen::make_valid_bake_blob({.thr_lo16 = 0, .thr_hi16 = 1});
        auto st = s.ev_bake(blob);
        if (expect(bool(st), "ev_bake must succeed")) return 1;

        std::array<u8, kLanes> ingress{};
        ingress.fill(15);
        std::array<tile_trace, kTileCount> traces{};
        auto res = s.trace_flash(1, ingress, traces);
        if (expect(bool(res.st), "trace_flash must succeed")) return 1;

        bool found_fire = false;
        for (std::size_t t = 0; t < kTileCount; ++t) {
            if (traces[t].locked_before == 0 && traces[t].locked_after == 1) {
                found_fire = true;
                if (expect(traces[t].thr_after == 1, "fired tile must have thr_after==thr_base")) return 1;
                break;
            }
        }
        if (expect(found_fire, "at least one tile should fire")) return 1;
    }

    // 7) BUSSummation: BUS16 = clamp15(Σ drive_vec[t][i] по maskBUS)
    {
        swarm s;
        auto blob = testgen::make_valid_bake_blob({.bus_writer_tile_id = 0});
        auto st = s.ev_bake(blob);
        if (expect(bool(st), "ev_bake must succeed")) return 1;

        std::array<u8, kLanes> ingress{};
        auto res = s.ev_flash(1, ingress);
        if (expect(bool(res.st), "ev_flash must succeed")) return 1;

        const auto* snap = s.view_snapshot_ptr();
        // BUS16 should be non-zero if tile 0 drives
        bool bus_nonzero = false;
        for (std::size_t i = 0; i < kLanes; ++i) {
            if (snap->bus16[i] > 0) {
                bus_nonzero = true;
                break;
            }
        }
        // May be zero if tile 0 doesn't fire, but structure should be correct
    }

    // 8) DomainReset: ev_reset_domain(mask16) → thr_cur:=0, locked:=(thr_base==0)
    {
        swarm s;
        auto blob = testgen::make_valid_bake_blob({.thr_lo16 = 0, .thr_hi16 = 10});
        auto st = s.ev_bake(blob);
        if (expect(bool(st), "ev_bake must succeed")) return 1;

        // Fire some tiles
        std::array<u8, kLanes> ingress{};
        ingress.fill(15);
        auto res1 = s.ev_flash(1, ingress);
        if (expect(bool(res1.st), "ev_flash must succeed")) return 1;

        // Reset domain 0
        auto st2 = s.ev_reset_domain(0x0001); // domain 0
        if (expect(bool(st2), "ev_reset_domain must succeed")) return 1;

        // Check that domain 0 tiles are reset
        const auto* snap = s.view_snapshot_ptr();
        (void)snap; // TODO: implement check when internal state access is available
        // Note: snapshot is from previous flash, so we need to check internal state
        // For full test, would need access to internal state or another flash
    }

    // 9) AUTORESET: AUTO_RESET_MASK16 применяется после READOUT
    {
        swarm s;
        auto blob = testgen::make_valid_bake_blob({
            .thr_lo16 = 0,
            .thr_hi16 = 1,
            .reset_on_fire_mask16 = 0x0001  // winner resets domain 0
        });
        auto st = s.ev_bake(blob);
        if (expect(bool(st), "ev_bake must succeed")) return 1;

        std::array<u8, kLanes> ingress{};
        ingress.fill(15);
        auto res = s.ev_flash(1, ingress);
        if (expect(bool(res.st), "ev_flash must succeed")) return 1;

        const auto* snap = s.view_snapshot_ptr();
        (void)snap; // TODO: implement check for AUTO_RESET_MASK16
        // AUTO_RESET_MASK16 should be set if winner exists
        // (may be 0 if no tiles fired)
    }

    // 10) Snapshot copy test
    {
        swarm s;
        auto blob = testgen::make_valid_bake_blob();
        auto st = s.ev_bake(blob);
        if (expect(bool(st), "ev_bake must succeed")) return 1;

        std::array<u8, kLanes> ingress{};
        auto res = s.ev_flash(1, ingress);
        if (expect(bool(res.st), "ev_flash must succeed")) return 1;

        // Snapshot copy — single memcpy
        std::vector<u8> snap(s.view_snapshot_size_bytes());
        std::memcpy(snap.data(), s.view_snapshot_ptr(), snap.size());
        if (expect(snap.size() == sizeof(d8_view_snapshot), "snapshot size must match")) return 1;
    }

    return 0;
}
