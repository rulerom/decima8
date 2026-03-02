DECIMA-8 Resonance Swarm Contract v0.2 (Activation Graph + Range Fuse + Decay-to-Zero)
Codename: Siberian Tank Interface

Status: v0.2 DESIGN FREEZE

Owner: Conductor (Digital Island) ↔ Swarm/Island (Analog Core)

---

## 0) Scope / Non-Goals

### Scope (v0.2)

- Один детерминированный ритм для: Emulator → Proto (PCB) → FPGA → ASIC.
- Level16: 0..15 на каждой из 8 линий данных.
- Двунаправленный обмен на VSB:
- Conductor задаёт входной вектор **до READ** и держит стабильно на апертуре READ,
- Island драйвит VSB только в WRITE (readout после WRITE).
- **Tile = минимальная программируемая сущность** (RuleROM адресует тайлы).
- **Все данные передаются только через общую шину BUS16** (8 lane). Соседи данные не передают.
- Соседи формируют **граф активации (эстафету)**: locked-тайл активирует потомков для чтения BUS.
- **Фьюз (LOCK) по диапазону:** тайл защёлкивается только если thr_cur16 ∈ [thr_lo16..thr_hi16] (оба границы — i16).
- **Decay тянет к 0:** если decay16>0, аккумулятор каждый tick уменьшается по модулю к нулю и **никогда не перескакивает через 0**.
- **Схлоп ветки:** если тайл становится неактивным (не ACTIVE), он принудительно сбрасывается в thr_cur16=0, locked=0, и не участвует в вычислениях/драйве.
- Bake применяется атомарно только между EV_FLASH.
- MT не вводится: один swarm = один поток/процесс.

### Non-Goals (v0.2)

- Абсолютные вольтажи не фиксируются; фиксируется семантика Level16 + ритм.
- Никаких изменений baked-параметров внутри EV_FLASH.
- Совместимость со старым форматом v0.1 не требуется.

---

## 1) Термины

- **Conductor** — цифровой дирижёр (CPU), готовит вход, дергает EV_FLASH, делает bake/reset.
- **Island/Swarm** — сеть тайлов + общая шина BUS16 (8 lane) поверх VSB.
- **Tile** — узел ткани: 8 входных lane, 8 выходных lane (в BUS), локальный FUSE (thr/lock), веса и routing_flags16.
- **Level16** — значение 0..15 (4 бита), семантика “уровня/энергии”.
- **READ phase** — тайлы читают вход (VSB_INGRESS + опц. BUS16), обновляют runtime, НЕ драйвят BUS.
- **WRITE phase** — Conductor перестаёт драйвить шину, Island драйвит BUS16; пишут тайлы с BUS_W и locked-предком (или locked self), вход не читается.
- **Domain** — 0..15; группа тайлов для сброса thr_cur16/locked.
- **RESET_DOMAIN(mask16)** — сброс доменов (через EV_RESET_DOMAIN или авто-reset).
- **BAKE_APPLIED** — bool; 0 после power-on/reset, становится 1 после успешного EV_BAKE().
- **RoutingFlags16** — 16-битное поле флагов маршрутизации/активации (см. раздел 8).
- **Parents(t)** — множество родительских тайлов, имеющих ребро активации в сторону t (см. раздел 8).
- **ACTIVE(t)** — тайл “в цепочке живой”: может читать BUS16, вычисляться, применять decay.

---

## 2) Hard Constants (заморозка v0.2)

- VSB: 8 линий данных VSB[0..7], каждая несёт Level16.
- BUS16: 8 lane, суммирование вкладов в WRITE.
- Domains: ровно 16 доменов (0..15).
- Дисциплина фаз: READ/WRITE обязательны; тайл не может одновременно читать и писать в одной фазе.
- Routing: на тайл есть **RoutingFlags16** (10 используемых бит):

  N, E, S, W, NE, SE, SW, NW, BUS_R, BUS_W — по 1 биту каждый.
- **Всегда работаем по всем 8 lane.** Нет per-lane масок.
- Данные соседям не отправляются. Соседи только формируют граф активации для разрешения чтения BUS.

---

## 3) Плоскости интерфейса

### 3.1 Data Plane: VSB[0..7]

