# DECIMA-8 Resonance Swarm Contract v0.2

**副标题:** 激活图 + 范围熔丝 + 衰减归零  
**代号:** Siberian Tank Interface  
**状态:** v0.2 DESIGN FREEZE

**所有者:** Conductor (Digital Island) ↔ Swarm/Island (Analog Core)

---

## 0) 范围 / 非目标

### Scope (v0.2)

- 一个确定性节奏用于：Emulator → Proto (PCB) → FPGA → ASIC
- **Level16:** 8 条数据线路，每条 0..15
- **双向 VSB 交换:**
  - Conductor 在 **READ 之前** 设置输入向量并在 READ 孔径期间保持稳定
  - Island 仅在 WRITE 时驱动 VSB (WRITE 后 readout)
- **Tile = 最小可编程实体** (RuleROM 直接寻址 tiles)
- **所有数据仅通过公共 BUS16 传输** (8 lane)。邻居不传输数据
- 邻居形成 **激活图 (gating/选通):** locked tile 激活后代以读取 BUS
- **范围熔丝 (LOCK):** tile 仅当 thr_cur16 ∈ [thr_lo16..thr_hi16] 时锁存 (两个边界都是 i16)
- **Decay 拉向 0:** 如果 decay16>0，累加器每个 tick 按模减小到零，**永不跳过 0**
- **分支坍缩:** 如果 tile 变为非活动 (非 ACTIVE)，强制重置为 thr_cur16=0, locked=0，不参与计算/drive
- Bake 仅在 EV_FLASH 之间原子应用
- 不引入 MT: 一个 swarm = 一个线程/进程

### Non-Goals (v0.2)

- 不固定绝对电压；固定 Level16 语义 + 节奏
- EV_FLASH 内无 baked 参数变化
- 不需要与旧 v0.1 格式兼容

---

## 1) 术语

- **Conductor** — 数字指挥 (CPU)，准备输入，触发 EV_FLASH，执行 bake/reset
- **Island/Swarm** — tile 网络 + 公共 BUS16 (8 lane) 在 VSB 之上
- **Tile** — 织布节点：8 输入 lanes, 8 输出 lanes (到 BUS), 本地 FUSE (thr/lock), weights, routing_flags16
- **Level16** — 值 0..15 (4 bits), "水位/能量"语义
- **READ phase** — tiles 采样输入 (VSB_INGRESS + 可选 BUS16), 更新 runtime, **不**驱动 BUS
- **WRITE phase** — Conductor 停止驱动 bus, Island 驱动 BUS16; 有 BUS_W 和 locked-祖先 (或 locked self) 的 tiles 写入，不读取输入
- **Domain** — 0..15; 用于重置 thr_cur16/locked 的 tile 组
- **RESET_DOMAIN(mask16)** — 域重置 (通过 EV_RESET_DOMAIN 或 auto-reset)
- **BAKE_APPLIED** — bool; power-on/reset 后为 0，成功 EV_BAKE() 后为 1
- **RoutingFlags16** — 16-bit 路由/激活标志字段 (见 section 8)
- **Parents(t)** — 有激活边指向 t 的父 tiles 集合 (见 section 8)
- **ACTIVE(t)** — tile "在活链中": 可以读取 BUS16, 计算，应用 decay

---

## 2) 硬件常量 (v0.2 freeze)

- **VSB:** 8 条数据线路 VSB[0..7], 每条承载 Level16
- **BUS16:** 8 lane, WRITE 时贡献求和
- **Domains:** 正好 16 个域 (0..15)
- **相位纪律:** READ/WRITE 强制；tile 不能在同一相位同时读写
- **Routing:** tile 有 **RoutingFlags16** (10 个使用 bits):

  N, E, S, W, NE, SE, SW, NW, BUS_R, BUS_W — 各 1 bit

- **总是工作在所有 8 lanes.** 无 per-lane 掩码
- 数据不发送给邻居。邻居只形成激活图以允许 BUS 读取权限

---

## 3) 接口平面

### 3.1 Data Plane: VSB[0..7]

每条线路承载 Level16; 来自前一个 tick 的数据仅通过激活图 (BUS_R) 可用，不通过算术求和。

### 3.2 Rhythm Plane: READ/WRITE (两相保护器)

- **READ:** 所有 tiles 采样输入并更新 state
- **WRITE:** tiles 驱动输出到 BUS16
- 它们之间强制 turnaround (方向间隙), 以便 Conductor 释放 VSB 且 Island 可以驱动它
- Conductor 仅在 WRITE 完成后读取 BUS16

