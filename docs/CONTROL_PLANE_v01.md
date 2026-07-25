# DECIMA-8 Control Plane v0.1

Status: draft for native machine daemon.

## 0. Split

DECIMA-8 networked machines have two planes:

- Data plane: UDP VSB frames and solution packets. This remains the fast cascade path.
- Control plane: native HTTP REST + WSS server owned by the machine daemon. This controls bake lifecycle, reset, topology, telemetry and UI state.

The control plane must not carry high-rate VSB frames. It may expose snapshots for UI, but execution traffic stays on UDP.

## 1. Machine Roles

- Conductor: REST/WSS client that owns orchestration, uploads bake blobs, starts/stops machines, assigns cascade links.
- Machine daemon: native process around `d8_swarm_t`; receives UDP VSB, exposes REST/WSS, applies bake atomically between flashes.
- React UI: browser client served by the daemon or a separate static server. It uses REST for commands and WSS for live state.

## 2. Bake Transaction

Remote rebake is a transaction, not a file replacement.

States:

- `EMPTY`: no active bake.
- `ACTIVE`: active bake exists and UDP flash is allowed.
- `STAGING`: a new bake blob is uploaded but not active.
- `BAKING`: daemon validates and applies staging; UDP flash is paused.
- `FAILED`: active bake is unchanged, failure is reported.

Rules:

- `POST /api/v1/bake/staging` writes staging bytes only.
- `POST /api/v1/bake/apply` validates staging and applies it atomically.
- On failure, active bake remains unchanged.
- During `BAKING`, UDP packet handling must return/drop with `BadPhase` semantics rather than mutating swarm state.
- Successful apply resets runtime according to `EV_BAKE`.

## 3. REST API

All responses are JSON except raw bake download.

### Health and State

`GET /api/v1/health`

```json
{
  "ok": true,
  "version": "0.1",
  "state": "ACTIVE"
}
```

`GET /api/v1/status`

```json
{
  "state": "ACTIVE",
  "bake_id": 1,
  "profile_id": 1,
  "active_tile_count": 256,
  "frame_tag": 123,
  "frames_processed": 1000,
  "solutions_sent": 12,
  "udp": {
    "recv_port": 9901,
    "send_host": "127.0.0.1",
    "send_port": 9902
  }
}
```

### Bake

`POST /api/v1/bake/staging`

Body: raw `.d8p` bytes.

Response:

```json
{
  "ok": true,
  "staging_size": 233600,
  "crc32": "0x12345678"
}
```

`POST /api/v1/bake/apply`

Response:

```json
{
  "ok": true,
  "bake_id": 7,
  "profile_id": 1,
  "active_tile_count": 256
}
```

`GET /api/v1/bake/active`

Body: raw active `.d8p` bytes.

`DELETE /api/v1/bake/staging`

Drops staging bytes.

### Runtime

`POST /api/v1/runtime/reset`

Body:

```json
{ "domain_mask16": "0xffff" }
```

`POST /api/v1/runtime/flash`

Manual debug flash. Not for high-rate cascade.

Body:

```json
{ "frame_tag": 123, "vsb": [0, 1, 2, 3, 4, 5, 6, 7] }
```

### UDP Cascade

`GET /api/v1/cascade`

`PUT /api/v1/cascade`

Body:

```json
{
  "recv_port": 9901,
  "send_host": "127.0.0.1",
  "send_port": 9902,
  "reset_on_solution": false
}
```

Changing cascade config must stop and restart UDP sockets cleanly.

## 4. WSS API

Endpoint: `GET /api/v1/events`

Server sends JSON events. Client commands should use REST unless a later version needs low-latency UI commands.

Events:

```json
{
  "type": "status",
  "state": "ACTIVE",
  "frame_tag": 123,
  "bake_id": 7
}
```

```json
{
  "type": "snapshot",
  "frame_tag": 123,
  "bus16": [0, 0, 15, 0, 0, 0, 0, 0],
  "flags32_last": 1,
  "collide_mask16": 0,
  "auto_reset_mask16": 0,
  "cycle_time_us": 240
}
```

```json
{
  "type": "solution",
  "frame_tag": 123,
  "domain_id": 0,
  "winner_tile_id": 17,
  "pattern_id": 42,
  "collision": false
}
```

```json
{
  "type": "bake",
  "phase": "applied",
  "bake_id": 7,
  "profile_id": 1
}
```

## 5. Native Daemon Shape

Recommended binary:

`d8_machine_server`

Responsibilities:

- owns one `d8_swarm_t`
- loads optional boot bake
- owns UDP receiver/sender for cascade
- exposes REST/WSS
- serves React static assets in production builds
- serializes mutating operations through one machine mutex or single event loop

Initial implementation can reuse `net_solver` internals, but `net_solver` should become the UDP worker inside the daemon, not the final process shape.

## 6. Implementation Notes

- Keep `d8_udp_packet_t` as v1 data-plane packet.
- Do not extend the 37-byte UDP packet for remote bake; bake blobs are too large and need transaction semantics.
- REST upload must validate max size before allocating.
- WSS snapshot rate should be throttled independently from flash rate.
- Later hardware mode can swap the local `d8_swarm_t` backend for a device backend while keeping the same REST/WSS API.
