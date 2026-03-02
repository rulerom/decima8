DECIMA-8 Resonance Swarm Contract v0.2 (Activation Graph + Range Fuse + Decay-to-Zero)
Codename: Siberian Tank Interface

Status: v0.2 DESIGN FREEZE

Owner: Conductor (Digital Island) ↔ Swarm/Island (Analog Core)

---

0) Scope / Non-Goals

### Scope (v0.2)

- One deterministic rhythm for: Emulator → Proto (PCB) → FPGA → ASIC.
- Level16: 0..15 on each of 8 data lanes.
- Bidirectional VSB exchange:
  - Conductor sets input vector **before READ** and holds stable during READ aperture.
  - Island drives VSB only in WRITE (readout after WRITE).
- **Tile = minimal programmable entity** (RuleROM addresses tiles).
- **All data transferred only through common BUS16** (8 lane). Neighbors do not transfer data.
- Neighbors form **activation graph (relay)**: locked tile activates descendants for BUS reading.
- **Range-based FUSE (LOCK):** tile latches only if thr_cur16 ∈ [thr_lo16..thr_hi16] (both bounds are i16).
- **Decay pulls to 0:** if decay16>0, accumulator decreases by module toward zero each tick and **never jumps over 0**.
- **Branch collapse:** if tile becomes inactive (not ACTIVE), it forcibly resets to thr_cur16=0, locked=0, and does not participate in computation/drive.
- Bake applied atomically only between EV_FLASH.
- No MT introduced: one swarm = one thread/process.

### Non-Goals (v0.2)

- Absolute voltages not fixed; Level16 semantics + rhythm are fixed.
- No baked parameter changes inside EV_FLASH.
- Compatibility with old v0.1 format not required.

---

1) Terms

- **Conductor** — digital conductor (CPU), prepares input, triggers EV_FLASH, performs bake/reset.
- **Island/Swarm** — tile network + common BUS16 (8 lane) over VSB.
- **Tile** — fabric node: 8 input lanes, 8 output lanes (to BUS), local FUSE (thr/lock), weights, routing_flags16.
- **Level16** — value 0..15 (4 bits), semantics of "level/energy".
- **READ phase** — tiles sample input (VSB_INGRESS + opt. BUS16), update runtime, do NOT drive BUS.
- **WRITE phase** — Conductor stops driving bus, Island drives BUS16; tiles with BUS_W and locked-ancestor (or locked self) write, input not read.
- **Domain** — 0..15; group of tiles for thr_cur16/locked reset.
- **RESET_DOMAIN(mask16)** — domain reset (via EV_RESET_DOMAIN or auto-reset).
- **BAKE_APPLIED** — bool; 0 after power-on/reset, becomes 1 after successful EV_BAKE().
- **RoutingFlags16** — 16-bit routing/activation flags field (see section 8).
- **Parents(t)** — set of parent tiles having activation edge toward t (see section 8).
- **ACTIVE(t)** — tile "in live chain": can read BUS16, compute, apply decay.

---

2) Hard Constants (v0.2 freeze)

- VSB: 8 data lanes VSB[0..7], each carries Level16.
- BUS16: 8 lane, contribution summing in WRITE.
- Domains: exactly 16 domains (0..15).
- Phase discipline: READ/WRITE mandatory; tile cannot read and write simultaneously in one phase.
- Routing: tile has **RoutingFlags16** (10 used bits):

  N, E, S, W, NE, SE, SW, NW, BUS_R, BUS_W — 1 bit each.
- **Always work on all 8 lanes.** No per-lane masks.
- Data not sent to neighbors. Neighbors only form activation graph for BUS read permission.

---

3) Interface Planes

### 3.1 Data Plane: VSB[0..7]
Each lane carries Level16; data from previous tick available only via activation graph (BUS_R), not through arithmetic summing.

### 3.2 Rhythm Plane: READ/WRITE (two-phase guard)

- READ: all tiles sample input and update state.
- WRITE: tiles drive outputs to BUS16.
- Turnaround (direction gap) mandatory between them so Conductor releases VSB and Island can drive it.
- Conductor reads BUS16 only after WRITE completes.

### 3.3 Config Plane: CFG (SPI-like)
CFG_CS, CFG_SCLK, CFG_MOSI, CFG_MISO

