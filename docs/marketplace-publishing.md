# 扩展市场发布指南（M9）

本文档说明如何把一个插件从源码发布到 SoulCove 扩展市场，并在应用内完成安装。
配套案例插件：`src/plugins/wordcount/`（WordCount 字数统计，覆盖 IPluginAPI 全部能力）。

## 一、市场结构

```
SoulCove-market 仓库（github.com/sclzj595/SoulCove-market）
├── marketplace.json          ← 注册表（市场面板启动时拉取）
└── plugins/
    ├── wordcount_plugin.dll  ← 插件二进制（release 构建）
    └── hello_plugin.dll
```

`marketplace.json` 格式（示例见 [marketplace.example.json](marketplace.example.json)）：

```json
{
  "items": [
    {
      "id": "com.soulcove.wordcount",
      "name": "WordCount 字数统计",
      "version": "1.0.0",
      "type": "plugin",
      "author": "scStudio",
      "description": "……",
      "downloadUrl": "https://github.com/sclzj595/SoulCove-market/raw/main/plugins/wordcount_plugin.dll",
      "fileName": "wordcount_plugin.dll"
    }
  ]
}
```

## 二、发布一个插件的完整流程

1. **实现插件**：参照 `src/plugins/wordcount/`（HelloPlugin 是最小版）：
   - 类继承 `QObject + IPlugin`，`Q_PLUGIN_METADATA(IID SoulCovePluginIID FILE "xxx.json")` + `Q_INTERFACES(IPlugin)`
   - `initialize(IPluginAPI*)` 中注册命令 / 订阅事件；`shutdown()` 置空 API 引用
   - 同目录放元数据 json（name/version/description）
2. **构建**：随主工程构建（CMake 已按 MODULE 输出到 `build/plugins/`），或单独构建该 target：
   `cmake --build build --target wordcount_plugin`
3. **上传**：把 release 构建的 `wordcount_plugin.dll` 传到 SoulCove-market 仓库 `plugins/` 目录
4. **登记**：在 `marketplace.json` 的 `items` 中加一条记录（id/name/version/downloadUrl/fileName 必填），提交推送
5. **验证**：应用内 `Ctrl+Shift+P` → 「扩展市场：打开」→ 刷新 → 选中 → 安装 → 重启 → 命令面板搜索 `plugin.wordcount.stats` 触发

## 三、发布一个主题（M9 stage4）

主题即 JSON 色板文件（`ThemeManager::paletteFromJson` 解析），安装后立即注册、
设置 → 外观（或命令面板）切换即生效，无需重启。

```json
{
  "id": "sunset",                    // 可选，缺省用文件名（去 .json）
  "name": "日落橙",                   // 显示名，缺省用 id
  "version": "1.0.0",                // 可选，市场更新检查用
  "base": "dark",                    // 缺失字段的回退基板：dark（默认）| light
  "colors": {
    "accentPrimary": "#E67E22",
    "bgEditor": "#1e1e1e",
    "fgPrimary": "#d4d4d4"
    // …字段名与 ThemePalette 成员一一对应，只写要覆盖的即可
  },
  "syntax": {
    "keyword": "#569cd6"
    // …字段名与 ThemePalette::SyntaxColors 成员一一对应
  }
}
```

- 颜色值支持 `#RGB/#RRGGBB/#AARRGGBB` 与 Qt 命名色；任一提供的值非法则整个文件拒绝安装
- 安装落盘到 `applicationDirPath()/themes/<fileName>`，应用启动时自动扫描该目录注册
- 覆盖安装/卸载由扩展市场面板完成；卸载"正在使用"的主题会先切回默认主题
- 发布流程与插件一致：把主题 json 上传到 SoulCove-market `themes/` 目录，
  注册表登记一条 `type: "theme"` 记录即可

## 四、注意

- `fileName` 只取文件名部分（市场面板已做路径穿越防护）
- 插件已加载时覆盖安装/卸载会被 Windows 文件锁拦截，面板会提示重启后重试
- ABI：接口 IID 为 `com.soulcove.plugin/1.1`，主版本不匹配的插件将被拒绝加载；
  接口只能新增（向后兼容），破坏性变更必须升 IID 主版本
- 在线安装支持 `type: "plugin" / "snippet" / "theme"`（M9 stage4 起 theme 补齐）