### 3.3 Config Plane: CFG (SPI-like)

CFG_CS, CFG_SCLK, CFG_MOSI, CFG_MISO

通过 CFG:

- 加载 BakeBlob 到 staging
- 读取 FLAGS
- 命令 EV_RESET_DOMAIN(mask16)
- (可选) 读取 BAKE_ID_ACTIVE / PROFILE_ID_ACTIVE

---

## 4) Event Protocol / SHM ABI

### 4.1 外部事件 (API)

- **EV_FLASH(tag_u32)**

  - 执行一个确定性 READ→WRITE 周期
  - 返回 readout (R0/R1), FLAGS 单独读取 (CFG) 或在 return struct 中
  - 仅当 BAKE_APPLIED==1 时允许，否则 NotBaked (state 不变)

- **EV_RESET_DOMAIN(mask16)**

  - 仅在 EV_FLASH 之间
  - 仅当 BAKE_APPLIED==1，否则 NotBaked

- **EV_BAKE()**

  - 仅在 EV_FLASH 之间
  - 原子应用 staging BakeBlob
  - 成功时重置 runtime (见 6.3)
  - 错误时无变化

### 4.2 内部 EV_FLASH 子相位 (非外部 API)

1. PHASE_READ
2. TURNAROUND
3. PHASE_WRITE
4. READOUT_SAMPLE
5. INTERPHASE_AUTORESET (可选，见 section 15)

### 4.3 Readout Timing

Default R0_RAW_BUS:

- Conductor 在 PHASE_WRITE 完成后立即读取 BUS16[0..7]
- 在 SHM 中：EV_FLASH 填充 OUT_buf, Conductor 在返回后读取

### 4.4 Bake Transaction / CFG Staging

Digital Island 中的 Staging buffer。EV_BAKE 应用参数到 fabric。

EV_BAKE 错误 (最小):

OK, BakeBadMagic, BakeBadVersion, BakeBadLen, BakeMissingTLV, BakeBadTLVLen, BakeCRCFail, BakeReservedNonZero, TopologyMismatch, BakeNoBlob.

---

## 5) 标准 Tick (一次"flash")

### 5.1 Setup (Conductor)

- Conductor 设置 VSB_INGRESS16[0..7] (Level16)
- 保持稳定直到 READ 孔径结束

### 5.2 PHASE_READ (Island)

在 READ 开始:

- 固定 locked_before[t] = locked[t] 对所有 tiles
- 计算 ACTIVE[t] 为 **激活图闭包** (见 6.1)
- 如果 ACTIVE[t]==0 → tile 强制归零 (见 6.1), 不计算也不 drive

对 ACTIVE[t]==1:

- 形成 in16 (VSB_INGRESS) 按 6.1
- 如果 locked_before==0: 计算 row-pipeline (6.5), 更新 thr_cur16 带 decay (6.6), 确定 locked_after
- 如果 locked_before==1: tile 保持 locked, matrix/decay 不应用
- 选择 drive_vec (6.8)

### 5.3 TURNAROUND

- Conductor 移除 VSB 驱动 (Hi-Z / no-drive)
- Island 仅在 WRITE 时启用驱动

### 5.4 PHASE_WRITE (Island)

- Tile 写入 BUS16 仅当 BUS_W==1 且 (locked self 或有 locked-祖先)
- 写入 **整个** drive_vec[0..7] (所有 8 lanes), 然后"诚实求和" (section 13)

### 5.5 READOUT_SAMPLE (Conductor)

- R0_RAW_BUS: readout = BUS16[0..7] 为 8×Level16

### 5.6 INTERPHASE_AUTORESET (可选)

- 在 readout 和 FLAGS32_LAST 锁定后，应用 AutoReset-by-Fire (section 15)

---

## 6) Tile Model v0.2

### 6.1 ACTIVE + Tile 输入 (gating + 瞬间分支坍缩)

**Routing edges:** 方向 N/E/S/W/NE/SE/SW/NW 形成边 A→B 如果 A 有标志设置且邻居 B 存在 (见 8)。

**Parents(t):** 所有 p 有边 p→t。

**ACTIVE closure (canonical):**

Seed: `ACTIVE[t]=1` 如果 `BUS_R==1` (源/链根)。

Propagate: `ACTIVE[t]=1` 如果存在 `p∈Parents(t)` 使得 `ACTIVE[p]==1` 且 `locked_before[p]==1`。

这计算为单调闭包直到稳定 (最小不动点)。确定性通过使用仅 locked_before 保证。

**Tile 输入和强制零:**