Via CFG:

- Load BakeBlob to staging
- Read FLAGS
- Command EV_RESET_DOMAIN(mask16)
- (Optional) Read BAKE_ID_ACTIVE / PROFILE_ID_ACTIVE

---

4) Event Protocol / SHM ABI

### 4.1 External Events (API)

- **EV_FLASH(tag_u32)**

  - Executes one deterministic READ→WRITE cycle.
  - Returns readout (R0/R1), FLAGS read separately (CFG) or in return struct.
  - Allowed only if BAKE_APPLIED==1, otherwise NotBaked (state unchanged).

- **EV_RESET_DOMAIN(mask16)**

  - Only between EV_FLASH.
  - Only if BAKE_APPLIED==1, otherwise NotBaked.

- **EV_BAKE()**

  - Only between EV_FLASH.
  - Applies staging BakeBlob atomically.
  - On success, resets runtime (see 6.3).
  - On error, nothing changes.

### 4.2 Internal EV_FLASH Sub-phases (not external API)

1. PHASE_READ
2. TURNAROUND
3. PHASE_WRITE
4. READOUT_SAMPLE
5. INTERPHASE_AUTORESET (optional, per section 15)

### 4.3 Readout Timing

Default R0_RAW_BUS:

- Conductor reads BUS16[0..7] immediately after PHASE_WRITE completes in same EV_FLASH.
- In SHM: EV_FLASH fills OUT_buf, Conductor reads after return.

### 4.4 Bake Transaction / CFG Staging

Staging buffer in Digital Island. EV_BAKE applies parameters to fabric.

EV_BAKE errors (minimum):

OK, BakeBadMagic, BakeBadVersion, BakeBadLen, BakeMissingTLV, BakeBadTLVLen, BakeCRCFail, BakeReservedNonZero, TopologyMismatch, BakeNoBlob.

---

5) Canonical Tick (one "flash")

### 5.1 Setup (Conductor)

- Conductor sets VSB_INGRESS16[0..7] (Level16).
- Holds stable until end of READ aperture.

### 5.2 PHASE_READ (Island)

At READ start:

- Fix locked_before[t] = locked[t] for all tiles.
- Compute ACTIVE[t] as **activation graph closure** (see 6.1).
- If ACTIVE[t]==0 → tile forcibly zeroed (see 6.1), does not compute or drive.

For ACTIVE[t]==1:

- Form in16 (VSB_INGRESS) per 6.1.
- If locked_before==0: compute row-pipeline (6.5), update thr_cur16 with decay (6.6), determine locked_after.
- If locked_before==1: tile stays locked, matrix/decay not applied.
- Select drive_vec (6.8).

### 5.3 TURNAROUND

- Conductor removes VSB drive (Hi-Z / no-drive).
- Island enables drive only in WRITE.

### 5.4 PHASE_WRITE (Island)

- Tile writes to BUS16 only if BUS_W==1 and (locked self or locked ancestor).
- Writes **entire** drive_vec[0..7] (all 8 lanes), then "honest summing" (section 13).

### 5.5 READOUT_SAMPLE (Conductor)

- R0_RAW_BUS: readout = BUS16[0..7] as 8×Level16.

### 5.6 INTERPHASE_AUTORESET (optional)

- After readout and FLAGS32_LAST latched, apply AutoReset-by-Fire (section 15).

---

6) Tile Model v0.2

### 6.1 ACTIVE + Tile Input (relay + instant branch collapse)

**Routing edges:** directions N/E/S/W/NE/SE/SW/NW form edge A→B if A has flag set and neighbor B exists (see 8).

**Parents(t):** all p having edge p→t.

**ACTIVE closure (canonical):**

Seed: `ACTIVE[t]=1` if `BUS_R==1` (source/root of chain).

Propagate: `ACTIVE[t]=1` if exists `p∈Parents(t)` such that `ACTIVE[p]==1` and `locked_before[p]==1`.

This computed as monotonic closure until stabilization (least fixed point). Determinism ensured by using only locked_before.

**Tile input and forced zero:**

If `ACTIVE[t]==0`, tile considered "dead zone":
```
thr_cur16 := 0
locked := 0
drive_vec := {0..0}
weights/row/decay not applied this tick
```

