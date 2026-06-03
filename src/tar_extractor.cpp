#include "bdslm/tar_extractor.h"
#include <fstream>
#include <cstring>
#include <zlib.h>

namespace bdslm {

size_t TarGzExtractor::parseOctal(const char *str, size_t len) {
    // Parse octal string, handling null-terminated and space-padded formats
    size_t result = 0;
    for (size_t i = 0; i < len; ++i) {
        if (str[i] == '\0' || str[i] == ' ') continue;
        if (str[i] >= '0' && str[i] <= '7') {
            result = result * 8 + (str[i] - '0');
        }
    }
    return result;
}

bool TarGzExtractor::isEndOfArchive(const unsigned char *block) {
    // Two consecutive zero blocks mark end of archive
    for (int i = 0; i < 512; ++i) {
        if (block[i] != 0) return false;
    }
    return true;
}

bool TarGzExtractor::extract(const std::string &tar_gz_path, const std::string &output_dir, std::string &error) {
    // Step 1: Decompress .tar.gz to memory using zlib
    // Read the entire .gz file
    std::ifstream ifs(tar_gz_path, std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) {
        error = "Cannot open file: " + tar_gz_path;
        return false;
    }
    auto file_size = ifs.tellg();
    ifs.seekg(0, std::ios::beg);

    std::vector<unsigned char> compressed(file_size);
    if (!ifs.read(reinterpret_cast<char*>(compressed.data()), file_size)) {
        error = "Cannot read file: " + tar_gz_path;
        return false;
    }
    ifs.close();

    // Decompress using zlib's gzip format support
    std::vector<unsigned char> tar_data;
    tar_data.reserve(file_size * 4);  // Estimate decompressed size

    z_stream strm = {};
    strm.next_in = compressed.data();
    strm.avail_in = static_cast<uInt>(compressed.size());

    // 16 + MAX_WBITS enables gzip decoding
    int ret = inflateInit2(&strm, 16 + MAX_WBITS);
    if (ret != Z_OK) {
        error = "zlib inflateInit2 failed: " + std::to_string(ret);
        return false;
    }

    unsigned char outbuf[65536];
    do {
        strm.next_out = outbuf;
        strm.avail_out = sizeof(outbuf);
        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            error = "zlib inflate failed: " + std::string(strm.msg ? strm.msg : "unknown error");
            inflateEnd(&strm);
            return false;
        }
        size_t have = sizeof(outbuf) - strm.avail_out;
        tar_data.insert(tar_data.end(), outbuf, outbuf + have);
    } while (ret != Z_STREAM_END);

    inflateEnd(&strm);

    // Step 2: Parse tar format and extract files
    if (tar_data.size() < 512) {
        error = "Tar data too small";
        return false;
    }

    size_t pos = 0;
    size_t files_extracted = 0;

    while (pos + 512 <= tar_data.size()) {
        // Check for end of archive
        if (isEndOfArchive(&tar_data[pos])) {
            break;
        }

        // Parse header
        const TarHeader *header = reinterpret_cast<const TarHeader*>(&tar_data[pos]);
        pos += 512;  // Skip header block

        // Get file size
        size_t file_size = parseOctal(header->size, 12);

        // Get full path (prefix + name)
        std::string filepath;
        if (header->prefix[0] != '\0') {
            filepath = std::string(header->prefix, strnlen(header->prefix, 155));
            filepath += '/';
        }
        filepath += std::string(header->name, strnlen(header->name, 100));

        // Skip empty names
        if (filepath.empty()) {
            pos += ((file_size + 511) / 512) * 512;
            continue;
        }

        // Create full output path
        std::filesystem::path full_path = std::filesystem::path(output_dir) / filepath;

        // Handle entry type
        char type = header->typeflag;
        if (type == '5' || type == 'D') {
            // Directory
            std::filesystem::create_directories(full_path);
        } else if (type == '0' || type == '\0' || type == '7') {
            // Regular file
            // Ensure parent directory exists
            std::filesystem::create_directories(full_path.parent_path());

            // Write file data
            std::ofstream ofs(full_path, std::ios::binary);
            if (!ofs.is_open()) {
                error = "Cannot create file: " + full_path.string();
                return false;
            }

            size_t remaining = file_size;
            size_t data_pos = pos;
            while (remaining > 0 && data_pos < tar_data.size()) {
                size_t chunk = std::min(remaining, static_cast<size_t>(65536));
                if (data_pos + chunk > tar_data.size()) {
                    chunk = tar_data.size() - data_pos;
                }
                ofs.write(reinterpret_cast<const char*>(&tar_data[data_pos]), chunk);
                data_pos += chunk;
                remaining -= chunk;
            }
            ofs.close();
            files_extracted++;
        }
        // Skip other types (links, etc.)

        // Advance to next entry (blocks are 512-byte aligned)
        pos += ((file_size + 511) / 512) * 512;
    }

    if (files_extracted == 0) {
        error = "No files extracted from tar archive";
        return false;
    }

    return true;
}

}  // namespace bdslm