如果 `ACTIVE[t]==0`, tile 被认为是"死区":
```
thr_cur16 := 0
locked := 0
drive_vec := {0..0}
weights/row/decay 在此 tick 不应用
```

如果 `ACTIVE[t]==1`, tile 读取 **仅 VSB_INGRESS16** (所有 8 lanes):
```
for i in 0..7:
  in16[t][i] = clamp15(VSB_INGRESS16[i])
  IN_CLIP[t][i] = (VSB_INGRESS16[i] > 15)
```

**重要:** BUS16 反映在前一个 EV_FLASH 中形成的 state, 不在当前 tick 与 VSB 求和。BUS16 在 READ 相位的唯一角色是语义的：有 BUS_R 标志的 tiles 成为激活图源 (ACTIVE seed)。总线数据不参与 in16 计算。

**Gating (选通):** 信号从祖先到后代传播用 2 ticks:
- Tick N: 祖先融合 → 在 PHASE_WRITE 中驱动总线
- Tick N+1: 后代通过 BUS_R 激活 → 读取 VSB_INGRESS → 计算

> **水比喻:** 公共总线如灌溉渠。父母 (locked tiles) 打开闸门 (gate), 允许水 (数据) 流向孩子的田地。但水总是在公共渠中，不是直接从父母传到孩子。

### 6.2 Baked Tile State (v0.2)

**Baked:**

- thr_lo16 (i16)
- thr_hi16 (i16) (不变量：thr_lo16 <= thr_hi16)
- decay16 (u16) (0..32767)
- domain_id4 (0..15)
- priority8 (0..255)
- pattern_id16 (0..32767)
- routing_flags16 (u16) (见 8)
- W[8][8] — SignedWeight5 (mag3∈[0..7]+sign1)
- reset_on_fire_mask16 (u16)

**Runtime:**

- thr_cur16 (i16: -32768..+32767)
- locked (0/1)

### 6.3 Reset Semantics (在 flash 之间)

EV_RESET_DOMAIN(mask16) 仅在 EV_FLASH 之间应用。

Auto-reset 在 INTERPHASE_AUTORESET 中应用。

如果 tile 的域落入 mask16:

- thr_cur16 := 0
- locked := 0

> Gating: 如果根/中间 tile 未融合 (reset/collide), 下一个 EV_FLASH 它不会给出 locked_before==1, 下游变为 ACTIVE==0 → 强制归零。

### 6.4 Canonical Functions (SignedWeight5)

Weight: mag3∈[0..7], sign1∈{0,1} (1="+", 0="−")。

Canonical signed 乘法 (无除法):

```
mul_signed_raw(a, mag, sign) = (sign ? +1 : -1) * (a * mag)
其中 a∈[0..15], mag∈[0..7] → [-105..+105] 每项
8 项每行 → [-840..+840] 每行
```

clamp15(x) = clamp_range(x, 0, 15)。

### 6.5 RowOut Pipeline 在 PHASE_READ (对 ACTIVE && !locked_before)

对每行 r=0..7:

```
row_raw_signed[r] = Σ_{i=0..7} (in16[i] * Wmag[r][i] * sign)  → 范围 [-840..+840]
```

对 lines/drive (无负数):

```
row16_out[r] = clamp15((max(row_raw_signed[r], 0) + 7) / 8) → 0..15
```

对累加器 (signed, 无 clamp):

```
row16_signed[r] = row_raw_signed[r] → 范围 [-840..+840]
```

### 6.6 累加器 + Decay-to-Zero + 范围熔丝

locked_before = locked (在 READ 开始时的快照)。

规则 **仅当 ACTIVE==1** 时应用。

**Decay 总是应用** (如果 decay16>0), 即使在 locked tiles 上。

如果 locked_before==0:

1. delta_raw = Σ_{r=0..7} row16_signed[r] → [-6720..+6720]
2. thr_tmp = thr_cur16 + delta_raw (在 i32 中)
3. **Decay 拉向 0，不跳过 0:**

```
if (decay16 > 0) {
  if (thr_tmp > 0) thr_tmp = max(thr_tmp - decay16, 0)
  else if (thr_tmp < 0) thr_tmp = min(thr_tmp + decay16, 0)
}
thr_cur16 = (i16)clamp_range(thr_tmp, -32768, 32767)
```

4. **范围熔丝:**

```
range_active = (thr_lo16 < thr_hi16)
in_range = range_active && (thr_lo16 <= thr_cur16) && (thr_cur16 <= thr_hi16)

has_signal = (delta_raw != 0)
entered_by_decay = (decay16 > 0) && (in_range == true) && (in_range_before_decay == false)
locked_after = (BAKE_APPLIED==1) && in_range && (has_signal || entered_by_decay)
```

