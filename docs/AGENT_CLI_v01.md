# DECIMA-8 Agent CLI v0.1

Status: draft + initial implementation.

Purpose: give AI agents a stable, scriptable surface for training loops:

1. create or receive a `.d8p`
2. create a deterministic input tape
3. run the swarm emulator
4. read machine-readable results
5. update/bake again

The CLI is intentionally boring: no terminal UI, no color, no prompts, deterministic output.

## Binary

`d8_agent_cli`

Build target:

```bash
cmake -B build -S opensource -DD8_BUILD_AGENT_CLI=ON
cmake --build build --target d8_agent_cli
```

## VSB Tape Contract

Agent tape format is raw binary. The preferred format is `raw8`:

- 8 bytes per frame
- one byte per lane
- every lane must be Level16 `0..15`

Legacy OCR/tipograf tapes use `packed4`:

- 4 bytes per frame
- two Level16 nibbles per byte
- byte 0 = lane0 low nibble, lane1 high nibble
- byte 1 = lane2/lane3, byte 2 = lane4/lane5, byte 3 = lane6/lane7

`run` supports `--tape-format raw8|packed4|auto`.

## Commands

### `inspect-bake`

Dump the actual swarm encoded in a `.d8p`. This is the command agents use when they need to see the personality before running or editing it.

```bash
d8_agent_cli inspect-bake \
  --bake ../../store/tipograf/tipograf_v01.d8p \
  --json tipograf_swarm.json \
  --html tipograf_swarm.html
```

The JSON contains topology, readout policy, summary counters, and one record per active tile:

```json
{
  "tile": 17,
  "x": 17,
  "y": 0,
  "domain": 0,
  "priority": 7,
  "pattern": 42,
  "thr_lo": 1,
  "thr_hi": 1000,
  "decay16": 0,
  "reset_on_fire_mask16": 65535,
  "routing": {"N":0,"E":1,"S":1,"W":0,"NE":0,"SE":0,"SW":0,"NW":0,"bus_w":0,"bus_r":1},
  "weights": {
    "nonzero": 12,
    "positive": 8,
    "negative": 4,
    "mag_sum": 39,
    "values": [0,3,-2,0,1]
  }
}
```

`weights.values` always contains 64 signed magnitudes in the tile's weight order. Positive values are excitatory, negative values are inhibitory, zero values are disconnected.

The HTML report shows an IDE-like swarm grid plus a pattern-tile table. Pattern tiles are labeled as `pN`; bus writers/readers get gold/cyan edge marks. The visual layout uses the runtime effective tile grid, so a 256-tile bake is shown as `32x8` even if the raw topology TLV still reports the full `128x32` field.

### `inspect-tape`

Dump a VSB tape as readable frames and, when a bake is provided, split it into pattern segments. This is the CLI equivalent of the IDE accordion view.

```bash
d8_agent_cli inspect-tape \
  --tape ../../store/tipograf/eng_small.vsb \
  --bake ../../store/tipograf/tipograf_v01.d8p \
  --labels ../../store/tipograf/tipograf_labels.tsv \
  --tape-format packed4 \
  --reset-on-winner \
  --json tipograf_tape.json \
  --html tipograf_tape.html
```

JSON shape:

```json
{
  "tape": "eng_small.vsb",
  "bake": "tipograf_v01.d8p",
  "format": "packed4",
  "frames": 120,
  "segments": [
    {
      "index": 0,
      "start": 0,
      "end": 6,
      "label": "1 p1/t64/d0",
      "pattern_id": 1,
      "symbol": "1",
      "frames": [
        {"frame":0,"lanes":[1,1,1,1,1,1,1,1],"fired":""}
      ]
    }
  ]
}
```

`--labels` accepts a simple text dictionary with one mapping per line:

```text
p1	1
p10	0
p20	A
p45	Z
```

Without `--bake`, the command still decodes the tape and writes one unresolved segment with raw Level16 lanes. With `--bake`, each segment is the input slice that led to a non-zero `pattern_id`.

### `bake-basic`

Generate a simple bake file for smoke tests or baseline search.

```bash
d8_agent_cli bake-basic --out personality.d8p --tiles 256 --thr-lo 1 --thr-hi 1000 --decay 0
```

Output:

```json
{"ok":true,"out":"personality.d8p","bytes":14720,"tiles":256}
```

### `run`

Run a bake against a deterministic VSB tape.

```bash
d8_agent_cli run --bake personality.d8p --tape input.vsb --jsonl result.jsonl --tape-format auto
```

Options:

- `--limit N`: process only first `N` frames.
- `--reset MASK`: apply domain reset mask before every frame. Mostly useful for independent samples.
- `--reset-on-winner`: after a frame emits at least one fired non-zero `pattern_id`, reset all domains before the next frame. This matches the native IDE/conductor OCR flow where a detected `pattern_id` closes the current glyph.
- `--reset-every-frames N`: reset all domains before every Nth frame. This is
  intended for controlled memory-policy experiments; `0` or an omitted option
  keeps continuous memory.
- `--jsonl -`: write JSONL to stdout.
- `--html report.html`: write a self-contained human report with colored ingress/readout lanes, fired patterns, winners, and summary.
- `--tape-format`: `raw8`, `packed4`, or `auto`.

Tipograf OCR example:

```bash
d8_agent_cli run \
  --bake ../../store/tipograf/tipograf_v01.d8p \
  --tape ../../store/tipograf/eng_small.vsb \
  --tape-format packed4 \
  --reset-on-winner \
  --jsonl tipograf.jsonl \
  --html tipograf.html
```

Each frame emits one JSON object:

```json
{
  "frame": 0,
  "status": 0,
  "ingress": [1,1,1,1,1,1,1,1],
  "readout": [0,0,0,0,0,0,0,0],
  "flags32": 1,
  "bus_clip": 0,
  "collide_mask16": 0,
  "auto_reset_mask16": 0,
  "winner_domain_mask16": 1,
  "cycle_us": 30,
  "fired_patterns": [
    {"domain":0,"tile":17,"pattern":42}
  ],
  "winners": [
    {"domain":0,"tile":17,"pattern":42,"priority":7,"collision":false}
  ]
}
```

Last line is a summary:

```json
{"summary":{"frames":100,"ok":100,"errors":0,"active_tiles":256,"fired_patterns":45}}
```

## Agent Training Loop

Minimal loop:

```text
agent generates candidate bake
agent writes training tape
d8_agent_cli run emits JSONL
agent scores winners/readout/collisions
agent edits bake plan
agent rebakes
repeat
```

Near-term missing command:

`d8_agent_cli patch-bake --base base.d8p --patch patch.json --out edited.d8p`

`d8_agent_cli train-personality --dataset dataset.jsonl --base base.d8p --out trained.d8p`

Recommended dataset record:

```json
{
  "id": "sample-001",
  "vsb": [[1,2,3,4,5,6,7,8], [2,3,4,5,6,7,8,9]],
  "expect": {"domain": 0, "pattern": 42}
}
```

The first implementation should not pretend to be ML. It should be a deterministic baker/searcher:

- assign pattern IDs
- set thresholds/routing/weights
- run evaluation
- choose the smallest bake that passes the dataset

## Relationship to Native Server

The CLI and native REST/WSS server should share the same backend functions:

- load/apply bake
- run tape
- serialize result events
- export active bake

The CLI is for agents and CI. The server is for humans, orchestration, and live machine control.
