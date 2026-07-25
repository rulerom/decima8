/*
 * DECIMA-8 Agent CLI
 *
 * Machine-readable command line surface for AI agents and batch training loops.
 */

#include <errno.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "d8/bake.h"
#include "d8/swarm.h"
#include "bake_gen.h"

static void print_usage(const char* prog) {
    fprintf(stderr,
        "Usage:\n"
        "  %s run --bake file.d8p --tape input.vsb [--jsonl out.jsonl] [--html report.html] [--limit N] [--reset MASK] [--reset-on-winner] [--reset-every-frames N] [--events-only] [--trace-tile N] [--trace-every N] [--tape-format raw8|packed4|auto]\n"
        "  %s inspect-bake --bake file.d8p [--json out.json|-] [--html report.html]\n"
        "  %s inspect-tape --tape input.vsb [--bake file.d8p] [--labels labels.tsv] [--json out.json|-] [--html report.html] [--limit N] [--reset-on-winner] [--tape-format raw8|packed4|auto]\n"
        "  %s bake-basic --out file.d8p [--tiles N] [--thr-lo N] [--thr-hi N] [--decay N]\n"
        "\n"
        "VSB tape formats: raw8 = 8 bytes/frame, packed4 = 4 bytes/frame with two nibbles/byte.\n",
        prog, prog, prog, prog);
}

static int read_file(const char* path, uint8_t** out_data, size_t* out_size) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "failed to open %s: %s\n", path, strerror(errno));
        return -1;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    long len = ftell(f);
    if (len < 0) {
        fclose(f);
        return -1;
    }
    rewind(f);

    uint8_t* data = (uint8_t*)malloc((size_t)len);
    if (!data && len > 0) {
        fclose(f);
        return -1;
    }

    size_t got = fread(data, 1, (size_t)len, f);
    fclose(f);
    if (got != (size_t)len) {
        free(data);
        return -1;
    }

    *out_data = data;
    *out_size = (size_t)len;
    return 0;
}

static int write_file(const char* path, const uint8_t* data, size_t size) {
    FILE* f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "failed to open %s: %s\n", path, strerror(errno));
        return -1;
    }

    size_t written = fwrite(data, 1, size, f);
    fclose(f);
    return written == size ? 0 : -1;
}

typedef struct d8_agent_label_map {
    char labels[32768][32];
} d8_agent_label_map_t;

static char* trim_ascii(char* s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char* end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) {
        end--;
        *end = '\0';
    }
    return s;
}

static int load_label_map(const char* path, d8_agent_label_map_t* map) {
    if (!path || !map) return 0;

    uint8_t* data = NULL;
    size_t size = 0;
    if (read_file(path, &data, &size) != 0) return -1;

    char* text = (char*)malloc(size + 1);
    if (!text) {
        free(data);
        return -1;
    }
    memcpy(text, data, size);
    text[size] = '\0';
    free(data);

    char* line = text;
    while (line && *line) {
        char* next = strchr(line, '\n');
        if (next) {
            *next = '\0';
            next++;
        }

        char* s = trim_ascii(line);
        if (*s != '\0' && *s != '#') {
            char* sep = strpbrk(s, "\t=,");
            if (sep) {
                *sep = '\0';
                char* id_text = trim_ascii(s);
                char* label = trim_ascii(sep + 1);
                if (id_text[0] == 'p' || id_text[0] == 'P') id_text++;
                char* end = NULL;
                unsigned long id = strtoul(id_text, &end, 10);
                if (end && *trim_ascii(end) == '\0' && id < 32768 && label[0] != '\0') {
                    snprintf(map->labels[id], sizeof(map->labels[id]), "%s", label);
                }
            }
        }
        line = next;
    }

    free(text);
    return 0;
}

static const char* lookup_label(const d8_agent_label_map_t* map, uint16_t pattern_id) {
    if (!map || pattern_id >= 32768 || map->labels[pattern_id][0] == '\0') return NULL;
    return map->labels[pattern_id];
}

static const char* arg_value(int argc, char** argv, const char* name) {
    for (int i = 2; i + 1 < argc; i++) {
        if (strcmp(argv[i], name) == 0) return argv[i + 1];
    }
    return NULL;
}

static int has_arg(int argc, char** argv, const char* name) {
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], name) == 0) return 1;
    }
    return 0;
}

static uint32_t parse_u32_or(const char* s, uint32_t fallback) {
    if (!s) return fallback;
    char* end = NULL;
    unsigned long v = strtoul(s, &end, 0);
    if (!end || *end != '\0') return fallback;
    return (uint32_t)v;
}

static int32_t parse_i32_or(const char* s, int32_t fallback) {
    if (!s) return fallback;
    char* end = NULL;
    long v = strtol(s, &end, 0);
    if (!end || *end != '\0') return fallback;
    return (int32_t)v;
}

static void print_json_array8(FILE* out, const uint8_t values[8]) {
    fputc('[', out);
    for (int i = 0; i < 8; i++) {
        if (i) fputc(',', out);
        fprintf(out, "%u", (unsigned)values[i]);
    }
    fputc(']', out);
}

static const char* html_lane_color(uint8_t value) {
    static const char* colors[16] = {
        "#1c2630", "#243445", "#2a4660", "#315a7b",
        "#3a6f8e", "#438390", "#519674", "#70a65b",
        "#9ab357", "#c1b85c", "#d6a75e", "#de8d5d",
        "#d66e63", "#c94f6a", "#a94178", "#7f3f86"
    };
    return colors[value & 0x0F];
}

static void print_html_lanes(FILE* out, const uint8_t values[8]) {
    for (int i = 0; i < 8; i++) {
        fprintf(out,
            "<span style=\"background:%s\">%u</span>",
            html_lane_color(values[i]),
            (unsigned)values[i]);
    }
}

static void print_json_string(FILE* out, const char* s) {
    fputc('"', out);
    for (const unsigned char* p = (const unsigned char*)s; p && *p; p++) {
        if (*p == '"' || *p == '\\') {
            fputc('\\', out);
            fputc(*p, out);
        } else if (*p == '\n') {
            fputs("\\n", out);
        } else if (*p == '\r') {
            fputs("\\r", out);
        } else if (*p == '\t') {
            fputs("\\t", out);
        } else {
            fputc(*p, out);
        }
    }
    fputc('"', out);
}

static void print_routing_json(FILE* out, const d8_bake_view_t* view, size_t t) {
    fprintf(out,
        "{\"N\":%u,\"E\":%u,\"S\":%u,\"W\":%u,\"NE\":%u,\"SE\":%u,\"SW\":%u,\"NW\":%u,\"bus_w\":%u,\"bus_r\":%u}",
        (unsigned)view->maskN[t],
        (unsigned)view->maskE[t],
        (unsigned)view->maskS[t],
        (unsigned)view->maskW[t],
        (unsigned)view->maskNE[t],
        (unsigned)view->maskSE[t],
        (unsigned)view->maskSW[t],
        (unsigned)view->maskNW[t],
        (unsigned)view->bus_w[t],
        (unsigned)view->bus_r[t]);
}