Каждая линия несёт Level16; данные от предыдущего тика доступны только через граф активации (BUS_R), не через арифметическое суммирование.

### 3.2 Rhythm Plane: READ/WRITE (двухфазный предохранитель)

- READ: все тайлы семплируют вход и обновляют state.
- WRITE: тайлы выставляют выходы в BUS16.
- Между ними обязателен turnaround (зазор направления), чтобы Conductor отпустил VSB и Island мог её драйвить.
- Conductor читает BUS16 только после завершения WRITE.

### 3.3 Config Plane: CFG (SPI-like)

CFG_CS, CFG_SCLK, CFG_MOSI, CFG_MISO

Через CFG:

- загрузка BakeBlob в staging
- чтение FLAGS
- команда EV_RESET_DOMAIN(mask16)
- (опционально) чтение BAKE_ID_ACTIVE / PROFILE_ID_ACTIVE

---

## 4) Event Protocol / SHM ABI

### 4.1 Внешние события (API)

- **EV_FLASH(tag_u32)**

  - выполняет один детерминированный цикл READ→WRITE
  - возвращает readout (R0/R1), FLAGS читаются отдельно (CFG) или в return struct
  - разрешён только если BAKE_APPLIED==1, иначе NotBaked (состояние не меняется)

- **EV_RESET_DOMAIN(mask16)**

  - только между EV_FLASH
  - только если BAKE_APPLIED==1, иначе NotBaked

- **EV_BAKE()**

  - только между EV_FLASH
  - применяет staging BakeBlob атомарно
  - при успехе делает reset runtime (см. 6.3)
  - при ошибке ничего не меняет

### 4.2 Внутренние подфазы EV_FLASH (не внешний API)

1. PHASE_READ
2. TURNAROUND
3. PHASE_WRITE
4. READOUT_SAMPLE
5. INTERPHASE_AUTORESET (опционально, по разделу 15)

### 4.3 Readout Timing

Default R0_RAW_BUS:

- Conductor читает BUS16[0..7] сразу после завершения PHASE_WRITE этого же EV_FLASH.
- В SHM: EV_FLASH заполняет OUT_buf, Conductor читает после возврата.

### 4.4 Bake Transaction / CFG Staging

Staging buffer в Digital Island. EV_BAKE применяет параметры в fabric.

Ошибки EV_BAKE (минимум):

OK, BakeBadMagic, BakeBadVersion, BakeBadLen, BakeMissingTLV, BakeBadTLVLen, BakeCRCFail, BakeReservedNonZero, TopologyMismatch, BakeNoBlob.

---

## 5) Канонический Tick (одна "вспышка")

### 5.1 Setup (Conductor)

- Conductor выставляет VSB_INGRESS16[0..7] (Level16).
- Держит стабильным до конца апертуры READ.

### 5.2 PHASE_READ (Island)

В начале READ:

- фиксируем locked_before[t] = locked[t] для всех тайлов
- вычисляем ACTIVE[t] как **замыкание по графу активации** (см. 6.1)
- если ACTIVE[t]==0 → тайл принудительно в ноль (см. 6.1), не вычисляется и не драйвит

Для ACTIVE[t]==1:

- формируем in16 (VSB_INGRESS) по 6.1
- если locked_before==0: считаем row-pipeline (6.5), обновляем thr_cur16 с decay (6.6), определяем locked_after
- если locked_before==1: тайл остаётся locked, матрица/decay не применяются
- выбираем drive_vec (6.8)

### 5.3 TURNAROUND

- Conductor снимает драйв VSB (Hi-Z / no-drive).
- Island включает драйв только в WRITE.

### 5.4 PHASE_WRITE (Island)

- Тайл пишет в BUS16 только если BUS_W==1 и (locked self или есть locked-предок).
- Пишется **весь** drive_vec[0..7] (все 8 lane), дальше “честное суммирование” (раздел 13).

### 5.5 READOUT_SAMPLE (Conductor)

- R0_RAW_BUS: readout = BUS16[0..7] как 8×Level16.

### 5.6 INTERPHASE_AUTORESET (опционально)

- после фиксации readout и FLAGS32_LAST применяем AutoReset-by-Fire (раздел 15).

---

