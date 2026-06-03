#include "bdslm/unmined_installer.h"
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <cstdio>
#include <thread>

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
    return std::filesystem::exists(getBinaryPath());
}

std::string UnminedInstaller::getLastError() const {
    return last_error_;
}

bool UnminedInstaller::isInstalling() const {
    return installing_.load();
}

std::string UnminedInstaller::detectPlatform() const {
#ifdef _WIN32
    return "windows-x64";
#else
    std::string arch = "x86_64";
    FILE *pipe = popen("uname -m 2>/dev/null", "r");
    if (pipe) {
        std::array<char, 64> buf;
        while (fgets(buf.data(), buf.size(), pipe) != nullptr) {
            arch += buf.data();
        }
        pclose(pipe);
        while (!arch.empty() && (arch.back() == '\n' || arch.back() == '\r' || arch.back() == ' '))
            arch.pop_back();
    }

    if (arch == "aarch64" || arch == "arm64") return "linux-arm64";
    if (arch == "armv7l" || arch == "armhf") return "linux-arm";
    return "linux-x64";
#endif
}

std::string UnminedInstaller::getDownloadUrl(const std::string &platform) const {
    // These URLs return .tar.gz via Content-Disposition header
    if (platform == "linux-x64") {
        return "https://unmined.net/download/unmined-cli-linux-x64-dev/";
    } else if (platform == "linux-arm64") {
        return "https://unmined.net/download/unmined-cli-linux-arm64-dev/";
    } else if (platform == "windows-x64") {
        return "https://unmined.net/download/unmined-cli-windows-x64-dev/";
    }
    return "https://unmined.net/download/unmined-cli-linux-x64-dev/";
}

std::string UnminedInstaller::getArchiveName(const std::string &platform) const {
    // Expected archive filenames from Content-Disposition
    if (platform == "linux-x64") {
        return "unmined-cli_0.19.60-dev_linux-x64.tar.gz";
    } else if (platform == "linux-arm64") {
        return "unmined-cli_0.19.60-dev_linux-arm64.tar.gz";
    } else if (platform == "windows-x64") {
        return "unmined-cli_0.19.60-dev_windows-x64.zip";
    }
    return "unmined-cli_0.19.60-dev_linux-x64.tar.gz";
}

bool UnminedInstaller::ensureInstalled() {
    if (isInstalled()) return true;
    return downloadAndInstall();
}

void UnminedInstaller::ensureInstalledAsync(std::function<void(bool success)> callback) {
    if (isInstalled()) {
        if (callback) callback(true);
        return;
    }
    if (installing_.load()) return;  // Already installing

    installing_.store(true);
    std::thread([this, callback]() {
        bool result = downloadAndInstall();
        installing_.store(false);
        if (callback) callback(result);
    }).detach();
}

bool UnminedInstaller::downloadAndInstall() {
    std::string platform = detectPlatform();

    // Check if curl or wget is available
    bool has_curl = (std::system("which curl > /dev/null 2>&1") == 0);
    bool has_wget = (std::system("which wget > /dev/null 2>&1") == 0);

    if (!has_curl && !has_wget) {
        last_error_ = "Neither curl nor wget found. Please install one, or download unmined-cli manually.";
        return false;
    }

    std::string download_url = getDownloadUrl(platform);

    // Create directories
    std::filesystem::create_directories(unmined_dir_);
    std::filesystem::create_directories(data_dir_ / ".tmp");

    std::filesystem::path tmp_archive = data_dir_ / ".tmp" / getArchiveName(platform);

    // Download
    int ret;
    if (has_curl) {
        std::string cmd =
            "curl -L --connect-timeout 30 --max-time 600 --retry 3 "
            "-o \"" + tmp_archive.string() + "\" "
            "\"" + download_url + "\" 2>/dev/null";
        ret = std::system(cmd.c_str());
    } else {
        // Fallback to wget
        std::string cmd =
            "wget --timeout=30 --tries=3 -q "
            "-O \"" + tmp_archive.string() + "\" "
            "\"" + download_url + "\" 2>/dev/null";
        ret = std::system(cmd.c_str());
    }

    if (ret != 0) {
        last_error_ = "Download failed (exit code " + std::to_string(ret) + ", tool: " +
                      (has_curl ? "curl" : "wget") + ")";
        std::filesystem::remove(tmp_archive);
        return false;
    }

    if (!std::filesystem::exists(tmp_archive)) {
        last_error_ = "Downloaded file not found";
        return false;
    }

    auto file_size = std::filesystem::file_size(tmp_archive);
    if (file_size < 10000) {
        last_error_ = "Downloaded file too small (" + std::to_string(file_size) +
                      " bytes), likely an error page. URL may have changed.";
        std::filesystem::remove(tmp_archive);
        return false;
    }

    // Extract
    std::filesystem::path extract_dir = data_dir_ / ".tmp" / "extract";
    std::filesystem::create_directories(extract_dir);

#ifdef _WIN32
    // Windows uses .zip
    std::string extract_cmd = "powershell -Command \"Expand-Archive -Path '" +
        tmp_archive.string() + "' -DestinationPath '" + extract_dir.string() + "' -Force\" 2>$null";
#else
    // Linux uses .tar.gz
    std::string extract_cmd = "tar xzf \"" + tmp_archive.string() +
        "\" -C \"" + extract_dir.string() + "\" 2>/dev/null";
#endif

    ret = std::system(extract_cmd.c_str());
    std::filesystem::remove(tmp_archive);

    if (ret != 0) {
        last_error_ = "Extraction failed (exit code " + std::to_string(ret) + ")";
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
        last_error_ = "Could not find unmined-cli directory in extracted archive";
        std::filesystem::remove_all(data_dir_ / ".tmp");
        return false;
    }

    // Copy files to unmined_dir_
    for (const auto &entry : std::filesystem::recursive_directory_iterator(extracted_dir)) {
        if (!std::filesystem::is_regular_file(entry.path())) continue;
        auto rel = std::filesystem::relative(entry.path(), extracted_dir);
        auto target = unmined_dir_ / rel;
        std::filesystem::create_directories(target.parent_path());
        std::filesystem::copy(entry.path(), target,
            std::filesystem::copy_options::overwrite_existing);
    }

    // Make binary executable (Linux/macOS)
#ifndef _WIN32
    auto bin = getBinaryPath();
    if (std::filesystem::exists(bin)) {
        std::filesystem::permissions(bin,
            std::filesystem::perms::owner_exec |
            std::filesystem::perms::group_exec |
            std::filesystem::perms::others_exec,
            std::filesystem::perm_options::add);
    }
#endif

    // Cleanup
    std::filesystem::remove_all(data_dir_ / ".tmp");

    if (isInstalled()) {
        last_error_.clear();
        return true;
    } else {
        last_error_ = "unmined-cli binary not found after installation";
        return false;
    }
}

}  // namespace bdslm