static void print_weights_json(FILE* out, const d8_bake_view_t* view, size_t t) {
    size_t base = t * 64;
    unsigned nonzero = 0;
    unsigned positive = 0;
    unsigned negative = 0;
    unsigned mag_sum = 0;

    for (size_t i = 0; i < 64; i++) {
        uint8_t mag = view->w_mag[base + i];
        if (mag == 0) continue;
        nonzero++;
        mag_sum += mag;
        if (view->w_sign[base + i]) negative++;
        else positive++;
    }

    fprintf(out,
        "{\"nonzero\":%u,\"positive\":%u,\"negative\":%u,\"mag_sum\":%u,\"values\":[",
        nonzero,
        positive,
        negative,
        mag_sum);
    for (size_t i = 0; i < 64; i++) {
        uint8_t mag = view->w_mag[base + i] & 0x0F;
        int signed_mag = view->w_sign[base + i] ? -(int)mag : (int)mag;
        if (i) fputc(',', out);
        fprintf(out, "%d", signed_mag);
    }
    fputs("]}", out);
}

static void print_routing_text(FILE* out, const d8_bake_view_t* view, size_t t) {
    int first = 1;
#define D8_PRINT_ROUTE(name, value) do { if (value) { if (!first) fputc(',', out); first = 0; fputs(name, out); } } while (0)
    D8_PRINT_ROUTE("N", view->maskN[t]);
    D8_PRINT_ROUTE("E", view->maskE[t]);
    D8_PRINT_ROUTE("S", view->maskS[t]);
    D8_PRINT_ROUTE("W", view->maskW[t]);
    D8_PRINT_ROUTE("NE", view->maskNE[t]);
    D8_PRINT_ROUTE("SE", view->maskSE[t]);
    D8_PRINT_ROUTE("SW", view->maskSW[t]);
    D8_PRINT_ROUTE("NW", view->maskNW[t]);
    D8_PRINT_ROUTE("bus_w", view->bus_w[t]);
    D8_PRINT_ROUTE("bus_r", view->bus_r[t]);
#undef D8_PRINT_ROUTE
    if (first) fputc('-', out);
}

static const char* html_tile_color(uint16_t pattern_id, uint8_t domain_id) {
    static const char* patterned[16] = {
        "#4f87ff", "#31a985", "#d1a239", "#d86876",
        "#9a7cf0", "#58a9c9", "#86a83d", "#ca7b43",
        "#3fa2a0", "#b889e5", "#b9a142", "#e06a9a",
        "#6f94d8", "#62a35b", "#d7895d", "#9b8edb"
    };
    static const char* domains[16] = {
        "#263344", "#263d38", "#403623", "#422d33",
        "#332f47", "#253d45", "#344023", "#412f24",
        "#263f3e", "#3d3046", "#403b25", "#472d3a",
        "#2f3748", "#303f2b", "#432f25", "#393447"
    };
    if (pattern_id != 0) return patterned[pattern_id & 0x0F];
    return domains[domain_id & 0x0F];
}

static void effective_tile_dims(const d8_bake_view_t* view, unsigned* out_w, unsigned* out_h) {
    size_t count = view->tile_count;
    unsigned w = view->topo.tile_w;
    unsigned h = view->topo.tile_h;

    switch (count) {
        case 256:  w = 32;  h = 8;  break;
        case 512:  w = 32;  h = 16; break;
        case 1024: w = 64;  h = 16; break;
        case 2048: w = 64;  h = 32; break;
        case 4096: w = 128; h = 32; break;
        default:
            if (w == 0 || h == 0 || (size_t)w * (size_t)h < count) {
                w = count < D8_K_EXPECTED_W ? (unsigned)count : D8_K_EXPECTED_W;
                h = (unsigned)((count + w - 1) / w);
            }
            break;
    }

    if (w == 0) w = 1;
    if (h == 0) h = 1;
    *out_w = w;
    *out_h = h;
}

static void write_inspect_json(FILE* out, const d8_bake_view_t* view, const char* bake_path) {
    size_t active = view->tile_count;
    if (active > D8_K_TILE_COUNT) active = D8_K_TILE_COUNT;
    unsigned eff_w = 1;
    unsigned eff_h = 1;
    effective_tile_dims(view, &eff_w, &eff_h);

    size_t pattern_tiles = 0;
    size_t bus_w_tiles = 0;
    size_t bus_r_tiles = 0;
    uint32_t domain_mask = 0;
    for (size_t t = 0; t < active; t++) {
        if (view->pattern_id[t] != 0) pattern_tiles++;
        if (view->bus_w[t]) bus_w_tiles++;
        if (view->bus_r[t]) bus_r_tiles++;
        if (view->domain_id[t] < 16) domain_mask |= (uint32_t)(1u << view->domain_id[t]);
    }

    fprintf(out,
        "{\"bake\":\"%s\",\"bake_id\":%u,\"profile_id\":%u,\"flags\":%u,"
        "\"topology\":{\"tile_count\":%zu,\"tile_w\":%u,\"tile_h\":%u,\"lanes\":%u,\"domains\":%u},"
        "\"effective_layout\":{\"tile_w\":%u,\"tile_h\":%u},"
        "\"readout\":{\"mode\":%u,\"winner_domain_mask\":%u,\"settle_ns\":%u},"
        "\"summary\":{\"pattern_tiles\":%zu,\"bus_w_tiles\":%zu,\"bus_r_tiles\":%zu,\"domain_mask16\":%u},"
        "\"tiles\":[",
        bake_path,
        (unsigned)view->bake_id,
        (unsigned)view->profile_id,
        (unsigned)view->bake_flags,
        active,
        (unsigned)view->topo.tile_w,
        (unsigned)view->topo.tile_h,
        (unsigned)view->topo.lanes,
        (unsigned)view->topo.domains,
        eff_w,
        eff_h,
        (unsigned)view->readout.mode,
        (unsigned)view->readout.winner_domain_mask,
        (unsigned)view->readout.settle_ns,
        pattern_tiles,
        bus_w_tiles,
        bus_r_tiles,
        (unsigned)domain_mask);

    for (size_t t = 0; t < active; t++) {
        if (t) fputc(',', out);
        fprintf(out,
            "{\"tile\":%zu,\"x\":%zu,\"y\":%zu,\"domain\":%u,\"priority\":%u,"
            "\"pattern\":%u,\"thr_lo\":%d,\"thr_hi\":%d,\"decay16\":%u,"
            "\"reset_on_fire_mask16\":%u,\"routing\":",
            t,
            t % eff_w,
            t / eff_w,
            (unsigned)view->domain_id[t],
            (unsigned)view->priority[t],
            (unsigned)view->pattern_id[t],
            (int)view->thr_lo[t],
            (int)view->thr_hi[t],
            (unsigned)view->decay16[t],
            (unsigned)view->reset_on_fire_mask16[t]);
        print_routing_json(out, view, t);
        fputs(",\"weights\":", out);
        print_weights_json(out, view, t);
        fputc('}', out);
    }
    fputs("]}\n", out);
}