## 6) Tile Model v0.2

### 6.1 ACTIVE + вход тайла (эстафета + мгновенный схлоп ветки)

Routing edges: направления N/E/S/W/NE/SE/SW/NW формируют ребро A→B если у A стоит флаг и сосед B существует (см. 8).

Parents(t): все p, у которых есть ребро p→t.

ACTIVE closure (канон):
Seed: `ACTIVE[t]=1` если `BUS_R==1` (источник/корень цепочки).
Propagate: `ACTIVE[t]=1` если существует `p∈Parents(t)` такой, что `ACTIVE[p]==1` и `locked_before[p]==1`.
Это вычисляется как монотонное замыкание до стабилизации (least fixed point). Детерминизм обеспечен тем, что используется только locked_before.

Вход тайла и принудительный ноль:

Если `ACTIVE[t]==0`, то тайл считается «мёртвым участком»:
`thr_cur16 := 0`
`locked := 0`
`drive_vec := {0..0}`
веса/row/decay не применяются в этом tick

Если `ACTIVE[t]==1`, то тайл читает только VSB_INGRESS16 (все 8 lane):
```
for i in 0..7:
  in16[t][i] = clamp15(VSB_INGRESS16[i])
  IN_CLIP[t][i] = (VSB_INGRESS16[i] > 15)
```
Важно: Шина BUS16 отражает состояние, сформированное в предыдущем EV_FLASH, и не суммируется с VSB в текущем тике. Единственная роль BUS16 в фазе READ — семантическая: тайлы с флагом BUS_R становятся источниками графа активации (ACTIVE seed). Данные из шины не участвуют в вычислении in16.
Эстафета: Сигнал распространяется от предка к потомку за 2 тика:
тик N: предок фьюзится → драйвит шину в PHASE_WRITE
тик N+1: потомок активируется через BUS_R → читает VSB_INGRESS → вычисляется

---

### 6.2 Baked state тайла (v0.2)

Baked:

- thr_lo16 (i16)
- thr_hi16 (i16) (инвариант: thr_lo16 <= thr_hi16)
- decay16 (u16) (0..32767)
- domain_id4 (0..15)
- priority8 (0..255)
- pattern_id16 (0..32767)
- routing_flags16 (u16) (см. 8)
- W[8][8] — SignedWeight5 (mag3∈[0..7]+sign1)
- reset_on_fire_mask16 (u16)

Runtime:

- thr_cur16 (i16: -32768..+32767)
- locked (0/1)

---

### 6.3 Reset semantics (между вспышками)
EV_RESET_DOMAIN(mask16) применяется только между EV_FLASH.

Авто-reset применяется в INTERPHASE_AUTORESET.

Если домен тайла попадает в mask16:

- thr_cur16 := 0
- locked := 0

> Эстафета: если корневой/промежуточный тайл расфюжен (reset/collide), то в следующем EV_FLASH он не даст locked_before==1, и downstream станет ACTIVE==0 → принудительно в 0.

---

### 6.4 Канонические функции (SignedWeight5)
Вес: mag3∈[0..7], sign1∈{0,1} (1="+", 0="−").

Каноническое signed-умножение (без деления):

mul_signed_raw(a, mag, sign) = (sign ? +1 : -1) * (a * mag)
где a∈[0..15], mag∈[0..7] → [-105..+105] за один член.
8 членов в строке → [-840..+840] за строку.

clamp15(x)=clamp_range(x,0,15).

---

### 6.5 RowOut pipeline в PHASE_READ (для ACTIVE && !locked_before)

Для каждой строки r=0..7:

row_raw_signed[r] = Σ_{i=0..7} (in16[i] * Wmag[r][i] * sign)  → диапазон [-840..+840]

Для линий/drive (без отрицательных):

row16_out[r] = clamp15( (max(row_raw_signed[r], 0) + 7) / 8 ) → 0..15

Для аккумулятора (signed вклад, без clamp):

row16_signed[r] = row_raw_signed[r] → диапазон [-840..+840]

---

### 6.6 Аккумулятор + decay-to-zero + fuse-by-range

locked_before = locked (снимок в начале READ).

Правила применяются **только если ACTIVE==1**.

**Decay применяется всегда** (если decay16>0), даже на locked тайлах.

