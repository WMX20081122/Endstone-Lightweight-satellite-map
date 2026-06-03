#pragma once

#include <string>
#include <filesystem>

namespace bdslm {

// Auto-installs unmined-cli if not found
class UnminedInstaller {
public:
    explicit UnminedInstaller(const std::filesystem::path &data_dir);

    // Returns true if unmined-cli is available (existing or just installed)
    bool ensureInstalled();

    // Get the path to the unmined-cli binary
    std::filesystem::path getBinaryPath() const;

    // Check if unmined-cli exists and is executable
    bool isInstalled() const;

private:
    std::filesystem::path data_dir_;
    std::filesystem::path unmined_dir_;

    bool downloadAndInstall();
    std::string detectPlatform() const;
};

}  // namespace bdslm