static void write_inspect_html(FILE* out, const d8_bake_view_t* view, const char* bake_path) {
    size_t active = view->tile_count;
    if (active > D8_K_TILE_COUNT) active = D8_K_TILE_COUNT;
    unsigned eff_w = 1;
    unsigned eff_h = 1;
    effective_tile_dims(view, &eff_w, &eff_h);

    size_t pattern_tiles = 0;
    size_t bus_w_tiles = 0;
    size_t bus_r_tiles = 0;
    for (size_t t = 0; t < active; t++) {
        if (view->pattern_id[t] != 0) pattern_tiles++;
        if (view->bus_w[t]) bus_w_tiles++;
        if (view->bus_r[t]) bus_r_tiles++;
    }

    fprintf(out,
        "<!doctype html><html><head><meta charset=\"utf-8\"><title>DECIMA-8 Bake Inspect</title>"
        "<style>"
        ":root{color-scheme:dark;--bg:#101316;--panel:#171d22;--ink:#edf2f5;--muted:#94a3ad;--line:#2d3942;--cyan:#45c7dd;--gold:#e2b84c;}"
        "body{margin:0;background:var(--bg);color:var(--ink);font-family:-apple-system,BlinkMacSystemFont,Segoe UI,sans-serif;}"
        "main{max-width:1440px;margin:0 auto;padding:28px;}h1{font-size:28px;margin:0 0 6px;}h2{font-size:17px;margin:26px 0 12px;}.muted{color:var(--muted)}"
        ".metrics{display:grid;grid-template-columns:repeat(auto-fit,minmax(160px,1fr));gap:10px;margin:18px 0 24px;}.metric{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:12px;}.metric b{display:block;font-size:22px;margin-top:4px;}"
        ".panel{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:14px;overflow:auto;}"
        ".swarm{display:grid;grid-template-columns:repeat(%u,24px);gap:3px;align-items:center;}.tile{width:24px;height:24px;box-sizing:border-box;border:1px solid rgba(255,255,255,.12);font-size:9px;line-height:22px;text-align:center;color:#fff;font-variant-numeric:tabular-nums;overflow:hidden;opacity:.32;}"
        ".live{opacity:.7}.pattern{font-weight:700;opacity:1}.busw{border-top-color:var(--gold);border-top-width:4px}.busr{border-bottom-color:var(--cyan);border-bottom-width:4px}"
        ".legend{display:flex;gap:12px;flex-wrap:wrap;margin-top:12px;color:var(--muted);font-size:12px}.mark{display:inline-block;width:18px;height:10px;border:1px solid rgba(255,255,255,.2);vertical-align:middle;margin-right:5px}.mark.pattern{background:#4f87ff}.mark.busw{border-top:4px solid var(--gold)}.mark.busr{border-bottom:4px solid var(--cyan)}"
        "table{width:100%%;border-collapse:collapse;font-size:12px;}th,td{border-bottom:1px solid var(--line);padding:7px 8px;text-align:right;white-space:nowrap;}th:first-child,td:first-child{text-align:left;}.left{text-align:left}"
        "</style></head><body><main>"
        "<h1>DECIMA-8 Bake Inspect</h1><div class=\"muted\">%s</div>"
        "<section class=\"metrics\">"
        "<div class=\"metric\"><span class=\"muted\">Tiles</span><b>%zu</b></div>"
        "<div class=\"metric\"><span class=\"muted\">Layout</span><b>%ux%u</b></div>"
        "<div class=\"metric\"><span class=\"muted\">Raw Topology</span><b>%ux%u</b></div>"
        "<div class=\"metric\"><span class=\"muted\">Pattern Tiles</span><b>%zu</b></div>"
        "<div class=\"metric\"><span class=\"muted\">Bus W / R</span><b>%zu / %zu</b></div>"
        "<div class=\"metric\"><span class=\"muted\">Bake / Profile</span><b>%u / %u</b></div>"
        "</section><h2>Swarm Grid</h2><section class=\"panel\"><div class=\"swarm\">",
        eff_w,
        bake_path,
        active,
        eff_w,
        eff_h,
        (unsigned)view->topo.tile_w,
        (unsigned)view->topo.tile_h,
        pattern_tiles,
        bus_w_tiles,
        bus_r_tiles,
        (unsigned)view->bake_id,
        (unsigned)view->profile_id);

    for (size_t t = 0; t < active; t++) {
        fprintf(out,
            "<div class=\"tile%s%s%s%s\" style=\"background:%s\" title=\"t%zu x%zu y%zu d%u p%u thr %d..%d decay %u route ",
            (view->thr_lo[t] != 0 || view->thr_hi[t] != 0 || view->pattern_id[t] || view->bus_w[t] || view->bus_r[t]) ? " live" : "",
            view->pattern_id[t] ? " pattern" : "",
            view->bus_w[t] ? " busw" : "",
            view->bus_r[t] ? " busr" : "",
            html_tile_color(view->pattern_id[t], view->domain_id[t]),
            t,
            t % eff_w,
            t / eff_w,
            (unsigned)view->domain_id[t],
            (unsigned)view->pattern_id[t],
            (int)view->thr_lo[t],
            (int)view->thr_hi[t],
            (unsigned)view->decay16[t]);
        print_routing_text(out, view, t);
        fprintf(out, "\">%s%u</div>", view->pattern_id[t] ? "p" : "", (unsigned)(view->pattern_id[t] ? view->pattern_id[t] : view->domain_id[t]));
    }

    fputs("</div><div class=\"legend\"><span><i class=\"mark pattern\"></i>pattern_id</span><span><i class=\"mark busw\"></i>bus writer</span><span><i class=\"mark busr\"></i>bus reader</span></div></section><h2>Pattern Tiles</h2><section class=\"panel\"><table><thead><tr>"
          "<th>tile</th><th>x</th><th>y</th><th>pattern</th><th>domain</th><th>priority</th><th>thr</th><th>decay</th><th>reset</th><th class=\"left\">routing</th>"
          "</tr></thead><tbody>", out);
    for (size_t t = 0; t < active; t++) {
        if (view->pattern_id[t] == 0) continue;
        fprintf(out,
            "<tr><td>t%zu</td><td>%zu</td><td>%zu</td><td>p%u</td><td>d%u</td><td>%u</td><td>%d..%d</td><td>%u</td><td>%u</td><td class=\"left\">",
            t,
            t % eff_w,
            t / eff_w,
            (unsigned)view->pattern_id[t],
            (unsigned)view->domain_id[t],
            (unsigned)view->priority[t],
            (int)view->thr_lo[t],
            (int)view->thr_hi[t],
            (unsigned)view->decay16[t],
            (unsigned)view->reset_on_fire_mask16[t]);
        print_routing_text(out, view, t);
        fputs("</td></tr>", out);
    }
    fputs("</tbody></table></section></main></body></html>\n", out);
}