Если locked_before==0:

1. delta_raw = Σ_{r=0..7} row16_signed[r] → [-6720..+6720]
2. thr_tmp = thr_cur16 + delta_raw (в i32)
3. **Decay тянет к 0 и не перескакивает 0:**

```
if (decay16 > 0) {
  if (thr_tmp > 0) thr_tmp = max(thr_tmp - decay16, 0)
  else if (thr_tmp < 0) thr_tmp = min(thr_tmp + decay16, 0)
}
thr_cur16 = (i16)clamp_range(thr_tmp, -32768, 32767)
```

4. **Фьюз по диапазону:**

  range_active = (thr_lo16 < thr_hi16)
  in_range = range_active && (thr_lo16 <= thr_cur16) && (thr_cur16 <= thr_hi16)

  has_signal = (delta_raw != 0)
  entered_by_decay = (decay16 > 0) && (in_range == true) && (in_range_before_decay == false)
  locked_after = (BAKE_APPLIED==1) && in_range && (has_signal || entered_by_decay)

Примечание: `thr_lo16 == thr_hi16` означает отключенный фьюз (в т.ч. базовый случай `0..0`) — такой тайл не защёлкивается.

Если locked_before==1:

- locked_after := 1
- веса не применяются (passthrough)
- **Decay применяется** (если decay16>0, thr_cur16 тянется к 0):
```
if (decay16 > 0) {
  if (thr_cur16 > 0) thr_cur16 = max(thr_cur16 - decay16, 0)
  else if (thr_cur16 < 0) thr_cur16 = min(thr_cur16 + decay16, 0)
}
```

События:

- LOCK_TRANSITION(t) = (locked_before==0 && locked_after==1)
- FIRE(t) = LOCK_TRANSITION(t)
- locked := locked_after

Инвариант v0.2:

- locked==1 ⇒ thr_lo16 <= thr_cur16 <= thr_hi16

---

### 6.7 FUSE-LOCK passthrough (обязательный)

Если locked_after==1, тайл действует как "медный мост":

- матрица W не применяется,
- drive_vec[i] = in16[i] для всех i=0..7 (passthrough),
- **Decay применяется** (если decay16>0, thr_cur16 тянется к 0).

---

### 6.8 Drive selection (WRITE)

В конце READ:

- если locked_after==1: drive_vec[i] = in16[i]
- иначе: drive_vec[i] = row16_out[i]

---

## 7) RoutingFlags16: карта битов

LSB-first:

- bit0  N
- bit1  E
- bit2  S
- bit3  W
- bit4  NE
- bit5  SE
- bit6  SW
- bit7  NW
- bit8  BUS_R  (источник ACTIVE)
- bit9  BUS_W  (писать в BUS в WRITE)
- bit10..15 reserved=0

---

## 8) Граф активации (соседи) и координаты

### 8.1 Соседи по топологии

tile_id = y*tile_w + x

Кардинальные:

- N(x,y)=(x,y-1) если y>0
- S(x,y)=(x,y+1) если y<tile_h-1
- W(x,y)=(x-1,y) если x>0
- E(x,y)=(x+1,y) если x<tile_w-1

Диагонали:

- NE(x,y)=(x+1,y-1) если x<tile_w-1 и y>0
- SE(x,y)=(x+1,y+1) если x<tile_w-1 и y<tile_h-1
- SW(x,y)=(x-1,y+1) если x>0 и y<tile_h-1
- NW(x,y)=(x-1,y-1) если x>0 и y>0

### 8.2 Рёбра активации

Если у тайла A установлен флаг Dir и сосед B=neighbor(A,Dir) существует → ребро A→B.

Это влияет **только** на вычисление ACTIVE (6.1). Данные не передаются.

Мульти-родители разрешены. Циклы разрешены (детерминизм через locked_before и least-fixed-point ACTIVE).

---

## 9) BUS Semantics: честное суммирование + CLIP/OVF

В PHASE_WRITE для каждой линии i=0..7:

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

Политика v0.2: только saturate (clamp15), без wrap/divide.

---

## 10) COLLIDE: домены и winner (как v0.1, но без DAG-зависимостей)

Определения в текущем tick:

