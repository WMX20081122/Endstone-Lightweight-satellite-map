#pragma once

#include <string>
#include <filesystem>
#include <vector>

namespace bdslm {

// Minimal tar.gz extractor using zlib for gzip decompression
// and manual tar format parsing. No external dependencies required.
class TarGzExtractor {
public:
    // Extract a .tar.gz file to the given directory
    // Returns true on success, false on failure
    static bool extract(const std::string &tar_gz_path, const std::string &output_dir, std::string &error);

private:
    // Tar header block structure (512 bytes)
    struct TarHeader {
        char name[100];      // File name
        char mode[8];        // File mode
        char uid[8];         // Owner ID
        char gid[8];         // Group ID
        char size[12];       // File size (octal)
        char mtime[12];      // Modification time (octal)
        char checksum[8];    // Header checksum
        char typeflag;       // Entry type ('0'=file, '5'=directory, etc.)
        char linkname[100];  // Link name
        char magic[6];       // "ustar"
        char version[2];     // "00"
        char uname[32];      // Owner name
        char gname[32];      // Group name
        char devmajor[8];    // Device major
        char devminor[8];    // Device minor
        char prefix[155];    // Path prefix
        char padding[12];    // Padding
    };

    static size_t parseOctal(const char *str, size_t len);
    static bool isEndOfArchive(const unsigned char *block);
};

}  // namespace bdslm