If `ACTIVE[t]==1`, tile reads only VSB_INGRESS16 (all 8 lanes):
```
for i in 0..7:
  in16[t][i] = clamp15(VSB_INGRESS16[i])
  IN_CLIP[t][i] = (VSB_INGRESS16[i] > 15)
```

**Important:** BUS16 reflects state formed in previous EV_FLASH, not summed with VSB in current tick. Only role of BUS16 in READ phase is semantic: tiles with BUS_R flag become activation graph sources (ACTIVE seed). Data from bus does not participate in in16 computation.

**Relay:** Signal propagates from ancestor to descendant in 2 ticks:
- Tick N: ancestor fuses → drives bus in PHASE_WRITE
- Tick N+1: descendant activates via BUS_R → reads VSB_INGRESS → computes

### 6.2 Baked Tile State (v0.2)

**Baked:**

- thr_lo16 (i16)
- thr_hi16 (i16) (invariant: thr_lo16 <= thr_hi16)
- decay16 (u16) (0..32767)
- domain_id4 (0..15)
- priority8 (0..255)
- pattern_id16 (0..32767)
- routing_flags16 (u16) (see 8)
- W[8][8] — SignedWeight5 (mag3∈[0..7]+sign1)
- reset_on_fire_mask16 (u16)

**Runtime:**

- thr_cur16 (i16: -32768..+32767)
- locked (0/1)

### 6.3 Reset Semantics (between flashes)

EV_RESET_DOMAIN(mask16) applied only between EV_FLASH.

Auto-reset applied in INTERPHASE_AUTORESET.

If tile's domain falls in mask16:

- thr_cur16 := 0
- locked := 0

> Relay: if root/intermediate tile unfused (reset/collide), next EV_FLASH it won't give locked_before==1, downstream becomes ACTIVE==0 → forcibly zeroed.

### 6.4 Canonical Functions (SignedWeight5)

Weight: mag3∈[0..7], sign1∈{0,1} (1="+", 0="−").

Canonical signed multiplication (no division):

```
mul_signed_raw(a, mag, sign) = (sign ? +1 : -1) * (a * mag)
where a∈[0..15], mag∈[0..7] → [-105..+105] per term
8 terms per row → [-840..+840] per row
```

clamp15(x) = clamp_range(x, 0, 15).

### 6.5 RowOut Pipeline in PHASE_READ (for ACTIVE && !locked_before)

For each row r=0..7:

```
row_raw_signed[r] = Σ_{i=0..7} (in16[i] * Wmag[r][i] * sign)  → range [-840..+840]
```

For lines/drive (no negatives):

```
row16_out[r] = clamp15((max(row_raw_signed[r], 0) + 7) / 8) → 0..15
```

For accumulator (signed, no clamp):

```
row16_signed[r] = row_raw_signed[r] → range [-840..+840]
```

### 6.6 Accumulator + Decay-to-Zero + Fuse-by-Range

locked_before = locked (snapshot at READ start).

Rules apply **only if ACTIVE==1**.

**Decay always applied** (if decay16>0), even on locked tiles.

If locked_before==0:

1. delta_raw = Σ_{r=0..7} row16_signed[r] → [-6720..+6720]
2. thr_tmp = thr_cur16 + delta_raw (in i32)
3. **Decay pulls to 0, doesn't jump over 0:**

```
if (decay16 > 0) {
  if (thr_tmp > 0) thr_tmp = max(thr_tmp - decay16, 0)
  else if (thr_tmp < 0) thr_tmp = min(thr_tmp + decay16, 0)
}
thr_cur16 = (i16)clamp_range(thr_tmp, -32768, 32767)
```

4. **Fuse by range:**

```
range_active = (thr_lo16 < thr_hi16)
in_range = range_active && (thr_lo16 <= thr_cur16) && (thr_cur16 <= thr_hi16)

has_signal = (delta_raw != 0)
entered_by_decay = (decay16 > 0) && (in_range == true) && (in_range_before_decay == false)
locked_after = (BAKE_APPLIED==1) && in_range && (has_signal || entered_by_decay)
```

Note: `thr_lo16 == thr_hi16` means disabled fuse (including base case `0..0`) — such tile does not latch.

If locked_before==1:

- locked_after := 1
- Weights not applied (passthrough)
- **Decay applied** (if decay16>0, thr_cur16 pulls to 0):

```
if (decay16 > 0) {
  if (thr_cur16 > 0) thr_cur16 = max(thr_cur16 - decay16, 0)
  else if (thr_cur16 < 0) thr_cur16 = min(thr_cur16 + decay16, 0)
}
```

**Events:**

- LOCK_TRANSITION(t) = (locked_before==0 && locked_after==1)
- FIRE(t) = LOCK_TRANSITION(t)
- locked := locked_after

**Invariant v0.2:**

- locked==1 ⇒ thr_lo16 <= thr_cur16 <= thr_hi16

### 6.7 FUSE-LOCK Passthrough (mandatory)

If locked_after==1, tile acts as "copper bridge":

- Matrix W not applied.
- drive_vec[i] = in16[i] for all i=0..7 (passthrough).
- **Decay applied** (if decay16>0, thr_cur16 pulls to 0).

### 6.8 Drive Selection (WRITE)

At end of READ:

- If locked_after==1: drive_vec[i] = in16[i]
- Else: drive_vec[i] = row16_out[i]

---

7) RoutingFlags16: Bit Map

LSB-first:

| Bit | Flag | Description |
|-----|------|-------------|
| bit0 | N | North |
| bit1 | E | East |
| bit2 | S | South |
| bit3 | W | West |
| bit4 | NE | Northeast |
| bit5 | SE | Southeast |
| bit6 | SW | Southwest |
| bit7 | NW | Northwest |
| bit8 | BUS_R | Read bus (ACTIVE source) |
| bit9 | BUS_W | Write to bus (WRITE phase) |
| bit10..15 | reserved | Must be 0 |

---

8) Activation Graph (Neighbors) and Coordinates

### 8.1 Neighbors by Topology

tile_id = y * tile_w + x

**Cardinal:**

- N(x,y) = (x, y-1) if y > 0
- S(x,y) = (x, y+1) if y < tile_h-1
- W(x,y) = (x-1, y) if x > 0
- E(x,y) = (x+1, y) if x < tile_w-1

**Diagonals:**

- NE(x,y) = (x+1, y-1) if x < tile_w-1 and y > 0
- SE(x,y) = (x+1, y+1) if x < tile_w-1 and y < tile_h-1
- SW(x,y) = (x-1, y+1) if x > 0 and y < tile_h-1
- NW(x,y) = (x-1, y-1) if x > 0 and y > 0

### 8.2 Activation Edges

If tile A has Dir flag set and neighbor B = neighbor(A, Dir) exists → edge A→B.

This affects **only** ACTIVE computation (6.1). Data not transferred.

Multi-parents allowed. Cycles allowed (determinism via locked_before and least-fixed-point ACTIVE).

---

9) BUS Semantics: Honest Summing + CLIP/OVF

In PHASE_WRITE for each lane i=0..7:

```
contrib_from_all_tiles[i] =
  Σ_{t | (routing_flags16[t] & BUS_W)!=0 && (locked self || locked_ancestor)} drive_vec[t][i]

bus_raw[i]  = contrib_from_all_tiles[i]
BUS16[i]    = clamp15(bus_raw[i])
BUS_CLIP[i] = (bus_raw[i] > 15)
BUS_CLIP_ANY = OR_i BUS_CLIP[i]
```

OVF:

- PHASE_WRITE (BUS_CLIP)
- BUS_OVF_ANY = BUS_CLIP_ANY
- OVF_ANY = BUS_OVF_ANY

Policy v0.2: only saturate (clamp15), no wrap/divide.

---

10) COLLIDE: Domains and Winner (as v0.1, no DAG dependencies)

Definitions in current tick:

- FIRE(t) = (locked_before[t]==0 && locked_after[t]==1)
- FIRED_SET(d) = { t | domain_id(t)=d && FIRE(t)=1 }
- cnt(d) = |FIRED_SET(d)|

**Rules:**

- cnt(d)=0 → no winner, COLLIDE(d)=0
- cnt(d)=1 → single winner, COLLIDE(d)=0
- cnt(d)≥2 → COLLIDE(d)=1 and winner selected:

  1. max priority8
  2. on tie min tile_id

```
winner(d) = argmax_{t∈FIRED_SET(d)} (priority8(t), -tile_id(t))
```