- FIRE(t) = (locked_before[t]==0 && locked_after[t]==1)
- FIRED_SET(d) = { t | domain_id(t)=d && FIRE(t)=1 }
- cnt(d)=|FIRED_SET(d)|

Правила:

- cnt(d)=0 → нет winner, COLLIDE(d)=0
- cnt(d)=1 → winner единственный, COLLIDE(d)=0
- cnt(d)≥2 → COLLIDE(d)=1 и winner выбирается:

    1. max priority8
    2. при равенстве min tile_id

winner(d) = argmax_{t∈FIRED_SET(d)} (priority8(t), -tile_id(t))

---

## 11) AutoReset-by-Fire (межфазный доменный сброс) — v0.2

(опционально; если не нужно — можно выкинуть целиком)

Каждый тайл имеет baked reset_on_fire_mask16[t].

Маска авто-reset:

```
AUTO_RESET_MASK16 =
  OR_{d | cnt(d)>0} reset_on_fire_mask16[ winner(d) ]
```

Применение строго после READOUT_SAMPLE текущего EV_FLASH:

apply_reset_domain(AUTO_RESET_MASK16)

Эффект apply_reset_domain — как 6.3 для доменов в маске, **кроме** тайла‑сбрасывателя и всей цепочки его предков (они исключаются из reset).

Гарантия: winner всегда успевает locked/drive в текущем tick (reset только “между вспышками”).

---

## 12) READOUT_POLICY v0.2

Default: R0_RAW_BUS — readout = BUS16[0..7] после WRITE.

Опционально R1_DOMAIN_WINNER_ID32 возможно, но требует дисциплины “только winner драйвит ID”, иначе сумма разрушит ID. (Если нужно — добавим точный канон, сейчас не критично.)

---

## 13) Bake Binary TLV Spec v0.2 (несовместим с v0.1)

### 13.1 Общие правила

- Little-endian.
- TLV padding = 0, выравнивание value до 4-байт boundary.
- CRC32 IEEE (zlib/crc32) по всем байтам blob от offset 0 до начала TLV_CRC32 (TLV_CRC32 header+value в CRC не входят).

### 13.2 Header (28 bytes)

BakeBlobHeader:

- magic char[4] = "D8BK"
- ver_major u16 = 2
- ver_minor u16 = 0
- flags u32
- total_len u32
- bake_id u32
- profile_id u32
- reserved0 u32 = 0

Header flags:

- bit0 (BAKE_FLAG_DOUBLE_STRAIT): дирижер делает двойной пролив на каждый входящий аккорд; в этом режиме НЕ выдавать решения на первый пролив.

### 13.3 TLV header (8 bytes)

- type u16
- tflags u16
- len u32

- value[len] + padding

### 13.4 TLV type map v0.2

- TLV_TOPOLOGY (0x0100)
- TLV_TILE_PARAMS_V2 (0x0121)
- TLV_TILE_ROUTING_FLAGS16 (0x0131)
- TLV_READOUT_POLICY (0x0140)
- TLV_RESET_ON_FIRE_MASK16 (0x0150)
- TLV_TILE_WEIGHTS_PACKED (0x0160)
- TLV_TILE_FIELD_LIMIT (0x0170)
- TLV_CRC32 (0xFFFE) — последний

### 13.5 Обязательные TLV

Все из списка выше обязательны, кроме того, что AutoReset можно сделать нулевыми масками.

---

## 14) TLV структуры (канон v0.2)

### 14.1 TLV_TOPOLOGY (0x0100), len=16

TopologyV0:

- tile_count u32
- tile_w u16
- tile_h u16
- lanes u8 (=8)
- domains u8 (=16)
- reserved u16 (=0)
- reserved2 u32 (=0)

### 14.2 TLV_TILE_PARAMS_V2 (0x0121), len = tile_count * 13

TileParamsV2 (13 bytes per tile):

- thr_lo16   i16 (bytes 0-1)
- thr_hi16   i16 (bytes 2-3)
- decay16    u16 (bytes 4-5)
- domain_id4 u8  (low nibble, high nibble reserved=0) (byte 6)
- priority8  u8  (byte 7)
- pattern_id16 u16 (bytes 8-9)
- flags8     u8  (=0 reserved) (byte 10)
- reserved   u16 (=0) (bytes 11-12)

