#pragma once

#include <string>
#include <filesystem>
#include <functional>
#include <atomic>

namespace bdslm {

class UnminedInstaller {
public:
    explicit UnminedInstaller(const std::filesystem::path &data_dir);

    std::filesystem::path getBinaryPath() const;
    bool isInstalled() const;
    std::string getLastError() const;
    bool isInstalling() const;

    // Sync install
    bool ensureInstalled();

    // Async install — callback receives (success, error_message)
    void ensureInstalledAsync(std::function<void(bool success, const std::string &error)> callback);

    // Detect current platform
    std::string detectPlatform() const;

private:
    std::filesystem::path data_dir_;
    std::filesystem::path unmined_dir_;
    std::string last_error_;
    std::atomic<bool> installing_{false};

    std::string getDownloadUrl(const std::string &platform) const;
    bool downloadAndInstall();

    // 7-Zip extraction
    std::filesystem::path getBundled7zPath() const;
    bool ensure7zAvailable();
    bool extractWith7z(const std::filesystem::path &archive,
                       const std::filesystem::path &output_dir,
                       const std::string &sevenz_bin);

    // Fallback: system tools
    std::string findSystemExtractTool() const;
    bool extractWithSystemTool(const std::filesystem::path &archive,
                               const std::filesystem::path &output_dir,
                               const std::string &tool);
};

}  // namespace bdslm
