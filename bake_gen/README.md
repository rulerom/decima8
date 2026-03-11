# DECIMA-8 Bake Generator

Pure C implementation of DECIMA-8 bake file generator.

## Building

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

## Usage

### Test executable
```bash
./bake_gen_test
# Generates test_bake.d8p
```

### Benchmark
```bash
./d8_bake_bench
# Runs performance benchmarks
```

### Library API

```c
#include "bake_gen.h"

uint8_t buffer[240000];
size_t size;

// Generate test bake
int ret = d8_bake_gen_test(buffer, &size);

// Or generate custom bake
ret = d8_bake_gen_custom(
    buffer,
    &size,
    4096,      // tile_count
    0,         // thr_lo
    0,         // thr_hi
    0          // decay16
);
```

## Bake File Format

```
Header (28 bytes):
  - Magic: "D8BK"
  - Version: major, minor
  - Flags
  - Total length
  - Bake ID
  - Profile ID
  - Reserved

TLV_TOPOLOGY (24 bytes):
  - Tile count
  - Tile W, H
  - Lanes, Domains
  - Reserved, Reserved2 (tile_field_limit)

TLV_TILE_PARAMS_V2 (13 bytes per tile):
  - thr_lo, thr_hi
  - decay16
  - domain_id, priority
  - pattern_id
  - flags, reserved

TLV_TILE_ROUTING_FLAGS16 (2 bytes per tile):
  - Routing flags

TLV_TILE_WEIGHTS_PACKED (40 bytes per tile):
  - 64 nibbles (magnitudes)
  - 64 bits (signs)

TLV_RESET_ON_FIRE_MASK16 (2 bytes per tile):
  - Reset masks

TLV_READOUT_POLICY (12 bytes):
  - Mode, masks, timing

TLV_CRC32 (4 bytes):
  - CRC32 of header + TLVs
```

## License

All rights belong to the ORDEN (c) 2026
