#pragma once

#include <string>
#include <filesystem>

namespace bdslm {

class UnminedInstaller {
public:
    explicit UnminedInstaller(const std::filesystem::path &data_dir);

    std::filesystem::path getBinaryPath() const;
    bool isInstalled() const;

    bool ensureInstalled();

private:
    std::filesystem::path data_dir_;
    std::filesystem::path unmined_dir_;

    std::string detectPlatform() const;
    bool downloadAndInstall();
};

}  // namespace bdslm
