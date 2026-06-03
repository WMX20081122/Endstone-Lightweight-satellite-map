#include "bdslm/bdslm_plugin.h"
#include "bdslm/config_manager.h"
#include "bdslm/map_renderer.h"
#include "bdslm/player_tracker.h"
#include "bdslm/web_server.h"
#include "bdslm/unmined_installer.h"

#include <endstone/endstone.hpp>
#include <endstone/event/player/player_join_event.h>
#include <endstone/event/player/player_quit_event.h>
#include <endstone/event/player/player_move_event.h>
#include <endstone/scheduler/scheduler.h>
#include <filesystem>

namespace bdslm {

// ============================================================
// ENDSTONE_PLUGIN 宏 — 注册命令和权限
// ============================================================
ENDSTONE_PLUGIN("bdslm", "1.0.0", BDSLMPlugin)
{
    description = "BDSLM - Bedrock Server Live Map (卫星地图)";
    website = "https://github.com/WMX20081122/endstone-bdslm";
    authors = {"WMX20081122", "千寻酱"};

    command("bdslm")
        .description("BDSLM 地图管理")
        .usages("/bdslm <render|status|reload|marker>")
        .permissions("bdslm.use");

    permission("bdslm.use")
        .description("使用 BDSLM 命令")
        .default_(endstone::PermissionDefault::Operator);
}

// ============================================================
// onLoad
// ============================================================
void BDSLMPlugin::onLoad() {
    auto data_dir = getDataFolder();
    std::filesystem::create_directories(data_dir);

    config_ = std::make_unique<ConfigManager>(data_dir);
    config_->load();
    config_->loadMarkers();

    tracker_ = std::make_unique<PlayerTracker>();
    renderer_ = std::make_unique<MapRenderer>(*config_);
    installer_ = std::make_unique<UnminedInstaller>(data_dir);

    getLogger().info("BDSLM 加载中...");
}

// ============================================================
// onEnable
// ============================================================
void BDSLMPlugin::onEnable() {
    getLogger().info("BDSLM 正在启动...");

    // Auto-install unmined-cli if missing (async to avoid blocking server)
    if (!installer_->isInstalled()) {
        getLogger().info("未检测到 unmined-cli，正在后台自动安装...");
        installer_->ensureInstalledAsync([this](bool success, const std::string &error) {
            if (success) {
                getLogger().info("§aunmined-cli 安装成功! 地图渲染现在可用。");
                config_->getConfig().paths.unmined_cli = installer_->getBinaryPath().string();
                config_->save();
                // Do initial render now that unmined-cli is available
                renderer_->render("overworld");
            } else {
                getLogger().error("§cunmined-cli 自动安装失败: {}", error);
                getLogger().error("§c请安装 lip、7z 或 tar 后重启服务器。");
                getLogger().error("§c或手动下载 unmined-cli 到: {}", installer_->getBinaryPath().parent_path().string());
                getLogger().error("§c下载地址: https://unmined.net/downloads/");
                getLogger().error("§c地图渲染功能已禁用。");
            }
        });
    }

    // Register event handlers
    registerEvent<endstone::PlayerJoinEvent>([this](auto &event) {
        tracker_->onJoin(event.getPlayer().getName());
    });
    registerEvent<endstone::PlayerQuitEvent>([this](auto &event) {
        tracker_->onQuit(event.getPlayer().getName());
    });
    registerEvent<endstone::PlayerMoveEvent>([this](auto &event) {
        auto &player = event.getPlayer();
        auto loc = player.getLocation();
        std::string dim = "overworld";
        try {
            dim = loc.getDimension().getName();
        } catch (...) {}
        tracker_->onMove(player.getName(), loc.getX(), loc.getY(), loc.getZ(), dim);
    });

    // Start web server
    web_server_ = std::make_unique<WebServer>(*config_, *tracker_);
    if (!web_server_->start()) {
        getLogger().error("§cWeb 服务器启动失败!");
    }

    // Schedule player position file updates (every second = 20 ticks)
    getServer().getScheduler().runTaskTimer(*this, [this]() {
        tracker_->updateFile(config_->getPlayersPath());
    }, 20, 20);

    // Schedule auto-render
    const auto &auto_rend = config_->getConfig().auto_rend;
    if (auto_rend.enable) {
        int cycle_ticks = auto_rend.cycle * 60 * 20;
        getServer().getScheduler().runTaskTimer(*this, [this]() {
            renderer_->render("overworld");
        }, cycle_ticks, cycle_ticks);
    }

    // Initial render
    renderer_->render("overworld");

    const auto &ws = config_->getConfig().webserver;
    getLogger().info("§aBDSLM 已就绪! 🗺️ 浏览器访问 §bhttp://{}:{}/", ws.bind_address, ws.port);
}

// ============================================================
// onDisable
// ============================================================
void BDSLMPlugin::onDisable() {
    getLogger().info("BDSLM 正在关闭...");
    if (web_server_) {
        web_server_->stop();
    }
    config_->saveMarkers();
    getLogger().info("BDSLM 已停止。再见! 👋");
}

// ============================================================
// 命令处理
// ============================================================
bool BDSLMPlugin::onCommand(endstone::CommandSender &sender, const endstone::Command &command,
                             const std::vector<std::string> &args) {
    if (command.getName() != "bdslm") {
        return false;
    }

    if (args.empty()) {
        sender.sendMessage("§e用法: /bdslm <render|status|reload|marker>");
        return true;
    }

    const auto &action = args[0];

    if (action == "render") {
        std::string dim = args.size() > 1 ? args[1] : "overworld";
        sender.sendMessage("§a开始渲染 " + dim + " 维度...");
        if (renderer_->render(dim)) {
            sender.sendMessage("§a渲染已启动!");
        } else {
            if (renderer_->isRendering()) {
                sender.sendMessage("§c渲染正在进行中，请稍后再试!");
            } else {
                sender.sendMessage("§c渲染启动失败! 请检查 unmined-cli 是否已安装。");
            }
        }
    }
    else if (action == "status") {
        sender.sendMessage("§eBDSLM 状态:");
        bool web_running = web_server_ && web_server_->isRunning();
        sender.sendMessage(std::string("  Web 服务器: ") + (web_running ? "§a运行中" : "§c已停止"));
        sender.sendMessage("  端口: " + std::to_string(config_->getConfig().webserver.port));
        bool auto_on = config_->getConfig().auto_rend.enable;
        sender.sendMessage(std::string("  自动渲染: ") + (auto_on ? "§a开启" : "§c关闭"));
        sender.sendMessage("  渲染周期: " + std::to_string(config_->getConfig().auto_rend.cycle) + " 分钟");
        sender.sendMessage(std::string("  正在渲染: ") + (renderer_->isRendering() ? "§a是" : "§c否"));
        bool installed = installer_->isInstalled();
        sender.sendMessage(std::string("  unmined-cli: ") + (installed ? "§a已安装" : "§c未安装"));
    }
    else if (action == "reload") {
        config_->reload();
        sender.sendMessage("§a配置已重载!");
    }
    else if (action == "marker") {
        if (args.size() < 2) {
            sender.sendMessage("§e用法: /bdslm marker <add|remove|list>");
            return true;
        }

        const auto &sub = args[1];
        if (sub == "add" && args.size() >= 5) {
            const auto &name = args[2];
            try {
                int x = std::stoi(args[3]);
                int z = std::stoi(args[4]);

                auto &markers = config_->getMarkers();
                for (const auto &m : markers) {
                    if (m.contains("text") && m["text"] == name) {
                        sender.sendMessage("§c标记已存在!");
                        return true;
                    }
                }
                markers.push_back({{"x", x}, {"z", z}, {"text", name}});
                config_->saveMarkers();
                renderer_->applyMarkers();
                sender.sendMessage("§a标记已添加: " + name);
            } catch (const std::exception &) {
                sender.sendMessage("§c坐标必须是整数!");
            }
        }
        else if (sub == "remove" && args.size() >= 3) {
            const auto &name = args[2];
            auto &markers = config_->getMarkers();
            size_t before = markers.size();
            markers.erase(std::remove_if(markers.begin(), markers.end(),
                [&name](const nlohmann::json &m) {
                    return m.contains("text") && m["text"] == name;
                }), markers.end());
            if (markers.size() < before) {
                config_->saveMarkers();
                renderer_->applyMarkers();
                sender.sendMessage("§a标记已移除: " + name);
            } else {
                sender.sendMessage("§c标记不存在!");
            }
        }
        else if (sub == "list") {
            auto &markers = config_->getMarkers();
            bool found = false;
            for (const auto &m : markers) {
                if (m.contains("text") && !m["text"].get<std::string>().empty()) {
                    sender.sendMessage("§7- " + m["text"].get<std::string>() +
                        " (" + std::to_string(m["x"].get<int>()) +
                        ", " + std::to_string(m["z"].get<int>()) + ")");
                    found = true;
                }
            }
            if (!found) {
                sender.sendMessage("§7没有标记");
            }
        }
        else {
            sender.sendMessage("§e用法: /bdslm marker <add|remove|list>");
        }
    }
    else {
        sender.sendMessage("§c未知操作: " + action);
    }

    return true;
}

}  // namespace bdslm