static int decode_tape_frame(const uint8_t* tape_data, size_t frame, int packed4, uint8_t out[8]) {
    if (packed4) {
        const uint8_t* p = tape_data + frame * 4;
        out[0] = p[0] & 0x0F;
        out[1] = (p[0] >> 4) & 0x0F;
        out[2] = p[1] & 0x0F;
        out[3] = (p[1] >> 4) & 0x0F;
        out[4] = p[2] & 0x0F;
        out[5] = (p[2] >> 4) & 0x0F;
        out[6] = p[3] & 0x0F;
        out[7] = (p[3] >> 4) & 0x0F;
        return 0;
    }

    const uint8_t* p = tape_data + frame * D8_K_LANES;
    for (int i = 0; i < 8; i++) {
        if (p[i] > 15) return -1;
        out[i] = p[i];
    }
    return 0;
}

static void print_winners_json(FILE* out, const d8_view_snapshot_t* snap) {
    fputc('[', out);
    int first = 1;
    for (int d = 0; d < (int)D8_K_DOMAINS; d++) {
        if (snap->winner_tile_id[d] == 0xFFFF) continue;
        if (!first) fputc(',', out);
        first = 0;
        fprintf(out,
            "{\"domain\":%d,\"tile\":%u,\"pattern\":%u,\"priority\":%u,\"collision\":%s}",
            d,
            (unsigned)snap->winner_tile_id[d],
            (unsigned)snap->winner_pattern_id[d],
            (unsigned)snap->winner_priority[d],
            (snap->collide_mask16 & (uint16_t)(1u << d)) ? "true" : "false");
    }
    fputc(']', out);
}

static uint16_t winner_domain_mask(const d8_view_snapshot_t* snap) {
    uint16_t mask = 0;
    for (int d = 0; d < (int)D8_K_DOMAINS; d++) {
        if (snap->winner_tile_id[d] != 0xFFFF) {
            mask |= (uint16_t)(1u << d);
        }
    }
    return mask;
}

static size_t count_fired_patterns(d8_swarm_t* swarm, const d8_view_snapshot_t* snap) {
    if (!swarm || !snap) return 0;

    size_t count = 0;
    size_t active = d8_swarm_get_active_tile_count(swarm);
    if (active > D8_K_TILE_COUNT) active = D8_K_TILE_COUNT;
    for (size_t t = 0; t < active; t++) {
        if ((snap->v_state[t] & 0x04) == 0) continue;
        if (d8_swarm_get_tile_pattern_id_direct(swarm, t) == 0) continue;
        count++;
    }
    return count;
}

static void print_fired_patterns_json(FILE* out, d8_swarm_t* swarm, const d8_view_snapshot_t* snap) {
    fputc('[', out);
    if (!swarm || !snap) {
        fputc(']', out);
        return;
    }

    int first = 1;
    size_t active = d8_swarm_get_active_tile_count(swarm);
    if (active > D8_K_TILE_COUNT) active = D8_K_TILE_COUNT;
    for (size_t t = 0; t < active; t++) {
        if ((snap->v_state[t] & 0x04) == 0) continue;
        d8_u16 pattern = d8_swarm_get_tile_pattern_id_direct(swarm, t);
        if (pattern == 0) continue;

        if (!first) fputc(',', out);
        first = 0;
        fprintf(out,
            "{\"domain\":%u,\"tile\":%zu,\"pattern\":%u}",
            (unsigned)d8_swarm_get_tile_domain_id_direct(swarm, t),
            t,
            (unsigned)pattern);
    }
    fputc(']', out);
}

static void print_fired_patterns_html(FILE* out, d8_swarm_t* swarm, const d8_view_snapshot_t* snap) {
    if (!swarm || !snap) return;

    int first = 1;
    size_t active = d8_swarm_get_active_tile_count(swarm);
    if (active > D8_K_TILE_COUNT) active = D8_K_TILE_COUNT;
    for (size_t t = 0; t < active; t++) {
        if ((snap->v_state[t] & 0x04) == 0) continue;
        d8_u16 pattern = d8_swarm_get_tile_pattern_id_direct(swarm, t);
        if (pattern == 0) continue;
        if (!first) fputs(", ", out);
        first = 0;
        fprintf(out, "p%u/t%zu/d%u",
            (unsigned)pattern,
            t,
            (unsigned)d8_swarm_get_tile_domain_id_direct(swarm, t));
    }
}

static void print_winners_html(FILE* out, const d8_view_snapshot_t* snap) {
    int first = 1;
    for (int d = 0; d < (int)D8_K_DOMAINS; d++) {
        if (snap->winner_tile_id[d] == 0xFFFF) continue;
        if (!first) fputs(", ", out);
        first = 0;
        fprintf(out, "p%u/t%u/d%d",
            (unsigned)snap->winner_pattern_id[d],
            (unsigned)snap->winner_tile_id[d],
            d);
    }
}

typedef struct d8_agent_tape_segment {
    size_t start_frame;
    size_t end_frame;
    char label[128];
} d8_agent_tape_segment_t;

static void collect_fired_patterns_text(d8_swarm_t* swarm, const d8_view_snapshot_t* snap, char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!swarm || !snap) return;

    size_t used = 0;
    int first = 1;
    size_t active = d8_swarm_get_active_tile_count(swarm);
    if (active > D8_K_TILE_COUNT) active = D8_K_TILE_COUNT;
    for (size_t t = 0; t < active; t++) {
        if ((snap->v_state[t] & 0x04) == 0) continue;
        d8_u16 pattern = d8_swarm_get_tile_pattern_id_direct(swarm, t);
        if (pattern == 0) continue;
        int wrote = snprintf(out + used, out_size - used, "%sp%u/t%zu/d%u",
            first ? "" : ", ",
            (unsigned)pattern,
            t,
            (unsigned)d8_swarm_get_tile_domain_id_direct(swarm, t));
        if (wrote < 0) break;
        if ((size_t)wrote >= out_size - used) {
            out[out_size - 1] = '\0';
            break;
        }
        used += (size_t)wrote;
        first = 0;
    }
}

static uint16_t first_pattern_from_label(const char* label) {
    if (!label) return 0;
    const char* p = label;
    while (*p) {
        if (*p == 'p' || *p == 'P') {
            char* end = NULL;
            unsigned long id = strtoul(p + 1, &end, 10);
            if (end != p + 1 && id > 0 && id < 32768) return (uint16_t)id;
        }
        p++;
    }
    return 0;
}