注意：`thr_lo16 == thr_hi16` 意味着禁用的熔丝 (包括基本情况 `0..0`) — 这样的 tile 不锁存。

如果 locked_before==1:

- locked_after := 1
- Weights 不应用 (passthrough)
- **Decay 应用** (如果 decay16>0, thr_cur16 拉向 0):

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

**不变量 v0.2:**

- locked==1 ⇒ thr_lo16 <= thr_cur16 <= thr_hi16

### 6.7 FUSE-LOCK Passthrough (强制)

如果 locked_after==1, tile 作为"铜桥":

- Matrix W 不应用
- drive_vec[i] = in16[i] 对所有 i=0..7 (passthrough)
- **Decay 应用** (如果 decay16>0, thr_cur16 拉向 0)

### 6.8 Drive Selection (WRITE)

在 READ 结束时:

- 如果 locked_after==1: drive_vec[i] = in16[i]
- 否则：drive_vec[i] = row16_out[i]

---

## 7) RoutingFlags16: 比特映射

LSB-first:

| Bit | Flag | Description |
|-----|------|-------------|
| bit0 | N | 北 |
| bit1 | E | 东 |
| bit2 | S | 南 |
| bit3 | W | 西 |
| bit4 | NE | 东北 |
| bit5 | SE | 东南 |
| bit6 | SW | 西南 |
| bit7 | NW | 西北 |
| bit8 | BUS_R | 读取总线 (ACTIVE 源) |
| bit9 | BUS_W | 写入总线 (WRITE 相位) |
| bit10..15 | reserved | 必须为 0 |

---

## 8) 激活图 (邻居) 和坐标

### 8.1 邻居按拓扑

tile_id = y * tile_w + x

**Cardinal:**

- N(x,y) = (x, y-1) 如果 y > 0
- S(x,y) = (x, y+1) 如果 y < tile_h-1
- W(x,y) = (x-1, y) 如果 x > 0
- E(x,y) = (x+1, y) 如果 x < tile_w-1

**Diagonals:**

- NE(x,y) = (x+1, y-1) 如果 x < tile_w-1 且 y > 0
- SE(x,y) = (x+1, y+1) 如果 x < tile_w-1 且 y < tile_h-1
- SW(x,y) = (x-1, y+1) 如果 x > 0 且 y < tile_h-1
- NW(x,y) = (x-1, y-1) 如果 x > 0 且 y > 0

### 8.2 激活边

如果 tile A 设置了 Dir 标志且邻居 B = neighbor(A, Dir) 存在 → 边 A→B。

这 **仅** 影响 ACTIVE 计算 (6.1)。数据不传输。

Multi-parents 允许。Cycles 允许 (确定性通过 locked_before 和最小不动点 ACTIVE)。

---

## 9) BUS Semantics: 诚实求和 + CLIP/OVF

在 PHASE_WRITE 中对每条线路 i=0..7:

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

策略 v0.2: 仅 saturate (clamp15), 无 wrap/divide。

---

## 10) COLLIDE: 域和 Winner

在当前 tick 中的定义:

- FIRE(t) = (locked_before[t]==0 && locked_after[t]==1)
- FIRED_SET(d) = { t | domain_id(t)=d && FIRE(t)=1 }
- cnt(d) = |FIRED_SET(d)|

**规则:**

- cnt(d)=0 → 无 winner, COLLIDE(d)=0
- cnt(d)=1 → 单个 winner, COLLIDE(d)=0
- cnt(d)≥2 → COLLIDE(d)=1 且 winner 选择:

  1. max priority8
  2. 平局时 min tile_id

```
winner(d) = argmax_{t∈FIRED_SET(d)} (priority8(t), -tile_id(t))
```

---

## 11) AutoReset-by-Fire (inter-phase 域重置) — v0.2

(可选；如果不需要可以完全省略)

每个 tile 有 baked reset_on_fire_mask16[t]。

**Auto-reset mask:**

```
AUTO_RESET_MASK16 =
  OR_{d | cnt(d)>0} reset_on_fire_mask16[winner(d)]
```

**应用** 严格在当前 EV_FLASH 的 READOUT_SAMPLE 之后:

```
apply_reset_domain(AUTO_RESET_MASK16)
```

apply_reset_domain 的效果 — 如 6.3 对掩码中的域，**除了** resetting tile 及其整个祖先链 (它们排除在 reset 外)。

