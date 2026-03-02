/*
 * DECIMA-8 Source Code
 * This code is part of Decima-8 Core
 *
 * All rights belong to the ORDEN (c) 2026
 */

#include <iostream>
#include <fstream>
#include <random>
#include <cstdint>
#include <vector>
#include <string>

// VSB Tape v0.1 format:
// - Each frame is 4 bytes (32 bits)
// - 8 channels, each 4 bits (0..15)
// - Byte 0: channels 0-1 (low nibble = channel 0, high nibble = channel 1)
// - Byte 1: channels 2-3
// - Byte 2: channels 4-5
// - Byte 3: channels 6-7

struct TapeFrame {
    std::uint8_t channels[8]; // Each channel 0..15 (4 bits)
};

void pack_frame(const TapeFrame& frame, std::uint8_t* bytes) {
    // Pack 8 channels (4 bits each) into 4 bytes
    bytes[0] = (frame.channels[0] & 0x0F) | ((frame.channels[1] & 0x0F) << 4);
    bytes[1] = (frame.channels[2] & 0x0F) | ((frame.channels[3] & 0x0F) << 4);
    bytes[2] = (frame.channels[4] & 0x0F) | ((frame.channels[5] & 0x0F) << 4);
    bytes[3] = (frame.channels[6] & 0x0F) | ((frame.channels[7] & 0x0F) << 4);
}

int main(int argc, char* argv[]) {
    // Default: 256 frames
    std::size_t frame_count = 256;
    std::string output_file = "random_tape.vsb";
    
    // Parse command line arguments
    if (argc > 1) {
        try {
            frame_count = std::stoull(argv[1]);
            if (frame_count == 0) {
                std::cerr << "Error: Frame count must be > 0" << std::endl;
                return 1;
            }
        } catch (const std::exception&) {
            std::cerr << "Error: Invalid frame count: " << argv[1] << std::endl;
            return 1;
        }
    }
    
    if (argc > 2) {
        output_file = argv[2];
    }
    
    std::cout << "Generating VSB tape with " << frame_count << " frames..." << std::endl;
    std::cout << "Output file: " << output_file << std::endl;
    
    // Random number generator
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<unsigned int> dist(0, 15); // 0..15 for 4-bit values
    
    // Generate frames
    std::vector<TapeFrame> frames;
    frames.reserve(frame_count);
    
    for (std::size_t i = 0; i < frame_count; ++i) {
        TapeFrame frame;
        for (int j = 0; j < 8; ++j) {
            frame.channels[j] = static_cast<std::uint8_t>(dist(gen));
        }
        frames.push_back(frame);
    }
    
    // Write to file
    std::ofstream file(output_file, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Error: Failed to open file for writing: " << output_file << std::endl;
        return 1;
    }
    
    std::uint8_t bytes[4];
    for (const auto& frame : frames) {
        pack_frame(frame, bytes);
        file.write(reinterpret_cast<const char*>(bytes), 4);
    }
    
    file.close();
    
    std::cout << "Successfully generated " << frame_count << " frames (" 
              << (frame_count * 4) << " bytes)" << std::endl;
    
    // Print first few frames for verification
    std::cout << "\nFirst 5 frames:" << std::endl;
    for (std::size_t i = 0; i < std::min(std::size_t(5), frames.size()); ++i) {
        std::cout << "Frame " << i << ": [";
        for (int j = 0; j < 8; ++j) {
            std::cout << static_cast<int>(frames[i].channels[j]);
            if (j < 7) std::cout << ", ";
        }
        std::cout << "]" << std::endl;
    }
    
    return 0;
}