static void write_tape_html_header(FILE* out,
                                   const char* tape_path,
                                   const char* bake_path,
                                   size_t frame_count,
                                   int packed4,
                                   int reset_on_winner) {
    fprintf(out,
        "<!doctype html><html><head><meta charset=\"utf-8\"><title>DECIMA-8 Tape Inspect</title>"
        "<style>"
        ":root{color-scheme:dark;--bg:#101316;--panel:#171d22;--ink:#edf2f5;--muted:#94a3ad;--line:#2d3942;}"
        "body{margin:0;background:var(--bg);color:var(--ink);font-family:-apple-system,BlinkMacSystemFont,Segoe UI,sans-serif;}"
        "main{max-width:1280px;margin:0 auto;padding:28px;}h1{font-size:28px;margin:0 0 6px;}h2{font-size:17px;margin:26px 0 12px;}.muted{color:var(--muted)}"
        ".metrics{display:grid;grid-template-columns:repeat(auto-fit,minmax(160px,1fr));gap:10px;margin:18px 0 24px;}.metric{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:12px;}.metric b{display:block;font-size:22px;margin-top:4px;}"
        "details{background:var(--panel);border:1px solid var(--line);border-radius:8px;margin:10px 0;overflow:hidden;}summary{cursor:pointer;padding:11px 13px;font-weight:600;}summary span{color:var(--muted);font-weight:400;margin-left:8px;}"
        "table{width:100%%;border-collapse:collapse;font-size:12px;}td,th{border-top:1px solid var(--line);padding:6px 8px;text-align:right;}td:first-child,th:first-child{text-align:left;}.lanes{white-space:nowrap}.lanes span{display:inline-block;min-width:18px;margin-right:3px;border-radius:3px;text-align:center;color:#fff;font-variant-numeric:tabular-nums;}.text{text-align:left;}"
        "</style></head><body><main><h1>DECIMA-8 Tape Inspect</h1>"
        "<div class=\"muted\">%s%s%s | frames=%zu | tape=%s | reset-on-winner=%s</div>"
        "<section class=\"metrics\">"
        "<div class=\"metric\"><span class=\"muted\">Frames</span><b>%zu</b></div>"
        "<div class=\"metric\"><span class=\"muted\">Tape Format</span><b>%s</b></div>"
        "<div class=\"metric\"><span class=\"muted\">Bake</span><b>%s</b></div>"
        "</section><h2>Pattern Accordion</h2>\n",
        tape_path,
        bake_path ? " -> " : "",
        bake_path ? bake_path : "",
        frame_count,
        packed4 ? "packed4" : "raw8",
        reset_on_winner ? "yes" : "no",
        frame_count,
        packed4 ? "packed4" : "raw8",
        bake_path ? "yes" : "no");
}

static void write_tape_html_footer(FILE* out) {
    fputs("</main></body></html>\n", out);
}

static int cmd_inspect_tape(int argc, char** argv) {
    const char* tape_path = arg_value(argc, argv, "--tape");
    const char* bake_path = arg_value(argc, argv, "--bake");
    const char* labels_path = arg_value(argc, argv, "--labels");
    const char* json_path = arg_value(argc, argv, "--json");
    const char* html_path = arg_value(argc, argv, "--html");
    const char* tape_format = arg_value(argc, argv, "--tape-format");
    uint32_t limit = parse_u32_or(arg_value(argc, argv, "--limit"), 0);
    int reset_on_winner = has_arg(argc, argv, "--reset-on-winner");

    if (!tape_path || (!json_path && !html_path)) {
        print_usage(argv[0]);
        return 2;
    }

    d8_agent_label_map_t* labels = NULL;
    if (labels_path) {
        labels = (d8_agent_label_map_t*)calloc(1, sizeof(d8_agent_label_map_t));
        if (!labels || load_label_map(labels_path, labels) != 0) {
            fprintf(stderr, "failed to load labels: %s\n", labels_path);
            free(labels);
            return 1;
        }
    }

    uint8_t* tape_data = NULL;
    size_t tape_size = 0;
    if (read_file(tape_path, &tape_data, &tape_size) != 0) {
        free(labels);
        return 1;
    }

    int packed4 = 0;
    if (!tape_format || strcmp(tape_format, "auto") == 0) {
        packed4 = (tape_size % 8 != 0 && tape_size % 4 == 0) ? 1 : 0;
    } else if (strcmp(tape_format, "packed4") == 0) {
        packed4 = 1;
    } else if (strcmp(tape_format, "raw8") == 0) {
        packed4 = 0;
    } else {
        fprintf(stderr, "unknown tape format: %s\n", tape_format);
        free(labels);
        free(tape_data);
        return 1;
    }

    const size_t frame_bytes = packed4 ? 4 : D8_K_LANES;
    if (tape_size % frame_bytes != 0) {
        fprintf(stderr, "bad tape length: %zu is not divisible by %zu\n", tape_size, frame_bytes);
        free(labels);
        free(tape_data);
        return 1;
    }

    size_t frame_count = tape_size / frame_bytes;
    if (limit > 0 && limit < frame_count) frame_count = limit;

    uint8_t* bake_data = NULL;
    size_t bake_size = 0;
    d8_swarm_t* swarm = NULL;
    if (bake_path) {
        if (read_file(bake_path, &bake_data, &bake_size) != 0) {
            free(labels);
            free(tape_data);
            return 1;
        }
        swarm = d8_swarm_create();
        if (!swarm) {
            free(bake_data);
            free(labels);
            free(tape_data);
            return 1;
        }
        d8_status_t st = d8_swarm_ev_bake(swarm, bake_data, bake_size);
        if (st.code != D8_STATUS_OK) {
            fprintf(stderr, "bake failed: %d %s\n", (int)st.code, st.msg);
            d8_swarm_destroy(swarm);
            free(bake_data);
            free(labels);
            free(tape_data);
            return 1;
        }
    }

    d8_agent_tape_segment_t* segments = (d8_agent_tape_segment_t*)calloc(frame_count + 1, sizeof(d8_agent_tape_segment_t));
    uint8_t* lanes = (uint8_t*)calloc(frame_count * D8_K_LANES, 1);
    char (*fired_labels)[128] = (char (*)[128])calloc(frame_count ? frame_count : 1, sizeof(*fired_labels));
    if (!segments || !lanes || !fired_labels) {
        free(segments);
        free(lanes);
        free(fired_labels);
        if (swarm) d8_swarm_destroy(swarm);
        free(bake_data);
        free(labels);
        free(tape_data);
        return 1;
    }

    size_t segment_count = 0;
    size_t segment_start = 0;
    for (size_t frame = 0; frame < frame_count; frame++) {
        uint8_t* ingress = lanes + frame * D8_K_LANES;
        if (decode_tape_frame(tape_data, frame, packed4, ingress) != 0) {
            fprintf(stderr, "bad tape Level16 at frame %zu\n", frame);
            free(segments);
            free(lanes);
            free(fired_labels);
            if (swarm) d8_swarm_destroy(swarm);
            free(bake_data);
            free(labels);
            free(tape_data);
            return 1;
        }

        if (swarm) {
            d8_flash_result_t result = d8_swarm_ev_flash(swarm, (uint32_t)frame, ingress);
            if (result.st.code != D8_STATUS_OK) {
                snprintf(fired_labels[frame], sizeof(fired_labels[frame]), "error:%s", result.st.msg);
            } else {
                const d8_view_snapshot_t* snap = d8_swarm_get_snapshot(swarm);
                collect_fired_patterns_text(swarm, snap, fired_labels[frame], sizeof(fired_labels[frame]));
                if (fired_labels[frame][0] != '\0') {
                    segments[segment_count].start_frame = segment_start;
                    segments[segment_count].end_frame = frame;
                    uint16_t pattern_id = first_pattern_from_label(fired_labels[frame]);
                    const char* symbol = lookup_label(labels, pattern_id);
                    if (symbol) {
                        snprintf(segments[segment_count].label, sizeof(segments[segment_count].label), "%s %s", symbol, fired_labels[frame]);
                    } else {
                        snprintf(segments[segment_count].label, sizeof(segments[segment_count].label), "%s", fired_labels[frame]);
                    }
                    segment_count++;
                    segment_start = frame + 1;
                    if (reset_on_winner) d8_swarm_ev_reset_domain(swarm, 0xFFFF);
                }
            }
        }
    }
    if (segment_start < frame_count || segment_count == 0) {
        segments[segment_count].start_frame = segment_start;
        segments[segment_count].end_frame = frame_count ? frame_count - 1 : 0;
        snprintf(segments[segment_count].label, sizeof(segments[segment_count].label), "%s", "unresolved");
        segment_count++;
    }

    if (json_path) {
        FILE* out = stdout;
        if (strcmp(json_path, "-") != 0) {
            out = fopen(json_path, "wb");
            if (!out) {
                fprintf(stderr, "failed to open %s: %s\n", json_path, strerror(errno));
                free(segments);
                free(lanes);
                free(fired_labels);
                if (swarm) d8_swarm_destroy(swarm);
                free(bake_data);
                free(labels);
                free(tape_data);
                return 1;
            }
        }
        fputs("{\"tape\":", out);
        print_json_string(out, tape_path);
        fputs(",\"bake\":", out);
        if (bake_path) print_json_string(out, bake_path);
        else fputs("null", out);
        fprintf(out,
            ",\"format\":\"%s\",\"frames\":%zu,\"segments\":[",
            packed4 ? "packed4" : "raw8",
            frame_count);
        for (size_t s = 0; s < segment_count; s++) {
            if (s) fputc(',', out);
            fprintf(out, "{\"index\":%zu,\"start\":%zu,\"end\":%zu,\"label\":",
                s, segments[s].start_frame, segments[s].end_frame);
            print_json_string(out, segments[s].label);
            uint16_t pattern_id = first_pattern_from_label(segments[s].label);
            const char* symbol = lookup_label(labels, pattern_id);
            fprintf(out, ",\"pattern_id\":%u,\"symbol\":", (unsigned)pattern_id);
            if (symbol) print_json_string(out, symbol);
            else fputs("null", out);
            fputs(",\"frames\":[", out);
            for (size_t f = segments[s].start_frame; f <= segments[s].end_frame && f < frame_count; f++) {
                if (f > segments[s].start_frame) fputc(',', out);
                fprintf(out, "{\"frame\":%zu,\"lanes\":", f);
                print_json_array8(out, lanes + f * D8_K_LANES);
                fputs(",\"fired\":", out);
                print_json_string(out, fired_labels[f]);
                fputc('}', out);
            }
            fputs("]}", out);
        }
        fputs("]}\n", out);
        if (out != stdout) fclose(out);
    }

    if (html_path) {
        FILE* out = fopen(html_path, "wb");
        if (!out) {
            fprintf(stderr, "failed to open %s: %s\n", html_path, strerror(errno));
            free(segments);
            free(lanes);
            free(fired_labels);
            if (swarm) d8_swarm_destroy(swarm);
            free(bake_data);
            free(labels);
            free(tape_data);
            return 1;
        }
        write_tape_html_header(out, tape_path, bake_path, frame_count, packed4, reset_on_winner);
        for (size_t s = 0; s < segment_count; s++) {
            fprintf(out,
                "<details%s><summary>%s <span>frames %zu..%zu</span></summary><table><thead><tr><th>frame</th><th>lanes</th><th class=\"text\">fired</th></tr></thead><tbody>",
                s == 0 ? " open" : "",
                segments[s].label,
                segments[s].start_frame,
                segments[s].end_frame);
            for (size_t f = segments[s].start_frame; f <= segments[s].end_frame && f < frame_count; f++) {
                fprintf(out, "<tr><td>%zu</td><td class=\"lanes\">", f);
                print_html_lanes(out, lanes + f * D8_K_LANES);
                fprintf(out, "</td><td class=\"text\">%s</td></tr>", fired_labels[f]);
            }
            fputs("</tbody></table></details>\n", out);
        }
        write_tape_html_footer(out);
        fclose(out);
    }

    free(segments);
    free(lanes);
    free(fired_labels);
    free(labels);
    if (swarm) d8_swarm_destroy(swarm);
    free(bake_data);
    free(tape_data);
    return 0;
}

