#pragma once

#include <endstone/plugin/plugin.h>
#include <endstone/command/command.h>
#include <endstone/command/command_sender.h>
#include <memory>
#include <string>
#include <vector>

namespace endstone {
class PlayerJoinEvent;
class PlayerQuitEvent;
class PlayerMoveEvent;
}

namespace bdslm {

class WebServer;
class MapRenderer;
class ConfigManager;
class PlayerTracker;
class UnminedInstaller;

class BDSLMPlugin : public endstone::Plugin {
public:
    BDSLMPlugin() = default;
    ~BDSLMPlugin() override = default;

    void onLoad() override;
    void onEnable() override;
    void onDisable() override;
    bool onCommand(endstone::CommandSender &sender, const endstone::Command &command,
                   const std::vector<std::string> &args) override;

    // Getters
    ConfigManager &getConfigManager() { return *config_; }
    MapRenderer &getMapRenderer() { return *renderer_; }
    PlayerTracker &getPlayerTracker() { return *tracker_; }
    WebServer &getWebServer() { return *web_server_; }

private:
    std::unique_ptr<ConfigManager> config_;
    std::unique_ptr<MapRenderer> renderer_;
    std::unique_ptr<PlayerTracker> tracker_;
    std::unique_ptr<WebServer> web_server_;
    std::unique_ptr<UnminedInstaller> installer_;
};

}  // namespace bdslm