**保证:** winner 总是在当前 tick 中有时间 locked/drive (reset 仅在"flash 之间")。

---

## 12) READOUT_POLICY v0.2

Default: R0_RAW_BUS — readout = BUS16[0..7] 在 WRITE 后。

可选 R1_DOMAIN_WINNER_ID32 可能但需要纪律"仅 winner 驱动 ID", 否则求和破坏 ID。 (如果需要，可以添加精确 canon, 现在不关键。)

---

## 13) Bake Binary TLV Spec v0.2 (与 v0.1 不兼容)

### 13.1 一般规则

- Little-endian
- TLV padding = 0, value 对齐到 4 字节边界
- CRC32 IEEE (zlib/crc32) 从 offset 0 到 TLV_CRC32 开始的所有字节 (TLV_CRC32 header+value 不包括在 CRC 中)

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

- bit0 (BAKE_FLAG_DOUBLE_STRAIT): conductor 对每个传入和弦进行双重注入；在此模式下不在第一次注入时输出决策

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
| TLV_CRC32 | 0xFFFE (最后) |

### 13.5 必需 TLV

列表中所有都是必需的，除了 AutoReset 可以是零掩码。

---

## 14) TLV 结构 (canon v0.2)

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

Reserved bits 10..15 必须为 0。

### 14.4 TLV_TILE_WEIGHTS_PACKED (0x0160), len = tile_count × 40

如前:

- 32 bytes: magnitudes Wmag[8][8] (64 × mag3∈[0..7], 4 bits 每个，高 bit=0)
- 8 bytes: sign bits Wsign[8][8] (64 bits), LSB-first

### 14.5 TLV_RESET_ON_FIRE_MASK16 (0x0150), len = tile_count × 2

Per tile: reset_mask16 u16

### 14.6 TLV_READOUT_POLICY (0x0140), len=12

**ReadoutPolicyV0:**

| Field | Type |
|-------|------|
| mode | u8 (0=R0_RAW_BUS, 1=R1_DOMAIN_WINNER_ID32) |
| reserved0 | u8 (=0) |
| winner_domain_mask | u16 (如果 mode=1) |
| settle_ns | u16 (硬件建议；emulator 可忽略) |
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

## 15) Load-time Validation (强制)

Bake 加载器必须:

1. 检查 magic/ver/total_len
2. 验证所有必需 TLVs 存在
3. 严格 len:
   - TILE_PARAMS_V2: tile_count × 13
   - TILE_ROUTING_FLAGS16: tile_count × 2
   - TILE_WEIGHTS_PACKED: tile_count × 40
   - RESET_ON_FIRE_MASK16: tile_count × 2
4. CRC32 按规则"在 TLV_CRC32 之前"
5. Reserved 字段 = 0; flags8==0; routing reserved bits 10..15 == 0; header.flags 只能包含 BAKE_FLAG_DOUBLE_STRAIT
6. thr_lo16 <= thr_hi16 对每个 tile
7. tile_count == tile_w × tile_h

---

## 16) Runtime FLAGS (最小)

Island 返回 FLAGS32 (最小):

| Bit | Flag |
|-----|------|
| bit0 | READY_LAST |
| bit1 | OVF_ANY_LAST |
| bit2 | COLLIDE_ANY_LAST |

(masks OVF/COLLIDE 可选)

---

## 17) MUST for Emulator (匹配硬件)

1. EV_FLASH 总是执行 READ→WRITE, 双缓冲 (不能读取正在写入的内容)
2. EV_RESET_DOMAIN 和 EV_BAKE 仅在 EV_FLASH 之间
3. ACTIVE closure — **最小不动点** 按 locked_before (6.1)。这确保"分支坍缩"无额外半活 tick
4. 如果 ACTIVE==0, tile 强制：thr_cur16=0, locked=0, 无 weight/decay/drive
5. Decay 拉向 0，不跳过 0 (6.6)
6. Lock 按范围 [thr_lo16..thr_hi16] (6.6)
7. LOCK passthrough 强制：drive_vec=in16 当 locked

---

## 18) UDP Protocol (packet_v1, fixed binary)

**用途:** 机器级联。IN 和 OUT 的 packets 相同。

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

**注意:**

- reset_mask16 设置域用于 RESET_DOMAIN
- collision_mask16/winner_tile_id/pattern_id 有效如果 flags has_winner
- bus16 有效如果 flags has_bus
- cycle_time_us 有效如果 flags has_cycle
- flags32_last 有效如果 flags has_flags

---

**CONTRACT v0.2 结束**
