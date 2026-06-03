#include "bdslm/unmined_installer.h"
#include "bdslm/tar_extractor.h"
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <cstdio>
#include <thread>

#include <httplib.h>

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
    if (platform == "linux-x64") {
        return "/download/unmined-cli-linux-x64-dev/";
    } else if (platform == "linux-arm64") {
        return "/download/unmined-cli-linux-arm64-dev/";
    } else if (platform == "windows-x64") {
        return "/download/unmined-cli-windows-x64-dev/";
    }
    return "/download/unmined-cli-linux-x64-dev/";
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
    if (installing_.load()) return;

    installing_.store(true);
    std::thread([this, callback]() {
        bool result = downloadAndInstall();
        installing_.store(false);
        if (callback) callback(result);
    }).detach();
}

bool UnminedInstaller::downloadAndInstall() {
    std::string platform = detectPlatform();

    // Create directories
    std::filesystem::create_directories(unmined_dir_);
    std::filesystem::create_directories(data_dir_ / ".tmp");

    // Download via cpp-httplib HTTPS client
    std::filesystem::path tmp_archive;
    bool is_zip = false;

#ifdef _WIN32
    tmp_archive = data_dir_ / ".tmp" / "unmined-cli-download.zip";
    is_zip = true;
#else
    tmp_archive = data_dir_ / ".tmp" / "unmined-cli-download.tar.gz";
#endif

    httplib::Client cli("https://unmined.net");
    cli.set_connection_timeout(30);
    cli.set_read_timeout(600);
    cli.set_follow_location(true);

    std::string path = getDownloadUrl(platform);

    std::ofstream ofs(tmp_archive, std::ios::binary);
    if (!ofs.is_open()) {
        last_error_ = "Cannot create temp file: " + tmp_archive.string();
        return false;
    }

    size_t total_downloaded = 0;
    auto res = cli.Get(path, [&](const char *data, size_t data_len) {
        ofs.write(data, data_len);
        total_downloaded += data_len;
        return true;
    });

    ofs.close();

    if (!res) {
        last_error_ = "HTTPS download failed: " + httplib::to_string(res.error());
        std::filesystem::remove(tmp_archive);
        return false;
    }

    if (res->status != 200) {
        last_error_ = "HTTP " + std::to_string(res->status);
        std::filesystem::remove(tmp_archive);
        return false;
    }

    if (total_downloaded < 10000) {
        last_error_ = "Downloaded file too small (" + std::to_string(total_downloaded) + " bytes)";
        std::filesystem::remove(tmp_archive);
        return false;
    }

    // Extract using built-in extractor (no external tools needed!)
    std::filesystem::path extract_dir = data_dir_ / ".tmp" / "extract";
    std::filesystem::create_directories(extract_dir);

    bool extract_ok = false;
    if (is_zip) {
#ifdef _WIN32
        // Windows .zip: try PowerShell, then 7z
        std::string cmd = "powershell -Command \"Expand-Archive -Path '" +
            tmp_archive.string() + "' -DestinationPath '" + extract_dir.string() + "' -Force\" 2>$null";
        int ret = std::system(cmd.c_str());
        extract_ok = (ret == 0);
        if (!extract_ok) {
            last_error_ = "PowerShell extraction failed";
        }
#else
        last_error_ = "ZIP extraction only supported on Windows";
#endif
    } else {
        // Linux .tar.gz: use built-in TarGzExtractor
        std::string tar_error;
        extract_ok = TarGzExtractor::extract(tmp_archive.string(), extract_dir.string(), tar_error);
        if (!extract_ok) {
            last_error_ = "Tar extraction failed: " + tar_error;
        }
    }

    std::filesystem::remove(tmp_archive);

    if (!extract_ok) {
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
        last_error_ = "Could not find unmined-cli in extracted archive";
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

    // Make executable
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
