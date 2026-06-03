#include "bdslm/config_manager.h"
#include <fstream>
#include <iostream>

namespace bdslm {

ConfigManager::ConfigManager(const std::filesystem::path &data_dir)
    : data_dir_(data_dir) {
    std::filesystem::create_directories(data_dir_);
}

void ConfigManager::load() {
    nlohmann::json defaults = {
        {"webserver", {
            {"port", 5110},
            {"mapTitle", "default"},
            {"bindAddress", "0.0.0.0"}
        }},
        {"mapRender", {
            {"maxZoomLevel", -2},
            {"minZoomLevel", 4},
            {"imageFormat", "webp"}
        }},
        {"autoRend", {
            {"enable", true},
            {"cycle", 30}
        }},
        {"paths", {
            {"unminedCli", ""},
            {"worldPath", ""},
            {"outputDir", ""}
        }}
    };

    auto config_path = getConfigPath();
    if (std::filesystem::exists(config_path)) {
        try {
            std::ifstream f(config_path);
            auto user_config = nlohmann::json::parse(f);
            mergeConfig(defaults, user_config);
        } catch (const std::exception &e) {
            std::cerr << "[Bdslm] 配置加载失败: " << e.what() << std::endl;
        }
    }

    // Parse into struct
    auto &ws = defaults["webserver"];
    config_.webserver.port = ws.value("port", 5110);
    config_.webserver.map_title = ws.value("mapTitle", std::string("default"));
    config_.webserver.bind_address = ws.value("bindAddress", std::string("0.0.0.0"));

    auto &mr = defaults["mapRender"];
    config_.map_render.max_zoom_level = mr.value("maxZoomLevel", -2);
    config_.map_render.min_zoom_level = mr.value("minZoomLevel", 4);
    config_.map_render.image_format = mr.value("imageFormat", std::string("webp"));

    auto &ar = defaults["autoRend"];
    config_.auto_rend.enable = ar.value("enable", true);
    config_.auto_rend.cycle = ar.value("cycle", 30);

    auto &p = defaults["paths"];
    config_.paths.unmined_cli = p.value("unminedCli", std::string(""));
    config_.paths.world_path = p.value("worldPath", std::string(""));
    config_.paths.output_dir = p.value("outputDir", std::string(""));

    // Save back (ensures file exists with all keys)
    save();
}

void ConfigManager::save() {
    nlohmann::json j = {
        {"webserver", {
            {"port", config_.webserver.port},
            {"mapTitle", config_.webserver.map_title},
            {"bindAddress", config_.webserver.bind_address}
        }},
        {"mapRender", {
            {"maxZoomLevel", config_.map_render.max_zoom_level},
            {"minZoomLevel", config_.map_render.min_zoom_level},
            {"imageFormat", config_.map_render.image_format}
        }},
        {"autoRend", {
            {"enable", config_.auto_rend.enable},
            {"cycle", config_.auto_rend.cycle}
        }},
        {"paths", {
            {"unminedCli", config_.paths.unmined_cli},
            {"worldPath", config_.paths.world_path},
            {"outputDir", config_.paths.output_dir}
        }}
    };

    std::ofstream f(getConfigPath());
    f << j.dump(4);
}

void ConfigManager::reload() {
    load();
    loadMarkers();
}

void ConfigManager::mergeConfig(nlohmann::json &base, const nlohmann::json &override) {
    for (auto it = override.begin(); it != override.end(); ++it) {
        if (base.contains(it.key()) && base[it.key()].is_object() && it->is_object()) {
            mergeConfig(base[it.key()], *it);
        } else {
            base[it.key()] = *it;
        }
    }
}

void ConfigManager::loadMarkers() {
    auto path = getMarkersPath();
    if (std::filesystem::exists(path)) {
        try {
            std::ifstream f(path);
            markers_ = nlohmann::json::parse(f);
        } catch (const std::exception &) {
            markers_ = nlohmann::json::array({{{"x", 0}, {"z", 0}, {"text", ""}}});
        }
    } else {
        markers_ = nlohmann::json::array({{{"x", 0}, {"z", 0}, {"text", ""}}});
        saveMarkers();
    }
}

void ConfigManager::saveMarkers() {
    std::ofstream f(getMarkersPath());
    f << markers_.dump(4);
}

}  // namespace bdslm
