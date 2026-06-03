#include "bdslm/unmined_installer.h"
#include <filesystem>
#include <fstream>
#include <cstdlib>

#ifdef _WIN32
#include <cstdio>
#endif

namespace bdslm {

UnminedInstaller::UnminedInstaller(const std::filesystem::path &data_dir)
    : data_dir_(data_dir),
      unmined_dir_(data_dir / "unmined-cli") {}

std::filesystem::path UnminedInstaller::getBinaryPath() const {
#ifdef _WIN32
    return unmined_dir_ / "unmined-cli.exe";
#else
    return unmined_dir_ / "unmined-cli";
#endif
}

bool UnminedInstaller::isInstalled() const {
    auto bin = getBinaryPath();
    return std::filesystem::exists(bin);
}

std::string UnminedInstaller::detectPlatform() const {
#ifdef _WIN32
    return "windows-x64";
#else
    // Detect Linux architecture via uname
    std::string arch = "x86_64";  // default

    FILE *pipe = popen("uname -m 2>/dev/null", "r");
    if (pipe) {
        std::array<char, 64> buf;
        while (fgets(buf.data(), buf.size(), pipe) != nullptr) {
            arch += buf.data();
        }
        pclose(pipe);
        // Trim
        while (!arch.empty() && (arch.back() == '\n' || arch.back() == '\r' || arch.back() == ' '))
            arch.pop_back();
    }

    if (arch == "aarch64" || arch == "arm64") return "linux-arm64";
    if (arch == "armv7l" || arch == "armhf") return "linux-arm";
    return "linux-x64";
#endif
}

bool UnminedInstaller::ensureInstalled() {
    if (isInstalled()) return true;
    return downloadAndInstall();
}

bool UnminedInstaller::downloadAndInstall() {
#ifdef _WIN32
    // Windows: auto-install not supported, user must download manually
    return false;
#else
    std::string platform = detectPlatform();

    // Check if curl is available
    int curl_check = std::system("which curl > /dev/null 2>&1");
    if (curl_check != 0) {
        return false;  // curl not available
    }

    // Construct download URL
    std::string download_url;
    if (platform == "linux-x64") {
        download_url = "https://unmined.net/download/unmined-cli-linux-x64-dev/";
    } else if (platform == "linux-arm64") {
        download_url = "https://unmined.net/download/unmined-cli-linux-arm64-dev/";
    } else {
        download_url = "https://unmined.net/download/unmined-cli-linux-x64-dev/";
    }

    // Create directories
    std::filesystem::create_directories(unmined_dir_);
    std::filesystem::create_directories(data_dir_ / ".tmp");

    // Download to temp directory inside data_dir (not /tmp, more reliable)
    std::filesystem::path tmp_archive = data_dir_ / ".tmp" / "unmined-cli-download.tar.gz";
    std::string curl_cmd = "curl -L --connect-timeout 30 --max-time 300 -o \"" + tmp_archive.string() + "\" \"" + download_url + "\" 2>/dev/null";

    int ret = std::system(curl_cmd.c_str());
    if (ret != 0 || !std::filesystem::exists(tmp_archive) || std::filesystem::file_size(tmp_archive) < 1000) {
        // Cleanup on failure
        std::filesystem::remove(tmp_archive);
        return false;
    }

    // Extract archive to temp dir
    std::filesystem::path extract_dir = data_dir_ / ".tmp" / "extract";
    std::filesystem::create_directories(extract_dir);

    std::string tar_cmd = "tar xzf \"" + tmp_archive.string() + "\" -C \"" + extract_dir.string() + "\" 2>/dev/null";
    ret = std::system(tar_cmd.c_str());

    // Remove archive
    std::filesystem::remove(tmp_archive);

    if (ret != 0) {
        std::filesystem::remove_all(extract_dir);
        return false;
    }

    // Find extracted directory
    std::filesystem::path extracted_dir;
    for (const auto &entry : std::filesystem::directory_iterator(extract_dir)) {
        if (std::filesystem::is_directory(entry.path())) {
            auto name = entry.path().filename().string();
            if (name.find("unmined-cli") != std::string::npos) {
                extracted_dir = entry.path();
                break;
            }
        }
    }

    if (extracted_dir.empty()) {
        std::filesystem::remove_all(data_dir_ / ".tmp");
        return false;
    }

    // Copy all files to unmined_dir_
    for (const auto &entry : std::filesystem::recursive_directory_iterator(extracted_dir)) {
        if (!std::filesystem::is_regular_file(entry.path())) continue;
        auto rel = std::filesystem::relative(entry.path(), extracted_dir);
        auto target = unmined_dir_ / rel;
        std::filesystem::create_directories(target.parent_path());
        std::filesystem::copy(entry.path(), target,
            std::filesystem::copy_options::overwrite_existing);
    }

    // Make unmined-cli executable
    auto bin = getBinaryPath();
    if (std::filesystem::exists(bin)) {
        std::filesystem::permissions(bin,
            std::filesystem::perms::owner_exec |
            std::filesystem::perms::group_exec |
            std::filesystem::perms::others_exec,
            std::filesystem::perm_options::add);
    }

    // Cleanup temp
    std::filesystem::remove_all(data_dir_ / ".tmp");

    return isInstalled();
#endif
}

}  // namespace bdslm
