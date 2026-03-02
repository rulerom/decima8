/*
 * DECIMA-8 Source Code
 * This code is part of Decima-8 Core
 *
 * All rights belong to the ORDEN (c) 2026
 */

#include "d8/bake.hpp"
#include "d8/logger.hpp"
#include <vector>
#include <fstream>

static bool read_file(const char* path, std::vector<d8::u8>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    std::streamsize n = f.tellg();
    f.seekg(0, std::ios::beg);
    if (n < 0) return false;
    out.resize(std::size_t(n));
    if (!out.empty()) f.read(reinterpret_cast<char*>(out.data()), n);
    return true;
}

int main(int argc, char** argv) {
    d8::init_logger();
    
    if (argc < 2) {
        spdlog::info("Usage: bake_roundtrip <BakeBlob.bin>");
        spdlog::info("Stub example: validates and (optionally) canonicalizes blob.");
        return 0;
    }

    std::vector<d8::u8> blob;
    if (!read_file(argv[1], blob)) {
        spdlog::error("Failed to read file: {}", argv[1]);
        return 1;
    }

    d8::bake::bake_view view{};
    auto st = d8::bake::validate(blob, &view);
    if (!st) {
        spdlog::error("Validate failed: code={} aux={} msg={}", 
                      static_cast<std::uint32_t>(st.c), st.aux, st.msg);
        return 2;
    }

    spdlog::info("Validate OK. bake_id={} profile_id={}", 
                 view.header.bake_id, view.header.profile_id);

    // Canonicalize into a new buffer
    std::vector<d8::u8> out(blob.size());
    std::size_t out_len = 0;
    st = d8::bake::serialize_canonical(view, out, out_len);
    if (!st) {
        spdlog::error("Canonical serialize failed: code={} aux={} msg={}", 
                      static_cast<std::uint32_t>(st.c), st.aux, st.msg);
        return 3;
    }

    spdlog::info("Canonical serialize OK. out_len={}", out_len);
    return 0;
}