---

11) AutoReset-by-Fire (inter-phase domain reset) — v0.2

(optional; if not needed, can be omitted entirely)

Each tile has baked reset_on_fire_mask16[t].

**Auto-reset mask:**

```
AUTO_RESET_MASK16 =
  OR_{d | cnt(d)>0} reset_on_fire_mask16[winner(d)]
```

**Application** strictly after READOUT_SAMPLE of current EV_FLASH:

```
apply_reset_domain(AUTO_RESET_MASK16)
```

Effect of apply_reset_domain — as 6.3 for domains in mask, **except** resetting tile and its entire ancestor chain (they excluded from reset).

**Guarantee:** winner always has time for locked/drive in current tick (reset only "between flashes").

---

12) READOUT_POLICY v0.2

Default: R0_RAW_BUS — readout = BUS16[0..7] after WRITE.

Optional R1_DOMAIN_WINNER_ID32 possible but requires discipline "only winner drives ID", otherwise sum destroys ID. (If needed, can add exact canon, not critical now.)

---

13) Bake Binary TLV Spec v0.2 (incompatible with v0.1)

### 13.1 General Rules

- Little-endian.
- TLV padding = 0, value alignment to 4-byte boundary.
- CRC32 IEEE (zlib/crc32) over all bytes from offset 0 to start of TLV_CRC32 (TLV_CRC32 header+value not included in CRC).

### 13.2 Header (28 bytes)

**BakeBlobHeader:**

| Offset | Field | Type | Value |
|--------|-------|------|-------|
| 0 | magic | char[4] | "D8BK" |
| 4 | ver_major | u16 | 2 |
| 6 | ver_minor | u16 | 0 |
| 8 | flags | u32 | BAKE_FLAG_DOUBLE_STRAIT (bit0) |
| 12 | total_len | u32 | — |
| 16 | bake_id | u32 | — |
| 20 | profile_id | u32 | — |
| 24 | reserved0 | u32 | 0 |

**Header flags:**

- bit0 (BAKE_FLAG_DOUBLE_STRAIT): conductor does double pour on each incoming chord; in this mode, do not output decisions on first pour.

### 13.3 TLV Header (8 bytes)

| Offset | Field | Type |
|--------|-------|------|
| 0 | type | u16 |
| 2 | tflags | u16 |
| 4 | len | u32 |

- value[len] + padding

### 13.4 TLV Type Map v0.2

| TLV Type | ID |
|----------|-----|
| TLV_TOPOLOGY | 0x0100 |
| TLV_TILE_PARAMS_V2 | 0x0121 |
| TLV_TILE_ROUTING_FLAGS16 | 0x0131 |
| TLV_READOUT_POLICY | 0x0140 |
| TLV_RESET_ON_FIRE_MASK16 | 0x0150 |
| TLV_TILE_WEIGHTS_PACKED | 0x0160 |
| TLV_TILE_FIELD_LIMIT | 0x0170 |
| TLV_CRC32 | 0xFFFE (last) |

### 13.5 Required TLV

All from list above required, except AutoReset can be zero masks.

---

14) TLV Structures (canon v0.2)

### 14.1 TLV_TOPOLOGY (0x0100), len=16

**TopologyV0:**

| Field | Type |
|-------|------|
| tile_count | u32 |
| tile_w | u16 |
| tile_h | u16 |
| lanes | u8 (=8) |
| domains | u8 (=16) |
| reserved | u16 (=0) |
| reserved2 | u32 (=0) |

### 14.2 TLV_TILE_PARAMS_V2 (0x0121), len = tile_count × 13

**TileParamsV2 (13 bytes per tile):**

| Field | Size |
|-------|------|
| thr_lo16 | i16 (bytes 0-1) |
| thr_hi16 | i16 (bytes 2-3) |
| decay16 | u16 (bytes 4-5) |
| domain_id4 | u8 (low nibble, high nibble reserved=0) (byte 6) |
| priority8 | u8 (byte 7) |
| pattern_id16 | u16 (bytes 8-9) |
| flags8 | u8 (=0 reserved) (byte 10) |
| reserved | u16 (=0) (bytes 11-12) |

### 14.3 TLV_TILE_ROUTING_FLAGS16 (0x0131), len = tile_count × 2

