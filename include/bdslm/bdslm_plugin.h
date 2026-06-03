#pragma once

#include <endstone/plugin/plugin.h>
#include <memory>

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

    // Event handlers
    void onPlayerJoin(endstone::PlayerJoinEvent &event);
    void onPlayerQuit(endstone::PlayerQuitEvent &event);
    void onPlayerMove(endstone::PlayerMoveEvent &event);

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
