#pragma once

#include <string>
#include <filesystem>
#include <functional>
#include <atomic>

namespace bdslm {

// Auto-installs unmined-cli if not found
class UnminedInstaller {
public:
    explicit UnminedInstaller(const std::filesystem::path &data_dir);

    // Returns true if unmined-cli is available
    bool ensureInstalled();

    // Get the path to the unmined-cli binary
    std::filesystem::path getBinaryPath() const;

    // Check if unmined-cli exists and is executable
    bool isInstalled() const;

    // Get last error message
    std::string getLastError() const;

    // Install asynchronously (calls callback on completion)
    void ensureInstalledAsync(std::function<void(bool success)> callback);

    // Check if async install is in progress
    bool isInstalling() const;

private:
    std::filesystem::path data_dir_;
    std::filesystem::path unmined_dir_;
    std::string last_error_;
    std::atomic<bool> installing_{false};

    bool downloadAndInstall();
    std::string detectPlatform() const;
    std::string getDownloadUrl(const std::string &platform) const;
    std::string getArchiveName(const std::string &platform) const;
};

}  // namespace bdslm
