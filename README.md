# BDSLM - Endstone Python 卫星地图插件 🗺️

完全复刻 [bdslm-js](https://www.minebbs.com/resources/bdslm-js.8107/) 的运行模式，使用 Python 重写为 Endstone 原生插件。

作者: WMX20081122 / 千寻酱

## ✨ 特性

- 🗺️ **地图渲染** — 使用 unmined-cli 渲染 Bedrock 世界地图
- 🌐 **Web 服务器** — 内置 Python HTTP 服务器，无需 nginx
- 📍 **标记点** — 支持自定义标记，格式与 bdslm-js 一致
- 🔄 **自动渲染** — 可配置定时自动渲染周期
- 👥 **玩家追踪** — 实时追踪玩家位置，提供 API 接口
- 🎯 **命令管理** — `/satmap` 命令系统管理渲染/标记/配置
- 📦 **自动安装** — 首次运行自动下载安装 unmined-cli

## 与 bdslm-js 功能对照

| 功能 | bdslm-js (LLJS) | 本插件 (Endstone Python) |
|------|----------|--------|
| 地图渲染 | unmined-cli.exe | unmined-cli (Linux) ✅ |
| Web 服务器 | nginx.exe | Python http.server ✅ |
| 前端网页 | unmined-web (自动生成) | unmined-web (自动生成) ✅ |
| 标记点 | markers.json | markers.json (格式一致) ✅ |
| 配置文件 | config.json | config.json (格式一致) ✅ |
| 自动渲染 | setInterval(RendMap, cycle) | scheduler.run_task ✅ |
| 修改标题 | changeWebTitle() | _apply_title() ✅ |
| 添加标记 | addMarkers() → custom.markers.js | _apply_markers() ✅ |
| 玩家追踪 | ❌ | ✅ players.json + /api/players |
| 自动安装 unmined | ❌ | ✅ 首次运行自动下载 |

## 📦 安装

### 方式一：直接安装 .whl（推荐）

1. 从 [Releases](../../releases) 下载最新的 `.whl` 文件
2. 将 `.whl` 文件放入 Endstone 服务器的 `plugins/` 目录
3. 重启服务器，插件会自动下载 unmined-cli

### 方式二：从源码构建

```bash
git clone https://github.com/WMX20081122/bdslm-cpp.git
cd bdslm-cpp
pip install build
python3 -m build --wheel
# 生成的 .whl 在 dist/ 目录
```

## ⚙️ 配置文件 (config.json)

配置文件位于 `plugins/bdslm/config.json`，格式与 bdslm-js 完全一致：

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
        "unminedCli": "",
        "worldPath": "",
        "outputDir": ""
    }
}
```

### 配置说明

| 字段 | 说明 | 默认值 |
|------|------|--------|
| `webserver.port` | Web 服务器端口 | 5110 |
| `webserver.mapTitle` | 地图标题（"default" 使用世界名） | "default" |
| `webserver.bindAddress` | 绑定地址 | "0.0.0.0" |
| `mapRender.maxZoomLevel` | 最大缩放级别 | -2 |
| `mapRender.minZoomLevel` | 最小缩放级别 | 4 |
| `mapRender.imageFormat` | 图片格式 (webp/png/jpeg) | "webp" |
| `autoRend.enable` | 是否自动渲染 | true |
| `autoRend.cycle` | 自动渲染周期（分钟） | 30 |
| `paths.unminedCli` | unmined-cli 路径（空=自动检测/安装） | "" |
| `paths.worldPath` | 世界路径（空=自动检测） | "" |
| `paths.outputDir` | 输出目录（空=自动检测） | "" |

## 🎮 命令

| 命令 | 说明 | 权限 |
|------|------|------|
| `/satmap render [dimension]` | 手动触发渲染 | bdslm.command |
| `/satmap status` | 查看插件状态 | bdslm.command |
| `/satmap reload` | 重载配置和标记 | bdslm.command |
| `/satmap marker add <name> <x> <z>` | 添加标记点 | bdslm.command |
| `/satmap marker remove <name>` | 移除标记点 | bdslm.command |
| `/satmap marker list` | 列出所有标记 | bdslm.command |

别名: `/blm`

## 🌐 API 接口

Web 服务器提供以下 API：

| 端点 | 说明 |
|------|------|
| `/api/players` | 在线玩家位置（JSON） |
| `/api/markers` | 标记点数据（JSON） |
| `/api/config` | 当前配置（JSON） |

## 🔧 依赖

- **Endstone** >= 0.11.4 (Python API)
- **unmined-cli** Linux x64 (v0.19.60-dev+)
  - 下载: https://unmined.net/download/
  - 支持 Bedrock Edition 1.4 ~ 26.1+
  - 插件首次运行会自动下载安装

## 📁 文件结构

```
plugins/
├── endstone_bdslm-1.0.0-py2.py3-none-any.whl   # 插件包
└── bdslm/                                        # 数据目录（自动创建）
    ├── config.json                               # 配置
    ├── markers.json                              # 标记
    ├── players.json                              # 玩家位置（自动更新）
    ├── unmined-cli/                              # unmined-cli（自动安装）
    │   └── unmined-cli
    └── unmined-web/                              # 渲染输出（自动生成）
        ├── unmined.index.html
        ├── custom.markers.js
        └── tiles/
```

## 🔄 从 C++ 版迁移

本插件从 C++ 版 (bdslm-cpp) 重写为 Python 版，解决了以下 C++ 版问题：

- ❌ C++ asio Web 服务器绑定 0.0.0.0 导致端口被封
- ❌ C++ MapRenderer 的 runTaskAsync 捕获 Dimension* 指针导致 SIGSEGV
- ❌ 依赖下载在 chroot 环境中失败

Python 版优势：
- ✅ 纯 Python，无需编译
- ✅ 内置 HTTP 服务器，无需 nginx
- ✅ 自动安装 unmined-cli
- ✅ 配置/标记格式与 bdslm-js 完全兼容

## 📝 版本历史

### v1.0.0
- 初始 Python 版本，完全复刻 bdslm-js 功能
- unmined-cli 渲染 + Python HTTP 服务器
- 玩家位置追踪
- 标记点管理
- unmined-cli 自动安装
- ASCII Art 启动提示
- `/satmap` 命令系统

## License

MIT