static void write_agent_html_header(FILE* out,
                                    const char* bake_path,
                                    const char* tape_path,
                                    size_t frame_count,
                                    int packed4,
                                    int reset_on_winner) {
    fprintf(out,
        "<!doctype html><html><head><meta charset=\"utf-8\"><title>DECIMA-8 Agent Run Report</title>"
        "<style>"
        ":root{color-scheme:dark;--bg:#111417;--panel:#191f24;--ink:#e9eef2;--muted:#93a2ad;--line:#2a343c;}"
        "body{margin:0;background:var(--bg);color:var(--ink);font-family:-apple-system,BlinkMacSystemFont,Segoe UI,sans-serif;}"
        "main{max-width:1280px;margin:0 auto;padding:28px;}h1{font-size:28px;margin:0 0 6px;}h2{font-size:17px;margin:26px 0 12px;}"
        ".muted{color:var(--muted)}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(170px,1fr));gap:10px;margin-top:18px;}"
        ".metric{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:12px;}.metric b{display:block;font-size:22px;margin-top:4px;}"
        ".panel{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:14px;overflow:auto;}"
        "table{width:100%%;border-collapse:collapse;font-size:12px;}th,td{border-bottom:1px solid var(--line);padding:7px 8px;text-align:right;vertical-align:top;}th:first-child,td:first-child{text-align:left;}"
        ".lanes{white-space:nowrap}.lanes span{display:inline-block;min-width:18px;margin-right:3px;border-radius:3px;text-align:center;color:#fff;font-variant-numeric:tabular-nums;}"
        ".text{text-align:left;min-width:160px}.err{color:#d66e63}"
        "</style></head><body><main>"
        "<h1>DECIMA-8 Agent Run Report</h1><div class=\"muted\">%s -> %s | frames=%zu | tape=%s | reset-on-winner=%s</div>"
        "<h2>Frame Stream</h2><section class=\"panel\"><table><thead><tr>"
        "<th>frame</th><th>status</th><th>ingress</th><th>readout</th><th>fired patterns</th><th>winners</th><th>flags</th>"
        "</tr></thead><tbody>\n",
        tape_path,
        bake_path,
        frame_count,
        packed4 ? "packed4" : "raw8",
        reset_on_winner ? "yes" : "no");
}