### 14.3 TLV_TILE_ROUTING_FLAGS16 (0x0131), len = tile_count * 2

Per tile:

- routing_flags16 u16 (LE)

Reserved bits 10..15 должны быть 0.

### 14.4 TLV_TILE_WEIGHTS_PACKED (0x0160), len = tile_count * 40

Как раньше:

- 32 bytes: magnitudes Wmag[8][8] (64 × mag3∈[0..7], 4 бита каждый, старший бит=0)
- 8 bytes: sign bits Wsign[8][8] (64 бита), LSB-first

### 14.5 TLV_RESET_ON_FIRE_MASK16 (0x0150), len = tile_count * 2

Per tile: reset_mask16 u16

### 14.6 TLV_READOUT_POLICY (0x0140), len=12

ReadoutPolicyV0:

- mode u8 (0=R0_RAW_BUS, 1=R1_DOMAIN_WINNER_ID32)
- reserved0 u8 (=0)
- winner_domain_mask u16 (если mode=1)
- settle_ns u16 (рекомендация для железа; эмуль может игнорировать)
- reserved1 u16 (=0)
- reserved2 u32 (=0)

### 14.7 TLV_CRC32 (0xFFFE), len=4

- crc32 u32

### 14.8 TLV_TILE_FIELD_LIMIT (0x0170), len=4

- tile_field_limit u32 (0 = full kTileCount)

---

## 15) Load-time validation (обязательное)

Загрузчик bake обязан:

1. magic/ver/total_len
2. наличие всех обязательных TLV
3. строгие len:

    - TILE_PARAMS_V2: tile_count*13
    - TILE_ROUTING_FLAGS16: tile_count*2
    - TILE_WEIGHTS_PACKED: tile_count*40
    - RESET_ON_FIRE_MASK16: tile_count*2
4. CRC32 по правилу “до TLV_CRC32”
5. reserved-поля = 0; flags8==0; routing reserved bits 10..15 == 0
   header.flags может содержать только BAKE_FLAG_DOUBLE_STRAIT
6. thr_lo16 <= thr_hi16 для каждого тайла
7. tile_count == tile_w * tile_h

---

## 16) Runtime FLAGS (минимум)

Island отдаёт FLAGS32 (минимум):

- bit0 READY_LAST
- bit1 OVF_ANY_LAST
- bit2 COLLIDE_ANY_LAST

  (маски OVF/COLLIDE опционально)

---

## 18) UDP Protocol (packet_v1, fixed binary)

Назначение: каскадирование машин. Пакеты одинаковые для IN и OUT.

Формат (37 bytes, little-endian):

- magic u32 = 'D8UP' (0x50553844)
- version u16 = 1
- flags u16: bit0 has_winner, bit1 has_bus, bit2 has_cycle, bit3 has_flags
- frame_tag u32
- domain_id u8
- pattern_id u16
- reset_mask16 u16
- collision_mask16 u16
- winner_tile_id u16
- cycle_time_us u32
- flags32_last u32
- bus16[8] u8

Примечания:

- reset_mask16 задаёт домены для RESET_DOMAIN.
- collision_mask16/winner_tile_id/pattern_id валидны если flags has_winner.
- bus16 валиден если flags has_bus.
- cycle_time_us валиден если flags has_cycle.
- flags32_last валиден если flags has_flags.

---

## 17) MUST для эмулятора (чтобы железо совпало)

1. EV_FLASH всегда выполняет READ→WRITE, двойная буферизация (нельзя читать то, что пишешь).
2. EV_RESET_DOMAIN и EV_BAKE только между EV_FLASH.
3. ACTIVE closure — **least fixed point** по locked_before (6.1). Это обеспечивает “схлоп ветки” без лишних полуживых тиков.
4. Если ACTIVE==0, тайл принудительно: thr_cur16=0, locked=0, без веса/decay/drive.
5. Decay тянет к 0 и не перескакивает 0 (6.6).
6. Lock по диапазону [thr_lo16..thr_hi16] (6.6).
7. LOCK passthrough обязателен: drive_vec=in16 при locked.

---

END OF CONTRACT v0.2
