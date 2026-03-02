# Decima-8 Core (Open Source)

**Decima-8** is a neuromorphic engine implementing the "Island-Swarm" computing concept.

## What's Included in Open Source Release

### ✅ Core (Open Source)

| Component | Description |
|-----------|----------|
| `include/d8/` | Core header files: `swarm_core.hpp`, `bake.hpp`, data types |
| `src/swarm.cpp` | Swarm implementation (tile fabric, fuse-lock, activation) |
| `src/bake.cpp` | Configuration system (Bake Blob, TLV format) |
| `src/crc32_ieee.cpp` | CRC32 for Bake integrity verification |
| `src/hw.rc` | Resources (icons, if any) |
| `tests/` | Core unit tests |
| `examples/` | Usage examples |

### ❌ IDE (Closed Source)

| Component | Description |
|-----------|----------|
| `ide/` | Visual IDE: Accordion, SwarmView, Conductor, TapeDeck |
| `deps/wui/` | GUI library (WUI) |
| `res/` | IDE resources |

## Open Source Package Structure

```
decima8-core/
├── include/
│   └── d8/
│       ├── types.hpp        # Base types (u8, u16, u32, kLanes, kDomains)
│       ├── swarm_core.hpp   # Main API: ev_flash, ev_bake, ev_reset_domain
│       ├── bake.hpp         # Bake Blob structure and generation
│       └── udp/
│           └── packet_v1.hpp # UDP protocol for cascading
├── src/
│   ├── swarm.cpp            # Swarm implementation
│   ├── bake.cpp             # Bake system
│   ├── tlv.cpp              # TLV parser/serializer
│   ├── crc32_ieee.cpp       # CRC32 IEEE
│   └── hw.rc                # Resources
├── tests/
│   ├── test_main.cpp        # Test entry point
│   ├── test_bake.cpp        # Bake system tests
│   ├── test_swarm.cpp       # Swarm tests
│   └── test_determinism.cpp # Determinism tests
├── examples/
│   ├── bake_roundtrip.cpp   # Example: bake generation and verification
│   ├── ide_inprocess.cpp    # Example: IDE integration
│   └── vsb_tape_gen.cpp     # Example: VSB tape generation
├── docs/
│   ├── CONTRACT_v02.md      # Interface specification (Contract)
│   └── API.md               # API documentation
├── CMakeLists.txt           # Build configuration
└── LICENSE                  # License (MIT/Apache 2.0)
```

## Build Requirements

- CMake 3.24+
- C++23 compatible compiler (MSVC 2022, GCC 11+, Clang 14+)
- spdlog (logging)

## Building

### Windows (MSVC)

```bash
mkdir build && cd build
cmake .. -DD8_BUILD_TESTS=ON -DD8_BUILD_EXAMPLES=ON
cmake --build . --config Release
```

### Linux (GCC)

```bash
mkdir build && cd build
cmake .. -DD8_BUILD_TESTS=ON -DD8_BUILD_EXAMPLES=ON -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

## Quick Start

### 1. Create Swarm

```cpp
#include <d8/swarm_core.hpp>

d8::swarm_core core;

// Initialize (16x16 tile fabric = 256 tiles)
core.init(d8::kTileCount);  // kTileCount = 256
```

### 2. Load Bake

```cpp
#include <d8/bake.hpp>

// Generate test Bake Blob
auto bake_blob = d8::ide_utils::generate_test_bake_blob();

// Apply Bake
auto status = core.ev_bake(bake_blob);
if (status) {
    std::cout << "Bake applied: " << status.msg << std::endl;
}
```

### 3. Run Flash (Tick)

```cpp
// Set input vector (8 lanes, Level16 0..15)
std::array<d8::u8, d8::kLanes> ingress = {0, 8, 3, 0, 0, 0, 0, 0};
core.set_vsb_ingress(ingress);

// Execute tick (READ → WRITE)
auto readout = core.ev_flash();

// Read result
std::cout << "BUS16: [";
for (int i = 0; i < 8; ++i) {
    std::cout << (int)readout.bus16[i] << ", ";
}
std::cout << "]" << std::endl;
```

## Contract v0.2

Full specification available in `docs/CONTRACT_v02.md`.

### Key Concepts

- **Tile** — minimal programmable entity (8 inputs, 8 outputs, FUSE-LOCK)
- **Level16** — value 0..15 (4 bits) on each lane
- **READ/WRITE phases** — two-phase protocol (read first, then write)
- **FUSE-LOCK** — tile locks if `thr_cur16 ∈ [thr_lo16..thr_hi16]`
- **Decay-to-Zero** — accumulator pulls toward 0 if `decay16 > 0`
- **BUS16** — shared 8-lane bus, honest summation of contributions

## License

Decima-8 Core is distributed under the **MIT License** (or Apache 2.0 — at author's discretion).

IDE and visual components remain property of ORDEN (c) 2026.

## Contacts

- GitHub: [repository link]
- Documentation: `docs/`
- Contract: `docs/CONTRACT_v02.md`

---

**ORDEN (c) 2026** | Decima-8 Core Team
