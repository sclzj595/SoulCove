# SoulCove 插件 SDK

任何开发者都可以为 SoulCove 编写插件：**只需要本目录的两个公开头文件 +
Qt6（Core），不需要宿主源码**。构建出的动态库放入宿主 `plugins/` 目录（或经
[扩展市场](../docs/marketplace-publishing.md) 分发），重启即被加载。

```
sdk/
├── include/soulcove/     ← 公开接口头文件（IPlugin.h / IPluginAPI.h）
│                            由宿主构建的 plugin_sdk_sync 目标自动与源码保持同步
├── template/             ← 独立构建模板：复制改名即可开发你的第一个插件
│   ├── CMakeLists.txt
│   ├── MyPlugin.h / MyPlugin.cpp
│   └── myplugin.json
└── README.md
```

## 三步上手

```bash
# 1. 复制模板
cp -r sdk/template my-first-plugin && cd my-first-plugin

# 2. 构建（Qt6 Core + 任意 C++17 工具链；ABI 约束见下）
cmake -B build -DCMAKE_PREFIX_PATH="<你的Qt安装目录>"
cmake --build build

# 3. 部署
#    把产物 myplugin.dll 复制到宿主可执行文件旁的 plugins/ 目录，重启 SoulCove
#    命令面板 Ctrl+Shift+P 搜索 "myplugin" 即可触发
```

## 关键约束（务必阅读 docs/plugin-sdk.md）

- **ABI 匹配**：Windows 上插件必须与宿主使用**同款编译器工具链**（MinGW 对 MinGW、
  MSVC 对 MSVC）且 **Qt6 大版本一致**，否则无法加载
- **IID 版本**：接口 IID `com.soulcove.plugin/1.1`，宿主拒绝主版本不匹配的插件
- **唯一标识**：`name()` 用反向域名（`com.你的域名.插件名`），避免冲突
- 完整接口说明、事件列表、发布到扩展市场的流程见 [docs/plugin-sdk.md](../docs/plugin-sdk.md)
