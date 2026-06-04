#!/usr/bin/env python3
"""
BDSLM - Bedrock Server Satellite Map Plugin (Endstone Python)

完全复刻 bdslm-js 的运行模式:
- 使用 unmined-cli 渲染地图
- 使用 Python HTTP 服务器提供网页 (替代 nginx)
- 配置文件格式与 bdslm-js 一致
- 标记点格式与 bdslm-js 一致
- 自动定时渲染
- 玩家位置追踪
"""

import json
import os
import subprocess
import threading
import time
import http.server
import functools
from pathlib import Path

from endstone.plugin import Plugin
from endstone.command import Command, CommandSender
from endstone.event import (
    PlayerJoinEvent,
    PlayerQuitEvent,
    PlayerMoveEvent,
    EventPriority,
    event_handler,
)


class BDSLMPlugin(Plugin):
    """BDSLM Endstone Python 插件主类"""
    api_version = "0.11"

    def on_load(self) -> None:
        self._players = {}
        self._web_server = None
        self._web_thread = None
        self._rendering = False
        self._config = {}
        self._markers = []

        # 路径
        self._data_dir = Path(self.data_folder)
        self._data_dir.mkdir(parents=True, exist_ok=True)

        self._config_path = self._data_dir / "config.json"
        self._markers_path = self._data_dir / "markers.json"
        self._players_path = self._data_dir / "players.json"

        # 加载配置
        self._load_config()
        self._load_markers()

        self.logger.info("BDSLM 加载中...")

    def on_enable(self) -> None:
        self.logger.info("BDSLM 正在启动...")

        # 注册事件 (Endstone 0.11.4: 使用 register_events 装饰器模式)
        self.register_events(self)

        # 启动 Web 服务器
        self._start_web_server()

        # 启动玩家位置更新定时器 (Endstone 0.11.4: run_task with delay + period)
        self.server.scheduler.run_task(self, self._update_players_file, delay=20, period=20)

        # 启动自动渲染
        if self._config.get("autoRend", {}).get("enable", True):
            cycle_ticks = self._config["autoRend"]["cycle"] * 60 * 20
            self.server.scheduler.run_task(self, self._auto_render, delay=cycle_ticks, period=cycle_ticks)

        # 首次渲染
        self._render_map()

        self.logger.info(f"§aBDSLM 已就绪! 🗺️ 浏览器访问 §bhttp://localhost:{self._config['webserver']['port']}/")

    def on_disable(self) -> None:
        self.logger.info("BDSLM 正在关闭...")
        self._stop_web_server()
        self._save_markers()
        self.logger.info("BDSLM 已停止。再见! 👋")

    # ==================== 命令 ====================

    def on_command(self, sender: CommandSender, command: Command, args: list[str]) -> bool:
        if command.name != "bdslm":
            return False

        if not args:
            sender.send_message("§e用法: /bdslm <render|status|reload|marker>")
            return True

        action = args[0]

        if action == "render":
            dim = args[1] if len(args) > 1 else "overworld"
            sender.send_message(f"§a开始渲染 {dim} 维度...")
            if self._render_map(dim):
                sender.send_message("§a渲染已启动!")
            else:
                sender.send_message("§c渲染启动失败!")

        elif action == "status":
            sender.send_message("§eBDSLM 状态:")
            running = self._web_server is not None
            sender.send_message(f"  Web 服务器: {'§a运行中' if running else '§c已停止'}")
            sender.send_message(f"  端口: {self._config['webserver']['port']}")
            auto = self._config.get("autoRend", {}).get("enable", True)
            sender.send_message(f"  自动渲染: {'§a开启' if auto else '§c关闭'}")
            cycle = self._config.get("autoRend", {}).get("cycle", 30)
            sender.send_message(f"  渲染周期: {cycle} 分钟")
            sender.send_message(f"  正在渲染: {'§a是' if self._rendering else '§c否'}")

        elif action == "reload":
            self._load_config()
            self._load_markers()
            sender.send_message("§a配置已重载!")

        elif action == "marker":
            if len(args) < 2:
                sender.send_message("§e用法: /bdslm marker <add|remove|list>")
                return True

            sub = args[1]
            if sub == "add" and len(args) >= 5:
                name = args[2]
                try:
                    x, z = int(args[3]), int(args[4])
                except ValueError:
                    sender.send_message("§c坐标必须是整数!")
                    return True
                # 检查重名
                for m in self._markers:
                    if m["text"] == name:
                        sender.send_message("§c标记已存在!")
                        return True
                self._markers.append({"x": x, "z": z, "text": name})
                self._save_markers()
                self._apply_markers()
                sender.send_message(f"§a标记已添加: {name}")

            elif sub == "remove" and len(args) >= 3:
                name = args[2]
                before = len(self._markers)
                self._markers = [m for m in self._markers if m["text"] != name]
                if len(self._markers) < before:
                    self._save_markers()
                    self._apply_markers()
                    sender.send_message(f"§a标记已移除: {name}")
                else:
                    sender.send_message("§c标记不存在!")

            elif sub == "list":
                for m in self._markers:
                    if m.get("text"):
                        sender.send_message(f"§7- {m['text']} ({m['x']}, {m['z']})")
                if not any(m.get("text") for m in self._markers):
                    sender.send_message("§7没有标记")

            else:
                sender.send_message("§e用法: /bdslm marker <add|remove|list>")

        else:
            sender.send_message(f"§c未知操作: {action}")

        return True

    # ==================== 配置 ====================

    def _load_config(self) -> None:
        # 默认配置（和 bdslm-js 一致）
        default = {
            "webserver": {
                "port": 5110,
                "mapTitle": "default",
                "bindAddress": "127.0.0.1"
            },
            "mapRender": {
                "maxZoomLevel": -2,
                "minZoomLevel": 4,
                "imageFormat": "webp"
            },
            "autoRend": {
                "enable": True,
                "cycle": 30
            },
            "paths": {
                "unminedCli": "./plugins/bdslm/unmined-cli/unmined-cli",
                "worldPath": "",
                "outputDir": ""
            }
        }

        if self._config_path.exists():
            try:
                with open(self._config_path, "r") as f:
                    user_config = json.load(f)
                # 递归合并
                self._deep_merge(default, user_config)
            except Exception as e:
                self.logger.error(f"配置加载失败: {e}")

        self._config = default

        # 保存配置（确保文件存在）
        with open(self._config_path, "w") as f:
            json.dump(self._config, f, indent=4, ensure_ascii=False)

    @staticmethod
    def _deep_merge(base: dict, override: dict) -> None:
        for k, v in override.items():
            if k in base and isinstance(base[k], dict) and isinstance(v, dict):
                BDSLMPlugin._deep_merge(base[k], v)
            else:
                base[k] = v

    def _load_markers(self) -> None:
        if self._markers_path.exists():
            try:
                with open(self._markers_path, "r") as f:
                    self._markers = json.load(f)
            except Exception:
                self._markers = [{"x": 0, "z": 0, "text": ""}]
        else:
            self._markers = [{"x": 0, "z": 0, "text": ""}]
            self._save_markers()

    def _save_markers(self) -> None:
        with open(self._markers_path, "w") as f:
            json.dump(self._markers, f, indent=4, ensure_ascii=False)

    # ==================== 渲染 ====================

    def _get_world_path(self) -> str:
        path = self._config["paths"].get("worldPath", "")
        if path:
            return path
        return "./worlds/Bedrock level/"

    def _get_output_dir(self) -> str:
        path = self._config["paths"].get("outputDir", "")
        if path:
            return path
        return str(self._data_dir / "unmined-web")

    def _render_map(self, dimension: str = "overworld") -> bool:
        if self._rendering:
            self.logger.warning("渲染正在进行中，跳过本次请求")
            return False

        unmined_cli = self._config["paths"]["unminedCli"]
        world_path = self._get_world_path()
        output_dir = self._get_output_dir()
        zoomin = self._config["mapRender"]["maxZoomLevel"]
        zoomout = self._config["mapRender"]["minZoomLevel"]
        img_fmt = self._config["mapRender"]["imageFormat"]

        # 确保 unmined-cli 存在
        if not os.path.isfile(unmined_cli):
            self.logger.error(f"unmined-cli 不存在: {unmined_cli}")
            return False

        # 确保 output 目录存在
        os.makedirs(output_dir, exist_ok=True)

        # 构建命令
        cmd = [
            unmined_cli, "web", "render",
            f"--world={world_path}",
            f"--output={output_dir}",
            f"--imageformat={img_fmt}",
            "-c",  # continue on chunk errors
            f"--zoomin={zoomin}",
            f"--zoomout={zoomout}",
            f"--dimension={dimension}",
            "--players",
        ]

        self.logger.info(f"启动地图渲染: {' '.join(cmd)}")
        self._rendering = True

        # 异步执行渲染
        def _run_render():
            try:
                result = subprocess.run(cmd, capture_output=True, timeout=600)
                if result.returncode != 0:
                    self.logger.error(f"unmined-cli 返回码: {result.returncode}")
                    if result.stderr:
                        self.logger.error(f"stderr: {result.stderr[:500].decode('utf-8', errors='replace')}")
                else:
                    self.logger.info("地图渲染完成!")
                    # 应用配置（标题 + 标记）
                    self._apply_title()
                    self._apply_markers()
            except subprocess.TimeoutExpired:
                self.logger.error("unmined-cli 渲染超时 (10分钟)")
            except Exception as e:
                self.logger.error(f"渲染失败: {e}")
            finally:
                self._rendering = False

        thread = threading.Thread(target=_run_render, daemon=True)
        thread.start()
        return True

    def _apply_title(self) -> None:
        """修改地图标题（和 bdslm-js 的 changeWebTitle 一致）"""
        title = self._config["webserver"].get("mapTitle", "default")
        if title == "default":
            return

        output_dir = self._get_output_dir()
        index_file = os.path.join(output_dir, "unmined.index.html")
        if not os.path.isfile(index_file):
            return

        try:
            with open(index_file, "r", encoding="utf-8") as f:
                content = f.read()

            content = content.replace("UnminedMapProperties.worldName", f'"{title}"')

            with open(index_file, "w", encoding="utf-8") as f:
                f.write(content)
        except Exception as e:
            self.logger.error(f"修改标题失败: {e}")

    def _apply_markers(self) -> None:
        """写入自定义标记（和 bdslm-js 的 addMarkers 一致）"""
        output_dir = self._get_output_dir()

        # 构建标记数据
        marker_list = []
        for m in self._markers:
            if not m.get("text"):
                continue
            marker_list.append({
                "x": m["x"],
                "z": m["z"],
                "text": m["text"],
                "image": "custom.pin.png",
                "imageAnchor": ["0.5", 1],
                "imageScale": "0.3",
                "textColor": "red",
                "offsetX": 0,
                "offsetY": 20,
                "font": "bold 20px Calibri,sans serif",
            })

        marker_data = {
            "isEnabled": True,
            "markers": marker_list,
        }

        marker_file = os.path.join(output_dir, "custom.markers.js")
        try:
            with open(marker_file, "w", encoding="utf-8") as f:
                f.write("UnminedCustomMarkers = " + json.dumps(marker_data, indent=4, ensure_ascii=False))
        except Exception as e:
            self.logger.error(f"写入标记失败: {e}")

    def _auto_render(self) -> None:
        """定时自动渲染（和 bdslm-js 的 setInterval(RendMap, cycle) 一致）"""
        self._render_map("overworld")

    # ==================== Web 服务器 ====================

    def _start_web_server(self) -> None:
        """启动 Python HTTP 服务器（替代 nginx）"""
        bind = self._config["webserver"]["bindAddress"]
        port = self._config["webserver"]["port"]
        web_root = self._get_output_dir()

        os.makedirs(web_root, exist_ok=True)

        # 创建自定义 handler
        data_dir = str(self._data_dir)

        class MapHandler(http.server.SimpleHTTPRequestHandler):
            """提供 unmined-web 静态文件和 API"""

            def __init__(self, *args, **kwargs):
                super().__init__(*args, directory=web_root, **kwargs)

            def do_GET(self):
                # API 端点
                if self.path == "/api/players":
                    self._serve_json(os.path.join(data_dir, "players.json"))
                elif self.path == "/api/markers":
                    self._serve_json(os.path.join(data_dir, "markers.json"))
                elif self.path == "/api/config":
                    config_path = os.path.join(data_dir, "config.json")
                    self._serve_json(config_path)
                else:
                    super().do_GET()

            def _serve_json(self, filepath):
                try:
                    with open(filepath, "r") as f:
                        data = f.read()
                    self.send_response(200)
                    self.send_header("Content-Type", "application/json")
                    self.send_header("Access-Control-Allow-Origin", "*")
                    self.end_headers()
                    self.wfile.write(data.encode())
                except FileNotFoundError:
                    self.send_response(200)
                    self.send_header("Content-Type", "application/json")
                    self.send_header("Access-Control-Allow-Origin", "*")
                    self.end_headers()
                    self.wfile.write(b"[]")
                except Exception as e:
                    self.send_error(500, str(e))

            def log_message(self, format, *args):
                pass  # 静默日志

        try:
            self._web_server = http.server.HTTPServer((bind, port), MapHandler)
            self._web_thread = threading.Thread(target=self._web_server.serve_forever, daemon=True)
            self._web_thread.start()
            self.logger.info(f"Web 服务器已启动 → §b{bind}:{port}")
        except Exception as e:
            self.logger.error(f"Web 服务器启动失败: {e}")

    def _stop_web_server(self) -> None:
        if self._web_server:
            self._web_server.shutdown()
            self._web_server = None

    # ==================== 玩家追踪 ====================

    @event_handler
    def _on_player_join(self, event: PlayerJoinEvent) -> None:
        player = event.player
        self._players[player.name] = {
            "name": player.name,
            "x": 0.0, "y": 0.0, "z": 0.0,
            "dimension": "overworld"
        }

    @event_handler
    def _on_player_quit(self, event: PlayerQuitEvent) -> None:
        name = event.player.name
        self._players.pop(name, None)

    @event_handler
    def _on_player_move(self, event: PlayerMoveEvent) -> None:
        player = event.player
        loc = player.location
        if player.name in self._players:
            self._players[player.name].update({
                "x": loc.x, "y": loc.y, "z": loc.z,
                "dimension": loc.dimension.name
            })

    def _update_players_file(self) -> None:
        """每秒更新 players.json"""
        try:
            players_list = list(self._players.values())
            with open(self._players_path, "w") as f:
                json.dump(players_list, f, ensure_ascii=False)
        except Exception:
            pass

__all__ = ["BDSLMPlugin"]
