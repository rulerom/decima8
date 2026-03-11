# Decima-8 Core (Open Source)

Pure C implementation of Decima-8 neuromorphic core.

## Overview

Decima-8 is a neuromorphic computing core that simulates tile-based neural networks with:
- Up to 4096 tiles (128x32 grid)
- 8-lane VSB (Vector Signal Bus) interface
- 16 domains with winner selection
- Routing masks for inter-tile connections (N/E/S/W/NE/SE/SW/NW)
- BUS read/write for signal propagation
- Fuse-lock mechanism for pattern storage

## Quick Start

### Build

```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

### Run Tests

```bash
cd build
ctest
```

### Run Examples

```bash
cd build

# Generate a custom bake
./d8_example_bake_gen custom.d8p 4096 0 0 0

# Run simple flash example
./d8_example_flash

# Benchmark swarm performance
./d8_swarm_bench tape.vsb bake.d8p

# Benchmark bake generator
./d8_bake_gen_bench
```

## API

### Core Functions

```c
#include "d8/swarm.h"

// Create/destroy swarm
d8_swarm_t* d8_swarm_create(void);
void d8_swarm_destroy(d8_swarm_t* swarm);

// Apply bake
d8_status_t d8_swarm_ev_bake(d8_swarm_t* swarm, const uint8_t* blob, size_t size);

// Run flash cycle
d8_flash_result_t d8_swarm_ev_flash(d8_swarm_t* swarm, uint32_t tag, const uint8_t vsb[8]);

// Get tile parameters
d8_tile_params_t d8_swarm_get_tile_params(const d8_swarm_t* swarm, size_t tile_id);
d8_tile_routing_masks_t d8_swarm_get_tile_routing_masks(const d8_swarm_t* swarm, size_t tile_id);

// Set tile parameters
d8_status_t d8_swarm_set_tile_params(d8_swarm_t* swarm, size_t tile_id,
                                      int16_t thr_lo, int16_t thr_hi, uint16_t decay16,
                                      uint8_t domain_id, uint8_t priority);
```

### Bake Generator

```c
#include "d8/bake_gen.h"

// Generate test bake
int d8_bake_gen_test(uint8_t* buffer, size_t* size);

// Generate custom bake
int d8_bake_gen_custom(uint8_t* buffer, size_t* size,
                       uint32_t tile_count,
                       int16_t thr_lo, int16_t thr_hi, uint16_t decay16);

// Estimate size
size_t d8_bake_gen_estimate_size(uint32_t tile_count);
```

## File Formats

### D8P (Bake File)

Binary format containing:
- Header (28 bytes): Magic "D8BK", version, flags, bake_id, profile_id
- TLV_TOPOLOGY: Tile grid configuration
- TLV_TILE_PARAMS_V2: thr_lo, thr_hi, decay16, domain_id, etc.
- TLV_TILE_ROUTING_FLAGS16: Connection masks
- TLV_TILE_WEIGHTS_PACKED: Synaptic weights (packed nibbles/bits)
- TLV_RESET_ON_FIRE_MASK16: Reset configuration
- TLV_READOUT_POLICY: Output configuration
- TLV_CRC32: Integrity check

### VSB (Tape File)

Raw binary file with 8-byte frames (one byte per lane).

## Project Structure

```
opensource/
├── include/d8/          # Public headers
│   ├── swarm.h
│   ├── swarm_core.h
│   ├── bake.h
│   └── types.h
├── src/                 # Implementation
│   ├── swarm.c
│   ├── swarm_core.c
│   ├── tlv.c
│   └── crc32_ieee.c
├── bake_gen/            # Bake generator
│   ├── bake_gen.h
│   └── bake_gen.c
├── tests/               # Unit tests
│   ├── test_flash.c
│   └── test_bake_gen.c
├── bench/               # Benchmarks
│   ├── swarm_bench.c
│   └── bake_gen_bench.c
├── examples/            # Examples
│   ├── simple_flash.c
│   └── create_bake.c
├── CMakeLists.txt
└── README.md
```

## Integration

### CMake

```cmake
add_subdirectory(decima8-core/opensource)
target_link_libraries(your_target d8_core d8_bake_gen)
```

### Manual

1. Add `include/` to your include paths
2. Compile all `.c` files in `src/` and `bake_gen/`
3. Link with your application

## Performance

Typical performance on modern hardware:
- Flash cycle: 15-40 μs (25,000-65,000 FPS)
- Bake generation: 50-100 MB/s
- Memory usage: ~2 MB for 4096 tiles

## License

Boost Software License - Version 1.0.
(c) Decima-8 Core Team / Orden

## Contact

For questions and contributions, contact the [intent-garden](https://intent-garden.org)
