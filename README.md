# BDSLM - Endstone Python 卫星地图插件 🗺️

精简版 — 只做卫星图渲染 + Web 服务器

作者: WMX20081122 / 千寻酱

## ✨ 功能

- 🗺️ **地图渲染** — 调用 unmined-cli 渲染 Bedrock 世界地图
- 🌐 **Web 服务器** — 内置 Python HTTP 服务器，无需 nginx
- 🔄 **自动渲染** — 可配置定时自动渲染周期
- 🔧 **自动配置** — 首次运行自动生成 config.json

## 📦 安装

1. 从 [Releases](../../releases) 下载最新的 `.whl` 文件
2. 将 `.whl` 文件放入 Endstone 服务器的 `plugins/` 目录
3. 将 `unmined-cli` 放入 `plugins/bdslm/unmined-cli/` 目录
4. 重启服务器

## 🎮 命令

| 命令 | 说明 | 权限 |
|------|------|------|
| `/slmrender` | 手动触发渲染 | OP |

## ⚙️ 配置文件 (config.json)

首次运行自动生成于 `plugins/bdslm/config.json`：

```json
{
  "webserver": {
    "port": 5110,
    "bindAddress": "0.0.0.0"
  },
  "mapRender": {
    "zoomin": 1,
    "zoomout": -5
  },
  "autoRend": {
    "isEnabled": true,
    "cycle": 120
  }
}
```

### 配置说明

| 字段 | 说明 | 默认值 |
|------|------|--------|
| `webserver.port` | Web 服务器端口 | 5110 |
| `webserver.bindAddress` | 绑定地址 | 0.0.0.0 |
| `mapRender.zoomin` | 最大缩放级别 | 1 |
| `mapRender.zoomout` | 最小缩放级别 | -5 |
| `autoRend.isEnabled` | 是否自动渲染 | true |
| `autoRend.cycle` | 自动渲染间隔（分钟） | 120 |

## 🌐 Web API

| 路径 | 说明 |
|------|------|
| `/` | 地图页面 (index.html) |
| `/api/status` | 渲染状态 JSON |

## 📋 依赖

- [Endstone](https://github.com/EndstoneMC/endstone) v0.11+
- [unmined-cli](https://unmined.net/downloads/) — 地图渲染工具

## 📄 许可证

MIT License © 2026 WMX20081122
