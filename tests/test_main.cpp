/*
 * DECIMA-8 Source Code
 * This code is part of Decima-8 Core
 *
 * All rights belong to the ORDEN (c) 2026
 */

#include "d8/logger.hpp"

int test_bake();
int test_swarm();
int test_determinism();
int test_swarm_basic();

int main() {
    d8::init_logger();
    
    int fails = 0;

    auto run = [&](const char* name, int (*fn)()) {
        const int rc = fn();
        if (rc == 0) {
            spdlog::info("[PASS] {}", name);
        }
        else {
            spdlog::error("[FAIL] {} (rc={})", name, rc);
            ++fails;
        }
        };

    run("test_bake", &test_bake);
    run("test_swarm", &test_swarm);
    run("test_determinism", &test_determinism);
    run("test_swarm_basic", &test_swarm_basic);

    if (fails == 0) {
        spdlog::info("All tests passed.");
        return 0;
    }
    spdlog::error("{} test(s) failed.", fails);
    return 1;
}
