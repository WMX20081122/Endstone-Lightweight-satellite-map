#include "bdslm/map_renderer.h"
#include "bdslm/config_manager.h"
#include "bdslm/unmined_installer.h"

#include <cstdio>
#include <array>
#include <thread>
#include <filesystem>
#include <fstream>

namespace bdslm {

MapRenderer::MapRenderer(ConfigManager &config)
    : config_(config) {}

std::string MapRenderer::getWorldPath() const {
    const auto &path = config_.getConfig().paths.world_path;
    return path.empty() ? "./worlds/Bedrock level/" : path;
}

std::string MapRenderer::getOutputDir() const {
    const auto &path = config_.getConfig().paths.output_dir;
    return path.empty() ? (config_.getDataDir() / "unmined-web").string() : path;
}

bool MapRenderer::render(const std::string &dimension) {
    if (rendering_) {
        return false;
    }

    const auto &paths = config_.getConfig().paths;
    const auto &mr = config_.getConfig().map_render;

    std::string unmined_cli = paths.unmined_cli;

    // Check if unmined-cli exists
    if (!std::filesystem::exists(unmined_cli)) {
        // Try installer path
        return false;
    }

    std::string output_dir = getOutputDir();
    std::filesystem::create_directories(output_dir);

    // Build command
    std::string cmd = unmined_cli +
        " web render"
        " --world=\"" + getWorldPath() + "\""
        " --output=\"" + output_dir + "\""
        " --imageformat=" + mr.image_format +
        " -c"
        " --zoomin=" + std::to_string(mr.max_zoom_level) +
        " --zoomout=" + std::to_string(mr.min_zoom_level) +
        " --dimension=" + dimension +
        " --players";

    rendering_ = true;

    // Run in background thread
    std::thread([this, cmd, output_dir]() {
        // Redirect stderr to stdout, capture via popen
        std::string full_cmd = cmd + " 2>&1";
        FILE *pipe = popen(full_cmd.c_str(), "r");
        if (!pipe) {
            rendering_ = false;
            return;
        }

        std::array<char, 256> buf;
        std::string output;
        while (fgets(buf.data(), buf.size(), pipe) != nullptr) {
            output += buf.data();
        }
        int ret = pclose(pipe);

        if (ret != 0) {
            // Log error (first 500 chars)
            if (output.size() > 500) output.resize(500);
            // Can't call getLogger() from non-server thread easily,
            // so we just mark rendering done
        } else {
            applyTitle();
            applyMarkers();
        }
        rendering_ = false;
    }).detach();

    return true;
}

void MapRenderer::applyTitle() {
    const auto &title = config_.getConfig().webserver.map_title;
    if (title == "default") return;

    std::string index_file = getOutputDir() + "/unmined.index.html";
    if (!std::filesystem::exists(index_file)) return;

    try {
        std::ifstream f(index_file);
        std::string content((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());
        f.close();

        std::string target = "UnminedMapProperties.worldName";
        std::string replacement = "\"" + title + "\"";
        size_t pos = content.find(target);
        if (pos != std::string::npos) {
            content.replace(pos, target.size(), replacement);
            std::ofstream out(index_file);
            out << content;
        }
    } catch (const std::exception &) {}
}

void MapRenderer::applyMarkers() {
    std::string output_dir = getOutputDir();

    nlohmann::json marker_list = nlohmann::json::array();
    for (const auto &m : config_.getMarkers()) {
        if (!m.contains("text") || m["text"].get<std::string>().empty()) continue;
        marker_list.push_back({
            {"x", m["x"]},
            {"z", m["z"]},
            {"text", m["text"]},
            {"image", "custom.pin.png"},
            {"imageAnchor", nlohmann::json::array({"0.5", 1})},
            {"imageScale", "0.3"},
            {"textColor", "red"},
            {"offsetX", 0},
            {"offsetY", 20},
            {"font", "bold 20px Calibri,sans serif"}
        });
    }

    nlohmann::json marker_data = {
        {"isEnabled", true},
        {"markers", marker_list}
    };

    std::string marker_file = output_dir + "/custom.markers.js";
    try {
        std::ofstream f(marker_file);
        f << "UnminedCustomMarkers = " << marker_data.dump(4);
    } catch (const std::exception &) {}
}

}  // namespace bdslm
