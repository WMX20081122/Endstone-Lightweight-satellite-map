# endstone-bdslm

**BDSLM** — Bedrock Server Live Map plugin for [Endstone](https://github.com/EndstoneMC/endstone).

Real-time satellite map for your Minecraft Bedrock dedicated server, powered by [unmined-cli](https://unmined.net/).

## ✨ Features

- 🗺️ **Live satellite map** — auto-renders your world using unmined-cli
- 🌐 **Built-in web server** — no nginx required, just open in browser
- 👥 **Player tracking** — real-time player positions on the map
- 📍 **Custom markers** — add/remove named markers via in-game commands
- 🔄 **Auto-render** — configurable periodic map refresh
- 📦 **Auto-install** — unmined-cli is automatically downloaded on first run
- 🖥️ **Cross-platform** — compiles for Linux (.so) and Windows (.dll)

## 📋 Requirements

- Endstone >= 0.11.4
- BDS 1.26.x
- Linux or Windows server
- `curl` (for auto-installing unmined-cli on Linux)

## 🔧 Build

### Prerequisites

- CMake >= 3.15
- C++17 compiler (GCC/Clang/MSVC)

### Compile

```bash
git clone https://github.com/WMX20081122/endstone-bdslm.git
cd endstone-bdslm
mkdir build && cd build

# Linux
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Windows (Visual Studio Developer Command Prompt)
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

Output: `endstone_bdslm.so` (Linux) or `endstone_bdslm.dll` (Windows)

## 📦 Install

1. Copy `endstone_bdslm.so` (or `.dll`) to your `bedrock_server/plugins/` directory
2. Start the server
3. On first run, unmined-cli will be **automatically downloaded** to `plugins/bdslm/unmined-cli/`
4. The map will render and be available at `http://your-server:5110/`

## ⚙️ Configuration

Config file: `plugins/bdslm/config.json`

```json
{
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
        "enable": true,
        "cycle": 30
    },
    "paths": {
        "unminedCli": "./plugins/bdslm/unmined-cli/unmined-cli",
        "worldPath": "",
        "outputDir": ""
    }
}
```

| Key | Default | Description |
|-----|---------|-------------|
| `webserver.port` | `5110` | Web server port |
| `webserver.mapTitle` | `"default"` | Map title in browser (use world name if "default") |
| `webserver.bindAddress` | `"0.0.0.0"` | Bind address (`0.0.0.0` for external access) |
| `mapRender.maxZoomLevel` | `-2` | Maximum zoom in level |
| `mapRender.minZoomLevel` | `4` | Maximum zoom out level |
| `mapRender.imageFormat` | `"webp"` | Tile image format (`webp` or `png`) |
| `autoRend.enable` | `true` | Enable automatic periodic rendering |
| `autoRend.cycle` | `30` | Render interval in minutes |
| `paths.unminedCli` | `"./plugins/bdslm/unmined-cli/unmined-cli"` | Path to unmined-cli binary |
| `paths.worldPath` | `""` | World path (auto-detected if empty) |
| `paths.outputDir` | `""` | Output directory (auto-detected if empty) |

## 🎮 Commands

All commands require `bdslm.use` permission (OP by default).

| Command | Description |
|---------|-------------|
| `/bdslm render [dimension]` | Manually trigger map render |
| `/bdslm status` | Show plugin status |
| `/bdslm reload` | Reload config and markers |
| `/bdslm marker add <name> <x> <z>` | Add a marker |
| `/bdslm marker remove <name>` | Remove a marker |
| `/bdslm marker list` | List all markers |

## 🌐 Public Access

To make the map accessible from the internet:

1. Set `bindAddress` to `"0.0.0.0"` in config
2. Port-forward `5110` (or your configured port) in your router/firewall
3. Access at `http://your-public-ip:5110/`

## 🙏 Credits

- [unmined-cli](https://unmined.net/) — Map rendering engine
- [Endstone](https://github.com/EndstoneMC/endstone) — BDS plugin framework
- [cpp-httplib](https://github.com/yhirose/cpp-httplib) — HTTP server library
- [nlohmann/json](https://github.com/nlohmann/json) — JSON library

## 📄 License

MIT License — see [LICENSE](LICENSE) for details.
