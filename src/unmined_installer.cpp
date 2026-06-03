#include "bdslm/unmined_installer.h"
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <cstdio>
#include <thread>
#include <array>

#include <httplib.h>

namespace bdslm {

// 7-Zip standalone download URLs (ip7z fork, official mirror)
static const char *const SEVENZ_DOWNLOAD_URL_LINUX =
    "https://github.com/ip7z/7zip/releases/download/26.01/7z2601-linux-x64.tar.xz";
static const char *const SEVENZ_DOWNLOAD_URL_WINDOWS =
    "https://github.com/ip7z/7zip/releases/download/26.01/7zr.exe";

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
            arch = buf.data();
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

// ============================================================
// 7-Zip management
// ============================================================

std::filesystem::path UnminedInstaller::getBundled7zPath() const {
    auto tools_dir = data_dir_ / ".tools";
#ifdef _WIN32
    return tools_dir / "7zr.exe";
#else
    return tools_dir / "7zz";
#endif
}

bool UnminedInstaller::ensure7zAvailable() {
    auto sz_path = getBundled7zPath();
    if (std::filesystem::exists(sz_path)) return true;

    // Download 7-Zip standalone
    auto tools_dir = data_dir_ / ".tools";
    std::filesystem::create_directories(tools_dir);

#ifdef _WIN32
    // Windows: just download 7zr.exe directly
    httplib::Client cli("https://github.com");
    cli.set_connection_timeout(30);
    cli.set_read_timeout(120);
    cli.set_follow_location(true);

    std::ofstream ofs(sz_path, std::ios::binary);
    if (!ofs.is_open()) {
        last_error_ = "Cannot create 7zr.exe";
        return false;
    }

    auto res = cli.Get("/ip7z/7zip/releases/download/26.01/7zr.exe",
        [&](const char *data, size_t data_len) {
            ofs.write(data, data_len);
            return true;
        });

    ofs.close();

    if (!res || res->status != 200) {
        last_error_ = "Failed to download 7zr.exe";
        std::filesystem::remove(sz_path);
        return false;
    }
    return true;

#else
    // Linux: download 7z2601-linux-x64.tar.xz, extract 7zz from it
    // But wait — we need 7z/tar to extract .tar.xz too! Chicken-and-egg problem.
    // Solution: check if system tar/xz exists first for this one-time extraction
    // If not, try system 7z/7za

    // Check for system tools to extract .tar.xz
    bool has_tar_xz = (std::system("which tar 2>/dev/null && which xz 2>/dev/null") == 0);
    bool has_7z = (std::system("which 7z 2>/dev/null") == 0 || std::system("which 7za 2>/dev/null") == 0);

    if (!has_tar_xz && !has_7z) {
        last_error_ = "Cannot extract 7-Zip: need tar+xz or 7z installed on system";
        return false;
    }

    // Download
    auto tmp_archive = data_dir_ / ".tmp" / "7z-linux-x64.tar.xz";
    std::filesystem::create_directories(tmp_archive.parent_path());

    httplib::Client cli("https://github.com");
    cli.set_connection_timeout(30);
    cli.set_read_timeout(120);
    cli.set_follow_location(true);

    std::ofstream ofs(tmp_archive, std::ios::binary);
    auto res = cli.Get("/ip7z/7zip/releases/download/26.01/7z2601-linux-x64.tar.xz",
        [&](const char *data, size_t data_len) {
            ofs.write(data, data_len);
            return true;
        });

    ofs.close();

    if (!res || res->status != 200) {
        last_error_ = "Failed to download 7zz";
        std::filesystem::remove(tmp_archive);
        return false;
    }

    // Extract
    bool ok = false;
    if (has_tar_xz) {
        std::string cmd = "tar xJf " + tmp_archive.string() + " -C " + tmp_archive.parent_path().string();
        ok = (std::system(cmd.c_str()) == 0);
    }
    if (!ok && has_7z) {
        std::string cmd7z = std::system("which 7z 2>/dev/null") == 0 ? "7z" : "7za";
        std::string cmd = cmd7z + " x " + tmp_archive.string() + " -o" + tmp_archive.parent_path().string() + " -y";
        ok = (std::system(cmd.c_str()) == 0);
    }

    std::filesystem::remove(tmp_archive);

    if (!ok) {
        last_error_ = "Failed to extract 7zz archive";
        return false;
    }

    // Find and copy 7zz to tools dir
    for (const auto &entry : std::filesystem::recursive_directory_iterator(tmp_archive.parent_path())) {
        auto name = entry.path().filename().string();
        if (name == "7zz" || name == "7zzs") {
            std::filesystem::copy(entry.path(), sz_path,
                std::filesystem::copy_options::overwrite_existing);
            break;
        }
    }

    // Cleanup temp
    for (const auto &entry : std::filesystem::directory_iterator(tmp_archive.parent_path())) {
        if (entry.path() != sz_path && entry.path().filename() != ".tools") {
            if (entry.path().filename() != "7z-linux-x64.tar.xz") {
                std::filesystem::remove_all(entry.path());
            }
        }
    }

    if (!std::filesystem::exists(sz_path)) {
        last_error_ = "7zz binary not found after extraction";
        return false;
    }

    // Make executable
    std::filesystem::permissions(sz_path,
        std::filesystem::perms::owner_exec |
        std::filesystem::perms::group_exec |
        std::filesystem::perms::others_exec,
        std::filesystem::perm_options::add);

    return true;
#endif
}

bool UnminedInstaller::extractWith7z(const std::filesystem::path &archive,
                                      const std::filesystem::path &output_dir,
                                      const std::string &sevenz_bin) {
    std::string cmd = sevenz_bin + " x " + archive.string() + " -o" + output_dir.string() + " -y";
    return (std::system(cmd.c_str()) == 0);
}

// ============================================================
// System tool fallback
// ============================================================

std::string UnminedInstaller::findSystemExtractTool() const {
    const char *candidates[] = {"lip", "7z", "7za", "tar", nullptr};
    for (int i = 0; candidates[i] != nullptr; ++i) {
        std::string cmd = std::string("which ") + candidates[i] + " 2>/dev/null";
#ifdef _WIN32
        cmd = std::string("where ") + candidates[i] + " 2>nul";
#endif
        if (std::system(cmd.c_str()) == 0) return candidates[i];
    }
    return "";
}

bool UnminedInstaller::extractWithSystemTool(const std::filesystem::path &archive,
                                              const std::filesystem::path &output_dir,
                                              const std::string &tool) {
    std::string cmd;
    if (tool == "lip") {
        cmd = "lip install --local " + archive.string() + " -o " + output_dir.string();
    } else if (tool == "7z" || tool == "7za") {
        cmd = tool + " x " + archive.string() + " -o" + output_dir.string() + " -y";
    } else if (tool == "tar") {
        cmd = "tar xzf " + archive.string() + " -C " + output_dir.string();
    } else {
        return false;
    }
    return (std::system(cmd.c_str()) == 0);
}

// ============================================================
// Main install flow
// ============================================================

bool UnminedInstaller::ensureInstalled() {
    if (isInstalled()) return true;
    return downloadAndInstall();
}

void UnminedInstaller::ensureInstalledAsync(std::function<void(bool success, const std::string &error)> callback) {
    if (isInstalled()) {
        if (callback) callback(true, "");
        return;
    }
    if (installing_.load()) return;

    installing_.store(true);
    std::thread([this, callback]() {
        bool result = downloadAndInstall();
        installing_.store(false);
        if (callback) callback(result, last_error_);
    }).detach();
}

bool UnminedInstaller::downloadAndInstall() {
    std::string platform = detectPlatform();

    // Create directories
    std::filesystem::create_directories(unmined_dir_);
    std::filesystem::create_directories(data_dir_ / ".tmp");

    // Download unmined-cli via cpp-httplib HTTPS client
    std::filesystem::path tmp_archive;
#ifdef _WIN32
    tmp_archive = data_dir_ / ".tmp" / "unmined-cli-download.zip";
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

    // Extract — try bundled 7z first, then system tools
    std::filesystem::path extract_dir = data_dir_ / ".tmp" / "extract";
    std::filesystem::create_directories(extract_dir);

    bool extract_ok = false;

    // 1. Try bundled 7-Zip
    if (ensure7zAvailable()) {
        auto sz_path = getBundled7zPath();
        extract_ok = extractWith7z(tmp_archive, extract_dir, sz_path.string());
        if (!extract_ok) {
            // Bundled 7z failed, try system tools
        }
    }

    // 2. Fallback: system tools
    if (!extract_ok) {
        std::string sys_tool = findSystemExtractTool();
        if (!sys_tool.empty()) {
            extract_ok = extractWithSystemTool(tmp_archive, extract_dir, sys_tool);
        }
    }

    std::filesystem::remove(tmp_archive);

    if (!extract_ok) {
        last_error_ = "No extraction tool available. Install lip, 7z, or tar.";
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
