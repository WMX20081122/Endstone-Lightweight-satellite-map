#pragma once

#include <string>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace bdslm {

struct WebServerConfig {
    int port = 5110;
    std::string map_title = "default";
    std::string bind_address = "0.0.0.0";
};

struct MapRenderConfig {
    int max_zoom_level = -2;
    int min_zoom_level = 4;
    std::string image_format = "webp";
};

struct AutoRendConfig {
    bool enable = true;
    int cycle = 30;  // minutes
};

struct PathsConfig {
    std::string unmined_cli = "./plugins/bdslm/unmined-cli/unmined-cli";
    std::string world_path;
    std::string output_dir;
};

struct Config {
    WebServerConfig webserver;
    MapRenderConfig map_render;
    AutoRendConfig auto_rend;
    PathsConfig paths;
};

class ConfigManager {
public:
    explicit ConfigManager(const std::filesystem::path &data_dir);

    void load();
    void save();
    void reload();

    const Config &getConfig() const { return config_; }
    Config &getConfig() { return config_; }

    std::filesystem::path getDataDir() const { return data_dir_; }
    std::filesystem::path getConfigPath() const { return data_dir_ / "config.json"; }
    std::filesystem::path getMarkersPath() const { return data_dir_ / "markers.json"; }
    std::filesystem::path getPlayersPath() const { return data_dir_ / "players.json"; }

    // Markers
    nlohmann::json &getMarkers() { return markers_; }
    void saveMarkers();
    void loadMarkers();

private:
    std::filesystem::path data_dir_;
    Config config_;
    nlohmann::json markers_;

    void mergeConfig(nlohmann::json &base, const nlohmann::json &override);
};

}  // namespace bdslm
