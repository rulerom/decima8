/*
 * DECIMA-8 Source Code
 * This code is part of Decima-8 Core
 *
 * All rights belong to the ORDEN (c) 2026
 */

#include "d8/swarm.hpp"
#include "d8/logger.hpp"
#include <vector>

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    d8::init_logger();

    d8::swarm s;

    // Stub: demonstrates in-process calls and snapshot memcpy path.
    std::array<d8::u8, d8::kLanes> ingress{};

    auto r = s.ev_flash(1, ingress);
    if (!r.st) {
        spdlog::info("ev_flash failed (expected in stub without bake): {} msg={}", 
                     static_cast<std::uint32_t>(r.st.c), r.st.msg);
    }

    // Snapshot memcpy (IDE-style)
    std::vector<d8::u8> frame(s.view_snapshot_size_bytes());
    std::memcpy(frame.data(), s.view_snapshot_ptr(), frame.size());
    spdlog::info("Snapshot copied, bytes={}", frame.size());

    spdlog::info("Next step: call ev_bake(valid_blob) then ev_flash() for real frames.");
    return 0;
}
