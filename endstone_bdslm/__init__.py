"""
BDSLM - Bedrock Server Live Map (卫星地图)
Endstone Python Plugin v1.0.0

作者: WMX20081122 / 千寻酱
"""

import json
import os
import subprocess
import threading
import time
import platform
import shutil
from http.server import HTTPServer, SimpleHTTPRequestHandler
from pathlib import Path
from typing import Optional

from endstone.plugin import Plugin
from endstone.command import Command, CommandSender
from endstone.event import PlayerJoinEvent, PlayerQuitEvent, PlayerMoveEvent, event_handler


class BDSLMPlugin(Plugin):
    """卫星地图插件主类"""

    # Plugin metadata (required by Endstone)
    version = "1.0.0"
    api_version = "0.11"

    def __init__(self):
        super().__init__()
        self._config: dict = {}
        self._markers: list = []
        self._players: dict = {}
        self._rendering: bool = False
        self._web_server: Optional[HTTPServer] = None
        self._web_thread: Optional[threading.Thread] = None

    # ================================================================
    # 插件生命周期
    # ================================================================

    def on_load(self) -> None:
        self.logger.info("§e[卫星地图] §f插件加载中...")
        self._load_config()
        self._load_markers()

    def on_enable(self) -> None:
        self.logger.info("§e[卫星地图] §f正在启动...")

        # 确保 unmined-cli 存在
        unmined_path = self._get_unmined_cli_path()
        if not unmined_path or not Path(unmined_path).exists():
            self.logger.warning("§e[卫星地图] §eunmined-cli 未找到，正在自动安装...")
            if self._install_unmined_cli():
                self.logger.info("§e[卫星地图] §aunmined-cli 安装成功!")
            else:
                self.logger.error("§e[卫星地图] §cunmined-cli 安装失败，地图渲染将不可用")
                self.logger.error("§e[卫星地图] §c请手动下载: https://unmined.net/downloads/")

        # 启动 Web 服务器
        self._start_web_server()

        # 注册事件
        self.register_events(self)

        # 定时任务：每秒更新玩家位置文件
        self.server.scheduler.run_task(self, self._update_players_file, delay=20, period=20)

        # 定时渲染
        auto_rend = self._config.get("autoRend", {})
        if auto_rend.get("enable", True):
            cycle_ticks = auto_rend.get("cycle", 30) * 60 * 20
            self.server.scheduler.run_task(self, self._auto_render, delay=cycle_ticks, period=cycle_ticks)

        # 初始渲染
        self._render("overworld")

        # 加载提示
        ws = self._config.get("webserver", {})
        boot_logo = """
§e  ___  _    _ ___    _____ ___   ___  _     _ _        _  
§e / __|| |  | / __|  /  __ \|  \ /  || |   | | |      | | 
§e \__ \| |/\| \__ \ | |    | |\/| || |   | | |      | | 
§e |___/ \  /\  |___/ | \__/\| |  | || |___| | |____  | |____
§e        \/  \/        \____/\_|  |_/\_____/\______/  \______/

§6  WMX SatMap §e| §f卫星地图插件
§f  作者: §bWMX20081122 §f协作: §b千寻酱
"""
        self.logger.info(boot_logo)
        bind = ws.get("bindAddress", "0.0.0.0")
        port = ws.get("port", 5110)
        self.logger.info(f"§e[卫星地图] §f地图地址: §bhttp://{bind}:{port}/")
        self.logger.info(f"§e[卫星地图] §f加载完毕 §e| §f版本 §b1.0.0")

    def on_disable(self) -> None:
        self.logger.info("§e[卫星地图] §f正在关闭...")
        self._stop_web_server()
        self._save_markers()
        self.logger.info("§e[卫星地图] §f插件已卸载")

    # ================================================================
    # 命令
    # ================================================================

    def on_command(self, sender: CommandSender, command: Command, args: list) -> bool:
        if command.name != "satmap":
            return False

        if not args:
            sender.send_message("§e用法: /satmap <render|status|reload|marker>")
            return True

        action = args[0]

        if action == "render":
            dim = args[1] if len(args) > 1 else "overworld"
            sender.send_message(f"§a开始渲染 {dim} 维度...")
            if self._render(dim):
                sender.send_message("§a渲染已启动!")
            else:
                if self._rendering:
                    sender.send_message("§c渲染正在进行中，请稍后再试!")
                else:
                    sender.send_message("§c渲染启动失败! 请检查 unmined-cli 是否已安装。")

        elif action == "status":
            sender.send_message("§e[卫星地图] 状态:")
            sender.send_message(f"  Web 服务器: {'§a运行中' if self._web_server else '§c已停止'}")
            sender.send_message(f"  端口: {self._config.get('webserver', {}).get('port', 5110)}")
            auto_on = self._config.get("autoRend", {}).get("enable", True)
            sender.send_message(f"  自动渲染: {'§a开启' if auto_on else '§c关闭'}")
            sender.send_message(f"  渲染周期: {self._config.get('autoRend', {}).get('cycle', 30)} 分钟")
            sender.send_message(f"  正在渲染: {'§a是' if self._rendering else '§c否'}")
            unmined = self._get_unmined_cli_path()
            sender.send_message(f"  unmined-cli: {'§a已安装' if unmined and Path(unmined).exists() else '§c未安装'}")

        elif action == "reload":
            self._load_config()
            self._load_markers()
            sender.send_message("§a配置已重载!")

        elif action == "marker":
            if len(args) < 2:
                sender.send_message("§e用法: /satmap marker <add|remove|list>")
                return True

            sub = args[1]
            if sub == "add" and len(args) >= 5:
                name = args[2]
                try:
                    x, z = int(args[3]), int(args[4])
                    for m in self._markers:
                        if m.get("text") == name:
                            sender.send_message("§c标记已存在!")
                            return True
                    self._markers.append({"x": x, "z": z, "text": name})
                    self._save_markers()
                    self._apply_markers()
                    sender.send_message(f"§a标记已添加: {name}")
                except ValueError:
                    sender.send_message("§c坐标必须是整数!")

            elif sub == "remove" and len(args) >= 3:
                name = args[2]
                before = len(self._markers)
                self._markers = [m for m in self._markers if m.get("text") != name]
                if len(self._markers) < before:
                    self._save_markers()
                    self._apply_markers()
                    sender.send_message(f"§a标记已移除: {name}")
                else:
                    sender.send_message("§c标记不存在!")

            elif sub == "list":
                found = False
                for m in self._markers:
                    text = m.get("text", "")
                    if text:
                        sender.send_message(f"§7- {text} ({m['x']}, {m['z']})")
                        found = True
                if not found:
                    sender.send_message("§7没有标记")
            else:
                sender.send_message("§e用法: /satmap marker <add|remove|list>")

        else:
            sender.send_message(f"§c未知操作: {action}")

        return True

    # ================================================================
    # 事件处理
    # ================================================================

    @event_handler
    def _on_player_join(self, event: PlayerJoinEvent) -> None:
        name = event.player.name
        self._players[name] = {"x": 0, "y": 0, "z": 0, "dim": "overworld"}

    @event_handler
    def _on_player_quit(self, event: PlayerQuitEvent) -> None:
        name = event.player.name
        self._players.pop(name, None)

    @event_handler
    def _on_player_move(self, event: PlayerMoveEvent) -> None:
        player = event.player
        loc = player.location
        dim = "overworld"
        try:
            dim = loc.dimension.name
        except Exception:
            pass
        self._players[player.name] = {
            "x": loc.x, "y": loc.y, "z": loc.z, "dim": dim
        }

    # ================================================================
    # 配置管理
    # ================================================================

    @property
    def data_dir(self) -> Path:
        return Path(self.data_folder)

    @property
    def config_path(self) -> Path:
        return self.data_dir / "config.json"

    @property
    def markers_path(self) -> Path:
        return self.data_dir / "markers.json"

    @property
    def players_path(self) -> Path:
        return self.data_dir / "players.json"

    def _load_config(self) -> None:
        defaults = {
            "webserver": {
                "port": 5110,
                "mapTitle": "default",
                "bindAddress": "0.0.0.0"
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
                "unminedCli": "",
                "worldPath": "",
                "outputDir": ""
            }
        }

        if self.config_path.exists():
            try:
                with open(self.config_path) as f:
                    user_config = json.load(f)
                self._merge_config(defaults, user_config)
            except Exception as e:
                self.logger.warning(f"配置加载失败: {e}")

        self._config = defaults
        self._save_config()

    def _save_config(self) -> None:
        self.data_dir.mkdir(parents=True, exist_ok=True)
        with open(self.config_path, "w") as f:
            json.dump(self._config, f, indent=4, ensure_ascii=False)

    def _merge_config(self, base: dict, override: dict) -> None:
        for k, v in override.items():
            if k in base and isinstance(base[k], dict) and isinstance(v, dict):
                self._merge_config(base[k], v)
            else:
                base[k] = v

    def _load_markers(self) -> None:
        if self.markers_path.exists():
            try:
                with open(self.markers_path) as f:
                    self._markers = json.load(f)
            except Exception:
                self._markers = [{"x": 0, "z": 0, "text": ""}]
        else:
            self._markers = [{"x": 0, "z": 0, "text": ""}]
            self._save_markers()

    def _save_markers(self) -> None:
        self.data_dir.mkdir(parents=True, exist_ok=True)
        with open(self.markers_path, "w") as f:
            json.dump(self._markers, f, indent=4, ensure_ascii=False)

    # ================================================================
    # unmined-cli 管理
    # ================================================================

    def _get_unmined_cli_path(self) -> Optional[str]:
        """获取 unmined-cli 路径"""
        # 1. 从配置读取
        cfg_path = self._config.get("paths", {}).get("unminedCli", "")
        if cfg_path and Path(cfg_path).exists():
            return cfg_path

        # 2. 默认路径
        default = self.data_dir / "unmined-cli" / "unmined-cli"
        if default.exists():
            return str(default)

        return None

    def _install_unmined_cli(self) -> bool:
        """自动安装 unmined-cli"""
        # 检测平台
        machine = platform.machine().lower()
        if machine in ("aarch64", "arm64"):
            plat = "linux-arm64"
        elif machine in ("armv7l", "armhf"):
            plat = "linux-arm"
        else:
            plat = "linux-x64"

        if plat == "linux-x64":
            url = "https://unmined.net/download/unmined-cli-linux-x64-dev/"
        elif plat == "linux-arm64":
            url = "https://unmined.net/download/unmined-cli-linux-arm64-dev/"
        else:
            url = "https://unmined.net/download/unmined-cli-linux-x64-dev/"

        target_dir = self.data_dir / "unmined-cli"
        tmp_archive = self.data_dir / "unmined-cli-download.tar.gz"

        try:
            # 下载
            ret = subprocess.run(
                ["curl", "-L", "-o", str(tmp_archive), url],
                capture_output=True, timeout=300
            )
            if ret.returncode != 0 or not tmp_archive.exists():
                return False

            # 解压
            target_dir.mkdir(parents=True, exist_ok=True)
            ret = subprocess.run(
                ["tar", "xzf", str(tmp_archive), "-C", "/tmp/"],
                capture_output=True, timeout=60
            )
            if ret.returncode != 0:
                return False

            # 找到解压的目录
            extracted = None
            for entry in Path("/tmp").iterdir():
                if "unmined-cli" in entry.name and entry.is_dir():
                    extracted = entry
                    break

            if not extracted:
                return False

            # 复制文件
            for item in extracted.rglob("*"):
                if item.is_file():
                    rel = item.relative_to(extracted)
                    dest = target_dir / rel
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(item, dest)

            # 设置可执行权限
            bin_path = target_dir / "unmined-cli"
            if bin_path.exists():
                bin_path.chmod(bin_path.stat().st_mode | 0o111)

            # 更新配置
            self._config["paths"]["unminedCli"] = str(bin_path)
            self._save_config()

            # 清理
            tmp_archive.unlink(missing_ok=True)
            shutil.rmtree(extracted, ignore_errors=True)

            return bin_path.exists()

        except Exception as e:
            self.logger.error(f"安装失败: {e}")
            return False

    # ================================================================
    # 地图渲染
    # ================================================================

    def _render(self, dimension: str = "overworld") -> bool:
        """启动地图渲染"""
        if self._rendering:
            return False

        unmined_path = self._get_unmined_cli_path()
        if not unmined_path:
            return False

        paths = self._config.get("paths", {})
        mr = self._config.get("mapRender", {})

        world_path = paths.get("worldPath", "") or "./worlds/Bedrock level/"
        output_dir = paths.get("outputDir", "") or str(self.data_dir / "unmined-web")

        Path(output_dir).mkdir(parents=True, exist_ok=True)

        cmd = [
            unmined_path,
            "web", "render",
            f"--world={world_path}",
            f"--output={output_dir}",
            f"--imageformat={mr.get('imageFormat', 'webp')}",
            "-c",
            f"--zoomin={mr.get('maxZoomLevel', -2)}",
            f"--zoomout={mr.get('minZoomLevel', 4)}",
            f"--dimension={dimension}",
            "--players"
        ]

        self._rendering = True

        def run_render():
            try:
                result = subprocess.run(cmd, capture_output=True, timeout=600)
                if result.returncode == 0:
                    self._apply_title()
                    self._apply_markers()
                    self.logger.info(f"§e[卫星地图] §a{dimension} 维度渲染完成")
                else:
                    self.logger.error(f"§e[卫星地图] §c渲染失败: {result.stderr.decode(errors='ignore')[:200]}")
            except subprocess.TimeoutExpired:
                self.logger.error("§e[卫星地图] §c渲染超时!")
            except Exception as e:
                self.logger.error(f"§e[卫星地图] §c渲染异常: {e}")
            finally:
                self._rendering = False

        threading.Thread(target=run_render, daemon=True).start()
        return True

    def _auto_render(self) -> None:
        """定时渲染回调"""
        self._render("overworld")

    def _apply_title(self) -> None:
        """修改地图标题"""
        title = self._config.get("webserver", {}).get("mapTitle", "default")
        if title == "default":
            return

        output_dir = self._config.get("paths", {}).get("outputDir", "") or str(self.data_dir / "unmined-web")
        index_file = Path(output_dir) / "unmined.index.html"
        if not index_file.exists():
            return

        try:
            content = index_file.read_text(encoding="utf-8")
            target = "UnminedMapProperties.worldName"
            replacement = f'"{title}"'
            content = content.replace(target, replacement, 1)
            index_file.write_text(content, encoding="utf-8")
        except Exception:
            pass

    def _apply_markers(self) -> None:
        """生成标记文件"""
        output_dir = self._config.get("paths", {}).get("outputDir", "") or str(self.data_dir / "unmined-web")
        marker_list = []
        for m in self._markers:
            text = m.get("text", "")
            if not text:
                continue
            marker_list.append({
                "x": m["x"], "z": m["z"], "text": text,
                "image": "custom.pin.png",
                "imageAnchor": ["0.5", 1],
                "imageScale": "0.3",
                "textColor": "red",
                "offsetX": 0, "offsetY": 20,
                "font": "bold 20px Calibri,sans serif"
            })

        marker_data = {"isEnabled": True, "markers": marker_list}
        marker_file = Path(output_dir) / "custom.markers.js"
        try:
            marker_file.write_text(f"UnminedCustomMarkers = {json.dumps(marker_data, indent=4)}")
        except Exception:
            pass

    # ================================================================
    # Web 服务器
    # ================================================================

    def _start_web_server(self) -> None:
        """启动 HTTP 服务器"""
        ws = self._config.get("webserver", {})
        bind = ws.get("bindAddress", "0.0.0.0")
        port = ws.get("port", 5110)
        output_dir = self._config.get("paths", {}).get("outputDir", "") or str(self.data_dir / "unmined-web")

        Path(output_dir).mkdir(parents=True, exist_ok=True)

        plugin = self

        class MapHandler(SimpleHTTPRequestHandler):
            def __init__(self, *args, **kwargs):
                super().__init__(*args, directory=output_dir, **kwargs)

            def do_GET(self):
                if self.path == "/api/players":
                    data = json.dumps(plugin._players).encode()
                    self.send_response(200)
                    self.send_header("Content-Type", "application/json")
                    self.send_header("Access-Control-Allow-Origin", "*")
                    self.end_headers()
                    self.wfile.write(data)
                elif self.path == "/api/markers":
                    data = json.dumps(plugin._markers).encode()
                    self.send_response(200)
                    self.send_header("Content-Type", "application/json")
                    self.send_header("Access-Control-Allow-Origin", "*")
                    self.end_headers()
                    self.wfile.write(data)
                elif self.path == "/api/config":
                    data = json.dumps(plugin._config).encode()
                    self.send_response(200)
                    self.send_header("Content-Type", "application/json")
                    self.send_header("Access-Control-Allow-Origin", "*")
                    self.end_headers()
                    self.wfile.write(data)
                else:
                    super().do_GET()

            def log_message(self, format, *args):
                pass  # 静默日志

        try:
            self._web_server = HTTPServer((bind, port), MapHandler)
            self._web_thread = threading.Thread(
                target=self._web_server.serve_forever,
                daemon=True
            )
            self._web_thread.start()
            self.logger.info(f"§e[卫星地图] §fWeb 服务器已启动: {bind}:{port}")
        except Exception as e:
            self.logger.error(f"§e[卫星地图] §cWeb 服务器启动失败: {e}")

    def _stop_web_server(self) -> None:
        """停止 HTTP 服务器"""
        if self._web_server:
            self._web_server.shutdown()
            self._web_server = None
            self._web_thread = None

    # ================================================================
    # 玩家位置更新
    # ================================================================

    def _update_players_file(self) -> None:
        """定时更新玩家位置文件"""
        try:
            self.data_dir.mkdir(parents=True, exist_ok=True)
            with open(self.players_path, "w") as f:
                json.dump(self._players, f, ensure_ascii=False)
        except Exception:
            pass
