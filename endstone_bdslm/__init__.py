"""
BDSLM - Bedrock Server Live Map (卫星地图)
Endstone Python Plugin v1.0.1 - 精简版

只做卫星图渲染 + Web 服务器，不加聊天/命令/玩家标记
"""

import json
import subprocess
import threading
import time
from http.server import HTTPServer, BaseHTTPRequestHandler
from pathlib import Path
from typing import Optional

from endstone.plugin import Plugin
from endstone.command import Command, CommandSender


class BDSLMPlugin(Plugin):
    """卫星地图插件 - 精简版"""

    commands = {
        "slmrender": {
            "description": "手动触发卫星图渲染",
            "usages": ["/slmrender"],
            "permissions": ["bdslm.command.slmrender"],
        },
    }

    permissions = {
        "bdslm.command.slmrender": {
            "description": "手动渲染卫星图",
            "default": "op",
        },
    }

    def __init__(self):
        super().__init__()
        self._config: dict = {}
        self._rendering: bool = False
        self._render_lock = threading.Lock()
        self._last_render_time: float = 0
        self._render_process: Optional[subprocess.Popen] = None
        self._web_server: Optional[HTTPServer] = None
        self._web_thread: Optional[threading.Thread] = None

    # ================================================================
    # 生命周期
    # ================================================================

    def on_load(self) -> None:
        self.logger.info("§b[卫星地图] §f加载中...")
        self._load_config()

    def on_enable(self) -> None:
        self.logger.info("§b[卫星地图] §f启动中...")

        # 检查 unmined-cli
        unmined = self._get_unmined_path()
        if unmined:
            self.logger.info(f"§b[卫星地图] §funmined-cli: {unmined}")
        else:
            self.logger.warning("§e[卫星地图] §funmined-cli 未找到，渲染不可用")

        # 启动 Web
        if not self._start_web_server():
            self.logger.error("§c[卫星地图] §fWeb 服务器启动失败")

        # 定时渲染
        auto = self._config.get("autoRend", {})
        if auto.get("isEnabled", True):
            cycle_ticks = auto.get("cycle", 120) * 60 * 20
            self.server.scheduler.run_task(
                self, self._scheduled_render, delay=cycle_ticks, period=cycle_ticks
            )
            self.logger.info(f"§b[卫星地图] §f自动渲染: 每 {auto.get('cycle', 120)} 分钟")

        # 首次渲染
        self.server.scheduler.run_task(self, self._first_render, delay=100)

        ws_cfg = self._config.get("webserver", {})
        self.logger.info(f"§b[卫星地图] §f就绪 v1.0.1 | 端口 {ws_cfg.get('port', 5110)}")

    def on_command(self, sender: CommandSender, command: Command, args: list) -> bool:
        if command.name == "slmrender":
            if self._rendering:
                sender.send_message("§c渲染正在进行中")
                return True
            self._do_render("overworld")
            sender.send_message("§a渲染已启动")
            return True
        return False

    def on_disable(self) -> None:
        self._stop_web_server()
        if self._render_process and self._render_process.poll() is None:
            self._render_process.terminate()
        self.logger.info("§b[卫星地图] §f已卸载")

    # ================================================================
    # 配置
    # ================================================================

    @property
    def data_dir(self) -> Path:
        return Path(self.data_folder)

    def _default_config(self) -> dict:
        return {
            "webserver": {
                "port": 5110,
                "bindAddress": "0.0.0.0",
            },
            "mapRender": {
                "zoomin": 1,
                "zoomout": -5,
            },
            "autoRend": {
                "isEnabled": True,
                "cycle": 120,
            },
        }

    def _load_config(self) -> None:
        cfg_path = self.data_dir / "config.json"
        defaults = self._default_config()
        if cfg_path.exists():
            try:
                with open(cfg_path, encoding="utf-8") as f:
                    user = json.load(f)
                self._deep_merge(defaults, user)
            except Exception as e:
                self.logger.warning(f"§e[卫星地图] §f配置加载失败: {e}")
        self._config = defaults
        self._save_config()

    def _save_config(self) -> None:
        self.data_dir.mkdir(parents=True, exist_ok=True)
        with open(self.data_dir / "config.json", "w", encoding="utf-8") as f:
            json.dump(self._config, f, indent=2, ensure_ascii=False)

    @staticmethod
    def _deep_merge(base: dict, override: dict) -> None:
        for k, v in override.items():
            if k in base and isinstance(base[k], dict) and isinstance(v, dict):
                BDSLMPlugin._deep_merge(base[k], v)
            else:
                base[k] = v

    # ================================================================
    # unmined-cli
    # ================================================================

    def _get_unmined_path(self) -> Optional[str]:
        cfg = self._config.get("unminedCliPath", "")
        if cfg and Path(cfg).exists():
            return cfg
        for name in ["unmined-cli", "unmined-cli.exe"]:
            default = self.data_dir / "unmined-cli" / name
            if default.exists():
                return str(default)
        return None

    # ================================================================
    # 渲染
    # ================================================================

    def _get_world_path(self) -> str:
        cfg = self._config.get("world", {}).get("path", "")
        if cfg and Path(cfg).exists():
            return cfg

        world_name = "Bedrock level"
        try:
            level = self.server.level
            if level:
                world_name = level.name
        except Exception:
            pass

        candidates = []
        for base in [Path("."), Path(self.data_folder).parent.parent]:
            candidates.append(base / "worlds" / world_name)
        for abs_base in ["/workspace/bedrock_server", "/home/container/bedrock_server", "/bedrock_server"]:
            candidates.append(Path(abs_base) / "worlds" / world_name)

        for c in candidates:
            if c.exists():
                return str(c.resolve())

        self.logger.warning(f"§e[卫星地图] §f未找到世界: {world_name}")
        return str(Path(".") / "worlds" / world_name)

    def _get_output_dir(self) -> str:
        cfg = self._config.get("outputDir", "")
        return cfg if cfg else str(self.data_dir / "unmined-web")

    def _first_render(self) -> None:
        self._do_render("overworld")

    def _scheduled_render(self) -> None:
        self._do_render("overworld")

    def _do_render(self, dimension: str = "overworld") -> None:
        with self._render_lock:
            if self._rendering:
                return
            self._rendering = True

        unmined = self._get_unmined_path()
        if not unmined:
            self.logger.warning("§e[卫星地图] §funmined-cli 未找到，跳过")
            with self._render_lock:
                self._rendering = False
            return

        world_path = self._get_world_path()
        output_dir = self._get_output_dir()
        render_cfg = self._config.get("mapRender", {})
        Path(output_dir).mkdir(parents=True, exist_ok=True)

        dim_map = {"overworld": "0", "nether": "1", "end": "2"}
        cmd = [
            unmined, "web", "render",
            f"--world={world_path}",
            f"--output={output_dir}",
            "--imageformat=png",
            f"--zoomin={render_cfg.get('zoomin', 1)}",
            f"--zoomout={render_cfg.get('zoomout', -5)}",
            f"--dimension={dim_map.get(dimension, '0')}",
        ]

        self.logger.info(f"§b[卫星地图] §f开始渲染 {dimension}...")

        def run():
            start = time.time()
            try:
                self._save_hold()
                self._render_process = subprocess.Popen(
                    cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                    cwd=str(Path(unmined).parent),
                )
                stdout, stderr = self._render_process.communicate(timeout=600)
                rc = self._render_process.returncode
                self._render_process = None
                elapsed = (time.time() - start) / 60
                if rc == 0:
                    self.logger.info(f"§b[卫星地图] §f渲染完成 | {elapsed:.2f} 分钟")
                else:
                    err = stderr.decode(errors="ignore")[:500]
                    self.logger.error(f"§c[卫星地图] §f渲染失败 | {elapsed:.2f} 分钟\n{err}")
            except subprocess.TimeoutExpired:
                self.logger.error("§c[卫星地图] §f渲染超时")
                if self._render_process:
                    self._render_process.kill()
                    self._render_process = None
            except Exception as e:
                self.logger.error(f"§c[卫星地图] §f渲染异常: {e}")
            finally:
                self._save_resume()
                with self._render_lock:
                    self._rendering = False
                self._last_render_time = time.time()

        threading.Thread(target=run, daemon=True).start()

    def _save_hold(self) -> None:
        try:
            sender = self.server.command_sender
            self.server.scheduler.run_task(
                self, lambda: self.server.dispatch_command(sender, "save hold")
            )
            time.sleep(3)
        except Exception as e:
            self.logger.warning(f"§e[卫星地图] §fsave hold 失败: {e}")

    def _save_resume(self) -> None:
        try:
            sender = self.server.command_sender
            self.server.scheduler.run_task(
                self, lambda: self.server.dispatch_command(sender, "save resume")
            )
        except Exception as e:
            self.logger.warning(f"§e[卫星地图] §fsave resume 失败: {e}")

    # ================================================================
    # Web 服务器
    # ================================================================

    def _start_web_server(self) -> bool:
        ws_cfg = self._config.get("webserver", {})
        bind = ws_cfg.get("bindAddress", "0.0.0.0")
        port = ws_cfg.get("port", 5110)
        output_dir = self._get_output_dir()
        Path(output_dir).mkdir(parents=True, exist_ok=True)

        plugin = self

        class Handler(BaseHTTPRequestHandler):
            def do_GET(self):
                import urllib.parse, mimetypes

                path = self.path.split("?")[0]

                # API
                if path == "/api/status":
                    self._json({"rendering": plugin._rendering, "lastRender": plugin._last_render_time})
                    return

                # 静态文件 → unmined-web
                if path == "/":
                    path = "/index.html"

                rel = urllib.parse.unquote(path.lstrip("/"))
                fp = Path(output_dir) / rel

                try:
                    fp = fp.resolve()
                    base = Path(output_dir).resolve()
                    if not str(fp).startswith(str(base)):
                        self.send_error(403); return
                except Exception:
                    self.send_error(403); return

                if not fp.exists() or fp.is_dir():
                    self.send_error(404); return

                try:
                    mime, _ = mimetypes.guess_type(str(fp))
                    self.send_response(200)
                    self.send_header("Content-Type", mime or "application/octet-stream")
                    self.send_header("Content-Length", str(fp.stat().st_size))
                    self.end_headers()
                    with open(fp, "rb") as f:
                        self.wfile.write(f.read())
                except Exception:
                    self.send_error(500)

            def _json(self, data):
                body = json.dumps(data, ensure_ascii=False).encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "application/json; charset=utf-8")
                self.end_headers()
                self.wfile.write(body)

            def log_message(self, *_): pass

        for addr in [bind, "127.0.0.1"]:
            if addr is None:
                continue
            try:
                self._web_server = HTTPServer((addr, port), Handler)
                self._web_thread = threading.Thread(target=self._web_server.serve_forever, daemon=True)
                self._web_thread.start()
                self.logger.info(f"§b[卫星地图] §fWeb: {addr}:{port}")
                return True
            except OSError as e:
                self.logger.warning(f"§e[卫星地图] §f绑定 {addr}:{port} 失败: {e}")

        self.logger.error("§c[卫星地图] §fWeb 启动失败")
        return False

    def _stop_web_server(self) -> None:
        if self._web_server:
            self._web_server.shutdown()
            self._web_server = None
