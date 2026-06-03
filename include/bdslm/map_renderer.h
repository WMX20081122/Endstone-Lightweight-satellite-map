#pragma once

#include <string>
#include <functional>
#include <atomic>
#include <filesystem>

namespace bdslm {

class ConfigManager;

class MapRenderer {
public:
    explicit MapRenderer(ConfigManager &config);
    ~MapRenderer() = default;

    bool render(const std::string &dimension = "overworld");
    bool isRendering() const { return rendering_; }

    void applyTitle();
    void applyMarkers();

private:
    ConfigManager &config_;
    std::atomic<bool> rendering_{false};

    std::string getWorldPath() const;
    std::string getOutputDir() const;
};

}  // namespace bdslm
