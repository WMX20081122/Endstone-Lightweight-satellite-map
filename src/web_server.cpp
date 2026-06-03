#include "bdslm/web_server.h"
#include "bdslm/config_manager.h"
#include "bdslm/player_tracker.h"
#include <httplib.h>
#include <filesystem>

namespace bdslm {

WebServer::WebServer(ConfigManager &config, PlayerTracker &tracker)
    : config_(config), tracker_(tracker) {}

WebServer::~WebServer() {
    stop();
}

bool WebServer::start() {
    const auto &ws = config_.getConfig().webserver;
    auto output_dir = config_.getDataDir() / "unmined-web";
    std::filesystem::create_directories(output_dir);

    try {
        server_ = std::make_unique<httplib::Server>();

        // API endpoints
        server_->Get("/api/players", [this](const httplib::Request &, httplib::Response &res) {
            res.set_content(tracker_.toJson().dump(), "application/json");
            res.set_header("Access-Control-Allow-Origin", "*");
        });

        server_->Get("/api/markers", [this](const httplib::Request &, httplib::Response &res) {
            try {
                std::ifstream f(config_.getMarkersPath());
                if (f.is_open()) {
                    std::string content((std::istreambuf_iterator<char>(f)),
                                        std::istreambuf_iterator<char>());
                    res.set_content(content, "application/json");
                } else {
                    res.set_content("[]", "application/json");
                }
            } catch (const std::exception &) {
                res.set_content("[]", "application/json");
            }
            res.set_header("Access-Control-Allow-Origin", "*");
        });

        server_->Get("/api/config", [this](const httplib::Request &, httplib::Response &res) {
            try {
                std::ifstream f(config_.getConfigPath());
                if (f.is_open()) {
                    std::string content((std::istreambuf_iterator<char>(f)),
                                        std::istreambuf_iterator<char>());
                    res.set_content(content, "application/json");
                } else {
                    res.set_content("{}", "application/json");
                }
            } catch (const std::exception &) {
                res.set_content("{}", "application/json");
            }
            res.set_header("Access-Control-Allow-Origin", "*");
        });

        // Static file serving from unmined-web directory
        server_->set_mount_point("/", output_dir.string());

        // Start in background thread
        running_ = true;
        thread_ = std::thread([this, &ws]() {
            if (!server_->listen(ws.bind_address, ws.port)) {
                running_ = false;
            }
        });

        return true;
    } catch (const std::exception &) {
        return false;
    }
}

void WebServer::stop() {
    if (server_) {
        server_->stop();
        if (thread_.joinable()) {
            thread_.join();
        }
        server_.reset();
        running_ = false;
    }
}

}  // namespace bdslm
