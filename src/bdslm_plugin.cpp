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
ENDSTONE_PLUGIN("wmx_satmap", "1.0.0", BDSLMPlugin)
{
    description = "WMX SatMap - 卫星地图插件";
    website = "https://github.com/WMX20081122/bdslm-cpp";
    authors = {"WMX20081122", "千寻酱"};

    command("satmap")
        .description("卫星地图管理")
        .usages("/satmap <render|status|reload|marker>")
        .permissions("wmx_satmap.use");

    permission("wmx_satmap.use")
        .description("使用卫星地图命令")
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

    getLogger().info("§e[卫星地图] §f插件加载中...");
}

// ============================================================
// onEnable
// ============================================================
void BDSLMPlugin::onEnable() {
    getLogger().info("§e[卫星地图] §f正在启动...");

    // Auto-install unmined-cli if missing
    if (!installer_->isInstalled()) {
        getLogger().info("§e[卫星地图] §f未检测到 unmined-cli，正在自动安装...");
        if (installer_->ensureInstalled()) {
            getLogger().info("§e[卫星地图] §aunmined-cli 安装成功!");
            config_->getConfig().paths.unmined_cli = installer_->getBinaryPath().string();
            config_->save();
        } else {
            getLogger().error("§e[卫星地图] §cunmined-cli 自动安装失败，地图渲染将不可用");
            getLogger().error("§e[卫星地图] §c请手动下载: https://unmined.net/downloads/");
        }
    } else {
        // Already installed — ensure path is set
        auto &unmined_path = config_->getConfig().paths.unmined_cli;
        if (unmined_path.empty()) {
            unmined_path = installer_->getBinaryPath().string();
            config_->save();
        }
    }

    // Register event handlers
    registerEvent<endstone::PlayerJoinEvent>(
        [this](auto &event) { onPlayerJoin(event); }
    );
    registerEvent<endstone::PlayerQuitEvent>(
        [this](auto &event) { onPlayerQuit(event); }
    );
    registerEvent<endstone::PlayerMoveEvent>(
        [this](auto &event) { onPlayerMove(event); }
    );

    // Start web server
    web_server_ = std::make_unique<WebServer>(*config_, *tracker_);
    if (!web_server_->start()) {
        getLogger().error("§e[卫星地图] §cWeb 服务器启动失败!");
    }

    // Schedule player position file updates (every second = 20 ticks)
    getServer().getScheduler().runTask(*this, [this]() {
        tracker_->updateFile(config_->getPlayersPath());
    }, 20, 20);

    // Schedule auto-render
    const auto &auto_rend = config_->getConfig().auto_rend;
    if (auto_rend.enable) {
        int cycle_ticks = auto_rend.cycle * 60 * 20;
        getServer().getScheduler().runTask(*this, [this]() {
            renderer_->render("overworld");
        }, cycle_ticks, cycle_ticks);
    }

    // Initial render
    renderer_->render("overworld");

    // ============================================================
    // 加载提示
    // ============================================================
    const auto &ws = config_->getConfig().webserver;

    const std::string boot_logo = R"(
§e  ___  _    _ ___    _____ ___   ___  _     _ _        _  
§e / __|| |  | / __|  /  __ \|  \ /  || |   | | |      | | 
§e \__ \| |/\| \__ \ | |    | |\/| || |   | | |      | | 
§e |___/ \  /\  |___/ | \__/\| |  | || |___| | |____  | |____
§e        \/  \/        \____/\_|  |_/\_____/\______/  \______/
§e
§6  WMX SatMap §e| §f卫星地图插件
§f  作者: §bWMX20081122 §f协作: §b千寻酱
)";
    getLogger().info(boot_logo);
    getLogger().info("§e[卫星地图] §f地图地址: §bhttp://{}:{}/", ws.bind_address, ws.port);
    getLogger().info("§e[卫星地图] §f加载完毕 §e| §f版本 §b{}", getDescription().getVersion());
}

// ============================================================
// onDisable
// ============================================================
void BDSLMPlugin::onDisable() {
    getLogger().info("§e[卫星地图] §f正在关闭...");
    if (web_server_) {
        web_server_->stop();
    }
    config_->saveMarkers();
    getLogger().info("§e[卫星地图] §f插件已卸载");
}

// ============================================================
// 命令处理
// ============================================================
bool BDSLMPlugin::onCommand(endstone::CommandSender &sender, const endstone::Command &command,
                             const std::vector<std::string> &args) {
    if (command.getName() != "satmap") {
        return false;
    }

    if (args.empty()) {
        sender.sendMessage("§e用法: /satmap <render|status|reload|marker>");
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
        sender.sendMessage("§e[卫星地图] 状态:");
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
            sender.sendMessage("§e用法: /satmap marker <add|remove|list>");
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
            sender.sendMessage("§e用法: /satmap marker <add|remove|list>");
        }
    }
    else {
        sender.sendMessage("§c未知操作: " + action);
    }

    return true;
}

// ============================================================
// 事件处理
// ============================================================
void BDSLMPlugin::onPlayerJoin(endstone::PlayerJoinEvent &event) {
    tracker_->onJoin(event.getPlayer().getName());
}

void BDSLMPlugin::onPlayerQuit(endstone::PlayerQuitEvent &event) {
    tracker_->onQuit(event.getPlayer().getName());
}

void BDSLMPlugin::onPlayerMove(endstone::PlayerMoveEvent &event) {
    auto &player = event.getPlayer();
    auto loc = player.getLocation();
    std::string dim = "overworld";
    try {
        dim = loc.getDimension().getName();
    } catch (...) {}
    tracker_->onMove(player.getName(), loc.getX(), loc.getY(), loc.getZ(), dim);
}

}  // namespace bdslm