Per tile:

| Field | Type |
|-------|------|
| routing_flags16 | u16 (LE) |

Reserved bits 10..15 must be 0.

### 14.4 TLV_TILE_WEIGHTS_PACKED (0x0160), len = tile_count × 40

As before:

- 32 bytes: magnitudes Wmag[8][8] (64 × mag3∈[0..7], 4 bits each, high bit=0)
- 8 bytes: sign bits Wsign[8][8] (64 bits), LSB-first

### 14.5 TLV_RESET_ON_FIRE_MASK16 (0x0150), len = tile_count × 2

Per tile: reset_mask16 u16

### 14.6 TLV_READOUT_POLICY (0x0140), len=12

**ReadoutPolicyV0:**

| Field | Type |
|-------|------|
| mode | u8 (0=R0_RAW_BUS, 1=R1_DOMAIN_WINNER_ID32) |
| reserved0 | u8 (=0) |
| winner_domain_mask | u16 (if mode=1) |
| settle_ns | u16 (recommendation for hardware; emulator may ignore) |
| reserved1 | u16 (=0) |
| reserved2 | u32 (=0) |

### 14.7 TLV_CRC32 (0xFFFE), len=4

| Field | Type |
|-------|------|
| crc32 | u32 |

### 14.8 TLV_TILE_FIELD_LIMIT (0x0170), len=4

| Field | Type |
|-------|------|
| tile_field_limit | u32 (0 = full kTileCount) |

---

15) Load-time Validation (mandatory)

Bake loader must:

1. Check magic/ver/total_len
2. Verify all required TLVs present
3. Strict len:
   - TILE_PARAMS_V2: tile_count × 13
   - TILE_ROUTING_FLAGS16: tile_count × 2
   - TILE_WEIGHTS_PACKED: tile_count × 40
   - RESET_ON_FIRE_MASK16: tile_count × 2
4. CRC32 by rule "before TLV_CRC32"
5. Reserved fields = 0; flags8==0; routing reserved bits 10..15 == 0; header.flags may contain only BAKE_FLAG_DOUBLE_STRAIT
6. thr_lo16 <= thr_hi16 for each tile
7. tile_count == tile_w × tile_h

---

16) Runtime FLAGS (minimum)

Island returns FLAGS32 (minimum):

| Bit | Flag |
|-----|------|
| bit0 | READY_LAST |
| bit1 | OVF_ANY_LAST |
| bit2 | COLLIDE_ANY_LAST |

(masks OVF/COLLIDE optional)

---

17) MUST for Emulator (to match hardware)

1. EV_FLASH always performs READ→WRITE, double buffering (cannot read what you write).
2. EV_RESET_DOMAIN and EV_BAKE only between EV_FLASH.
3. ACTIVE closure — **least fixed point** by locked_before (6.1). This ensures "branch collapse" without extra half-live ticks.
4. If ACTIVE==0, tile forcibly: thr_cur16=0, locked=0, no weight/decay/drive.
5. Decay pulls to 0, doesn't jump over 0 (6.6).
6. Lock by range [thr_lo16..thr_hi16] (6.6).
7. LOCK passthrough mandatory: drive_vec=in16 when locked.

---

18) UDP Protocol (packet_v1, fixed binary)

**Purpose:** machine cascading. Packets identical for IN and OUT.

**Format (37 bytes, little-endian):**

| Field | Type |
|-------|------|
| magic | u32 = 'D8UP' (0x50553844) |
| version | u16 = 1 |
| flags | u16: bit0 has_winner, bit1 has_bus, bit2 has_cycle, bit3 has_flags |
| frame_tag | u32 |
| domain_id | u8 |
| pattern_id | u16 |
| reset_mask16 | u16 |
| collision_mask16 | u16 |
| winner_tile_id | u16 |
| cycle_time_us | u32 |
| flags32_last | u32 |
| bus16[8] | u8 |

**Notes:**

- reset_mask16 sets domains for RESET_DOMAIN.
- collision_mask16/winner_tile_id/pattern_id valid if flags has_winner.
- bus16 valid if flags has_bus.
- cycle_time_us valid if flags has_cycle.
- flags32_last valid if flags has_flags.

---

END OF CONTRACT v0.2