static void write_agent_html_footer(FILE* out,
                                    size_t frame_count,
                                    size_t ok_count,
                                    size_t error_count,
                                    size_t fired_pattern_count,
                                    size_t active_tiles) {
    fprintf(out,
        "</tbody></table></section><h2>Summary</h2><section class=\"grid\">"
        "<div class=\"metric\"><span class=\"muted\">Frames</span><b>%zu</b></div>"
        "<div class=\"metric\"><span class=\"muted\">OK</span><b>%zu</b></div>"
        "<div class=\"metric\"><span class=\"muted\">Errors</span><b>%zu</b></div>"
        "<div class=\"metric\"><span class=\"muted\">Fired Patterns</span><b>%zu</b></div>"
        "<div class=\"metric\"><span class=\"muted\">Active Tiles</span><b>%zu</b></div>"
        "</section></main></body></html>\n",
        frame_count,
        ok_count,
        error_count,
        fired_pattern_count,
        active_tiles);
}

static int cmd_run(int argc, char** argv) {
    const char* bake_path = arg_value(argc, argv, "--bake");
    const char* tape_path = arg_value(argc, argv, "--tape");
    const char* jsonl_path = arg_value(argc, argv, "--jsonl");
    const char* html_path = arg_value(argc, argv, "--html");
    const char* tape_format = arg_value(argc, argv, "--tape-format");
    uint32_t limit = parse_u32_or(arg_value(argc, argv, "--limit"), 0);
    uint32_t reset_mask = parse_u32_or(arg_value(argc, argv, "--reset"), 0);
    int reset_on_winner = has_arg(argc, argv, "--reset-on-winner");
    uint32_t reset_every_frames = parse_u32_or(
        arg_value(argc, argv, "--reset-every-frames"),
        0);
    int events_only = has_arg(argc, argv, "--events-only");
    uint32_t trace_tile = parse_u32_or(arg_value(argc, argv, "--trace-tile"), UINT32_MAX);
    uint32_t trace_every = parse_u32_or(arg_value(argc, argv, "--trace-every"), 1);
    if (trace_every == 0) trace_every = 1;

    if (!bake_path || !tape_path) {
        print_usage(argv[0]);
        return 2;
    }

    uint8_t* bake_data = NULL;
    size_t bake_size = 0;
    uint8_t* tape_data = NULL;
    size_t tape_size = 0;

    if (read_file(bake_path, &bake_data, &bake_size) != 0) return 1;
    if (read_file(tape_path, &tape_data, &tape_size) != 0) {
        free(bake_data);
        return 1;
    }

    int packed4 = 0;
    if (!tape_format || strcmp(tape_format, "auto") == 0) {
        packed4 = (tape_size % 8 != 0 && tape_size % 4 == 0) ? 1 : 0;
    } else if (strcmp(tape_format, "packed4") == 0) {
        packed4 = 1;
    } else if (strcmp(tape_format, "raw8") == 0) {
        packed4 = 0;
    } else {
        fprintf(stderr, "unknown tape format: %s\n", tape_format);
        free(bake_data);
        free(tape_data);
        return 1;
    }

    const size_t frame_bytes = packed4 ? 4 : D8_K_LANES;
    if (tape_size % frame_bytes != 0) {
        fprintf(stderr, "bad tape length: %zu is not divisible by %zu\n", tape_size, frame_bytes);
        free(bake_data);
        free(tape_data);
        return 1;
    }

    d8_swarm_t* swarm = d8_swarm_create();
    if (!swarm) {
        free(bake_data);
        free(tape_data);
        return 1;
    }

    d8_status_t st = d8_swarm_ev_bake(swarm, bake_data, bake_size);
    if (st.code != D8_STATUS_OK) {
        fprintf(stderr, "bake failed: %d %s\n", (int)st.code, st.msg);
        d8_swarm_destroy(swarm);
        free(bake_data);
        free(tape_data);
        return 1;
    }

    FILE* out = stdout;
    if (jsonl_path && strcmp(jsonl_path, "-") != 0) {
        out = fopen(jsonl_path, "wb");
        if (!out) {
            fprintf(stderr, "failed to open %s: %s\n", jsonl_path, strerror(errno));
            d8_swarm_destroy(swarm);
            free(bake_data);
            free(tape_data);
            return 1;
        }
    }

    size_t frame_count = tape_size / frame_bytes;
    if (limit > 0 && limit < frame_count) frame_count = limit;

    FILE* html = NULL;
    if (html_path) {
        html = fopen(html_path, "wb");
        if (!html) {
            fprintf(stderr, "failed to open %s: %s\n", html_path, strerror(errno));
            if (out != stdout) fclose(out);
            d8_swarm_destroy(swarm);
            free(bake_data);
            free(tape_data);
            return 1;
        }
        write_agent_html_header(html, bake_path, tape_path, frame_count, packed4, reset_on_winner);
    }

    size_t ok_count = 0;
    size_t error_count = 0;
    size_t fired_pattern_count = 0;
    size_t periodic_reset_count = 0;
    for (size_t frame = 0; frame < frame_count; frame++) {
        if (reset_every_frames != 0 && frame != 0 && frame % reset_every_frames == 0) {
            d8_swarm_ev_reset_domain(swarm, 0xFFFF);
            periodic_reset_count++;
        }
        if (reset_mask != 0) {
            d8_swarm_ev_reset_domain(swarm, (uint16_t)reset_mask);
        }

        uint8_t ingress[8];
        if (decode_tape_frame(tape_data, frame, packed4, ingress) != 0) {
            fprintf(out, "{\"frame\":%zu,\"status\":%d,\"error\":\"Bad tape Level16\"}\n",
                    frame, (int)D8_STATUS_BAD_INGRESS_LEVEL);
            if (html) {
                fprintf(html, "<tr><td>%zu</td><td class=\"err\">Bad tape Level16</td><td colspan=\"5\"></td></tr>\n", frame);
            }
            error_count++;
            continue;
        }
        d8_flash_result_t result = d8_swarm_ev_flash(swarm, (uint32_t)frame, ingress);
        const d8_view_snapshot_t* snap = d8_swarm_get_snapshot(swarm);

        if (result.st.code == D8_STATUS_OK) {
            ok_count++;
            uint16_t winner_mask = winner_domain_mask(snap);
            size_t fired_count = count_fired_patterns(swarm, snap);
            fired_pattern_count += fired_count;
            const int trace_this_frame =
                trace_tile < d8_swarm_get_active_tile_count(swarm)
                && frame % trace_every == 0;
            if (!events_only || fired_count != 0 || winner_mask != 0 || trace_this_frame) {
                fprintf(out, "{\"frame\":%zu,\"status\":%d", frame, (int)result.st.code);
                fprintf(out, ",\"ingress\":");
                print_json_array8(out, ingress);
                fprintf(out, ",\"readout\":");
                print_json_array8(out, result.readout16);
                fprintf(out,
                    ",\"flags32\":%u,\"bus_clip\":%u,\"collide_mask16\":%u,\"auto_reset_mask16\":%u,\"winner_domain_mask16\":%u,\"cycle_us\":%u,\"fired_patterns\":",
                    (unsigned)result.flags32_last,
                    (unsigned)snap->bus_clip_mask8,
                    (unsigned)snap->collide_mask16,
                    (unsigned)snap->auto_reset_mask16,
                    (unsigned)winner_mask,
                    (unsigned)snap->cycle_time_us);
                print_fired_patterns_json(out, swarm, snap);
                fprintf(out, ",\"winners\":");
                print_winners_json(out, snap);
                if (trace_this_frame) {
                    fprintf(
                        out,
                        ",\"trace\":{\"tile\":%u,\"thr_cur\":%d,\"locked\":%s}",
                        (unsigned)trace_tile,
                        (int)d8_swarm_get_tile_thr_cur(swarm, trace_tile),
                        d8_swarm_get_tile_locked(swarm, trace_tile) ? "true" : "false");
                }
                fputs("}\n", out);
            }
            if (html && (frame < 1024 || fired_count != 0 || winner_mask != 0)) {
                fprintf(html, "<tr><td>%zu</td><td>OK</td><td class=\"lanes\">", frame);
                print_html_lanes(html, ingress);
                fputs("</td><td class=\"lanes\">", html);
                print_html_lanes(html, result.readout16);
                fputs("</td><td class=\"text\">", html);
                print_fired_patterns_html(html, swarm, snap);
                fputs("</td><td class=\"text\">", html);
                print_winners_html(html, snap);
                fprintf(html,
                    "</td><td>flags=%u collide=%u reset=%u</td></tr>\n",
                    (unsigned)result.flags32_last,
                    (unsigned)snap->collide_mask16,
                    (unsigned)snap->auto_reset_mask16);
            }
            if (reset_on_winner && fired_count != 0) {
                d8_swarm_ev_reset_domain(swarm, 0xFFFF);
            }
        } else {
            error_count++;
            fprintf(out, "{\"frame\":%zu,\"status\":%d", frame, (int)result.st.code);
            fprintf(out, ",\"error\":\"%s\",\"extra\":%u", result.st.msg, (unsigned)result.st.extra);
            fputs("}\n", out);
            if (html) {
                fprintf(html,
                    "<tr><td>%zu</td><td class=\"err\">%s</td><td class=\"lanes\">",
                    frame,
                    result.st.msg);
                print_html_lanes(html, ingress);
                fprintf(html, "</td><td colspan=\"4\">extra=%u</td></tr>\n", (unsigned)result.st.extra);
            }
        }
    }

    fprintf(out,
        "{\"summary\":{\"frames\":%zu,\"ok\":%zu,\"errors\":%zu,\"active_tiles\":%zu,\"fired_patterns\":%zu,\"reset_every_frames\":%u,\"periodic_resets\":%zu}}\n",
        frame_count,
        ok_count,
        error_count,
        d8_swarm_get_active_tile_count(swarm),
        fired_pattern_count,
        (unsigned)reset_every_frames,
        periodic_reset_count);

    if (html) {
        write_agent_html_footer(html, frame_count, ok_count, error_count, fired_pattern_count, d8_swarm_get_active_tile_count(swarm));
        fclose(html);
    }
    if (out != stdout) fclose(out);
    d8_swarm_destroy(swarm);
    free(bake_data);
    free(tape_data);
    return error_count == 0 ? 0 : 1;
}

