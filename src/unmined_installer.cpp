#include "bdslm/unmined_installer.h"
#include <cstdio>
#include <array>
#include <filesystem>
#include <fstream>

namespace bdslm {

UnminedInstaller::UnminedInstaller(const std::filesystem::path &data_dir)
    : data_dir_(data_dir),
      unmined_dir_(data_dir / "unmined-cli") {}

std::filesystem::path UnminedInstaller::getBinaryPath() const {
    return unmined_dir_ / "unmined-cli";
}

bool UnminedInstaller::isInstalled() const {
    auto bin = getBinaryPath();
    return std::filesystem::exists(bin);
}

std::string UnminedInstaller::detectPlatform() const {
    // Detect Linux architecture
    FILE *pipe = popen("uname -m", "r");
    if (!pipe) return "linux-x64";

    std::array<char, 64> buf;
    std::string arch;
    while (fgets(buf.data(), buf.size(), pipe) != nullptr) {
        arch += buf.data();
    }
    pclose(pipe);

    // Trim whitespace
    while (!arch.empty() && (arch.back() == '\n' || arch.back() == '\r' || arch.back() == ' '))
        arch.pop_back();

    if (arch == "aarch64" || arch == "arm64") return "linux-arm64";
    if (arch == "armv7l" || arch == "armhf") return "linux-arm";
    return "linux-x64";  // Default: x86_64
}

bool UnminedInstaller::ensureInstalled() {
    if (isInstalled()) return true;
    return downloadAndInstall();
}

bool UnminedInstaller::downloadAndInstall() {
    std::string platform = detectPlatform();

    // Construct download URL
    // unmined-cli releases are at: https://unmined.net/download/unmined-cli-{platform}-dev/
    std::string download_url;
    if (platform == "linux-x64") {
        download_url = "https://unmined.net/download/unmined-cli-linux-x64-dev/";
    } else if (platform == "linux-arm64") {
        download_url = "https://unmined.net/download/unmined-cli-linux-arm64-dev/";
    } else {
        download_url = "https://unmined.net/download/unmined-cli-linux-x64-dev/";
    }

    // Download to temp file
    std::filesystem::path tmp_archive = data_dir_ / "unmined-cli-download.tar.gz";
    std::string curl_cmd = "curl -L -o \"" + tmp_archive.string() + "\" \"" + download_url + "\" 2>/dev/null";

    int ret = std::system(curl_cmd.c_str());
    if (ret != 0 || !std::filesystem::exists(tmp_archive)) {
        return false;
    }

    // Create target directory
    std::filesystem::create_directories(unmined_dir_);

    // Extract archive
    // The tar archive contains a directory like unmined-cli_0.19.60-dev_linux-x64/
    std::string tar_cmd = "tar xzf \"" + tmp_archive.string() + "\" -C /tmp/ 2>/dev/null";
    ret = std::system(tar_cmd.c_str());
    if (ret != 0) {
        std::filesystem::remove(tmp_archive);
        return false;
    }

    // Find extracted directory and copy contents
    // Try common patterns: unmined-cli_*_linux-x64/ or unmined-cli_*_linux-arm64/
    std::filesystem::path extracted_dir;
    for (const auto &entry : std::filesystem::directory_iterator("/tmp")) {
        auto name = entry.path().filename().string();
        if (name.find("unmined-cli") != std::string::npos && name.find(platform) != std::string::npos) {
            extracted_dir = entry.path();
            break;
        }
    }

    // Fallback: try any unmined-cli directory
    if (extracted_dir.empty()) {
        for (const auto &entry : std::filesystem::directory_iterator("/tmp")) {
            auto name = entry.path().filename().string();
            if (name.find("unmined-cli") != std::string::npos && std::filesystem::is_directory(entry.path())) {
                extracted_dir = entry.path();
                break;
            }
        }
    }

    if (extracted_dir.empty()) {
        std::filesystem::remove(tmp_archive);
        return false;
    }

    // Copy all files to unmined_dir_
    for (const auto &entry : std::filesystem::recursive_directory_iterator(extracted_dir)) {
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

    // Cleanup
    std::filesystem::remove(tmp_archive);
    std::filesystem::remove_all(extracted_dir);

    return isInstalled();
}

}  // namespace bdslm