static int cmd_inspect_bake(int argc, char** argv) {
    const char* bake_path = arg_value(argc, argv, "--bake");
    const char* json_path = arg_value(argc, argv, "--json");
    const char* html_path = arg_value(argc, argv, "--html");

    if (!bake_path || (!json_path && !html_path)) {
        print_usage(argv[0]);
        return 2;
    }

    uint8_t* bake_data = NULL;
    size_t bake_size = 0;
    if (read_file(bake_path, &bake_data, &bake_size) != 0) return 1;

    d8_bake_view_t* view = (d8_bake_view_t*)calloc(1, sizeof(d8_bake_view_t));
    if (!view) {
        free(bake_data);
        return 1;
    }

    d8_status_t st = d8_bake_parse_view(bake_data, bake_size, view);
    if (st.code != D8_STATUS_OK) {
        fprintf(stderr, "bake parse failed: %d %s\n", (int)st.code, st.msg);
        d8_bake_free_view(view);
        free(view);
        free(bake_data);
        return 1;
    }

    if (json_path) {
        FILE* out = stdout;
        if (strcmp(json_path, "-") != 0) {
            out = fopen(json_path, "wb");
            if (!out) {
                fprintf(stderr, "failed to open %s: %s\n", json_path, strerror(errno));
                d8_bake_free_view(view);
                free(view);
                free(bake_data);
                return 1;
            }
        }
        write_inspect_json(out, view, bake_path);
        if (out != stdout) fclose(out);
    }

    if (html_path) {
        FILE* out = fopen(html_path, "wb");
        if (!out) {
            fprintf(stderr, "failed to open %s: %s\n", html_path, strerror(errno));
            d8_bake_free_view(view);
            free(view);
            free(bake_data);
            return 1;
        }
        write_inspect_html(out, view, bake_path);
        fclose(out);
    }

    d8_bake_free_view(view);
    free(view);
    free(bake_data);
    return 0;
}

static int cmd_bake_basic(int argc, char** argv) {
    const char* out_path = arg_value(argc, argv, "--out");
    uint32_t tiles = parse_u32_or(arg_value(argc, argv, "--tiles"), 256);
    int32_t thr_lo = parse_i32_or(arg_value(argc, argv, "--thr-lo"), 1);
    int32_t thr_hi = parse_i32_or(arg_value(argc, argv, "--thr-hi"), 1000);
    uint32_t decay = parse_u32_or(arg_value(argc, argv, "--decay"), 0);

    if (!out_path || tiles == 0 || tiles > D8_K_TILE_COUNT) {
        print_usage(argv[0]);
        return 2;
    }

    size_t size = d8_bake_gen_estimate_size(tiles);
    uint8_t* buffer = (uint8_t*)malloc(size);
    if (!buffer) return 1;

    size_t actual = 0;
    int rc = d8_bake_gen_custom(buffer, &actual, tiles, (int16_t)thr_lo, (int16_t)thr_hi, (uint16_t)decay);
    if (rc != 0) {
        free(buffer);
        return 1;
    }

    if (write_file(out_path, buffer, actual) != 0) {
        free(buffer);
        return 1;
    }

    printf("{\"ok\":true,\"out\":\"%s\",\"bytes\":%zu,\"tiles\":%u}\n", out_path, actual, tiles);
    free(buffer);
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2 || has_arg(argc, argv, "--help")) {
        print_usage(argv[0]);
        return argc < 2 ? 2 : 0;
    }

    if (strcmp(argv[1], "run") == 0) return cmd_run(argc, argv);
    if (strcmp(argv[1], "inspect-bake") == 0) return cmd_inspect_bake(argc, argv);
    if (strcmp(argv[1], "inspect-tape") == 0) return cmd_inspect_tape(argc, argv);
    if (strcmp(argv[1], "bake-basic") == 0) return cmd_bake_basic(argc, argv);

    print_usage(argv[0]);
    return 2;
}
