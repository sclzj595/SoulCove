# SoulCove 插件开发指南（面向第三方开发者）

> 目标：**任何开发者**基于公开接口独立开发插件，构建出的动态库放入
> `plugins/` 目录（或经扩展市场安装）即可接入 SoulCove——无需宿主源码、
> 无需修改宿主。

## 1. 获取 SDK

只需要两个头文件（`sdk/include/soulcove/`）：

| 头文件 | 内容 |
|---|---|
| `IPlugin.h` | 插件生命周期接口（name/version/description/initialize/shutdown） |
| `IPluginAPI.h` | 宿主能力接口（日志/命令注册/文档访问/事件订阅/数据目录/版本查询） |

这两份头文件由宿主构建的 `plugin_sdk_sync` 目标自动从
`src/interfaces/plugin/` 同步——**接口的唯一事实源在宿主仓库**，
第三方拿到的 SDK 与宿主编译所用接口永远一致。

从 `sdk/template/` 复制独立构建模板即可开始（步骤见 `sdk/README.md`）。

## 2. ABI 与环境约束（重要）

Qt 插件是 C++ 级动态库，加载成功要求插件与宿主的构建环境 ABI 兼容：

| 维度 | 要求 |
|---|---|
| Qt 版本 | **大版本必须一致**（宿主为 Qt6 → 插件也用 Qt6；建议小版本也对齐） |
| 编译器 | Windows 上 **MinGW 对 MinGW、MSVC 对 MSVC**（MSVC 各版本间 C++ ABI 兼容，MinGW 需同款工具链族） |
| 构建标准 | C++17（与宿主一致） |
| 链接 | 只依赖 Qt6::Core 等公开库，**不得链接宿主内部符号**（接口之外的能力一律走 IPluginAPI） |

不满足时 `QPluginLoader` 会加载失败，PluginManager 会在日志中记录具体错误
（设置 → 插件 页可见）。

## 3. 生命周期

```
loaded ──→ initialize(api) ──→ [运行期：命令/事件回调] ──→ shutdown() ──→ unloaded
```

- `initialize` 在宿主功能就绪后调用一次；返回 `false` 会被拒绝启用并记录错误
- `shutdown` 在应用退出前调用；**之后不得再持有宿主资源**（把 `m_api` 置空）
- 订阅的事件在插件卸载时由宿主自动退订

## 4. IPluginAPI 能力参考

```cpp
api->log(msg);                        // 统一日志（前缀 Plugin/<name>）
api->registerCommand(id, desc, fn);   // 注册命令 → 命令面板 Ctrl+Shift+P 可搜可触发
                                      //   约定 id = "plugin.<插件名>.<动作>"
api->pluginDataDir(name());           // 插件私有可写目录（自动创建）
api->applicationVersion();            // 宿主版本
api->currentDocument();               // 当前文档只读快照 {path, text, valid}
                                      //   text 为全文档拷贝，勿高频轮询
api->subscribeEvent(event, fn);       // 事件：fileOpened/fileSaved/fileClosed/encodingChanged
api->unsubscribeEvent(subId);
```

线程约定：所有回调都在**主线程**执行；插件内部可自行开线程，但回调宿主时
必须回到主线程（建议回调内只做入队）。

## 5. 命名与打包约定

- 插件标识 `name()`：反向域名（`com.你的域名.插件名`），全局唯一
- 命令 ID：`plugin.<插件名>.<动作>`
- 元数据 json 与源码同目录，`Q_PLUGIN_METADATA(... FILE "xxx.json")` 引用
- 产物文件名：宿主 MinGW 构建产物带 `lib` 前缀（如 `libwordcount_plugin.dll`），
  模板已用 `set_target_properties(... PREFIX "")` 去除，发布名自定但需全小写无空格

## 6. 发布到扩展市场

构建产物 + 注册表条目即可上架，完整 5 步流程见
[marketplace-publishing.md](marketplace-publishing.md)。
要点：DLL 传到 SoulCove-market 仓库 `plugins/`，`marketplace.json` 增加条目
（id/version/downloadUrl/fileName 必填），推送即全网生效。

## 7. 版本兼容策略

- 接口 IID `com.soulcove.plugin/1.1`：**虚函数表一旦发布不可变更**；
  新能力只能"新增接口或递增 IID 小版本"，宿主拒绝主版本不匹配的插件
- 事件名集合可能扩充（向后兼容）；插件对未知事件名天然无感
- 建议插件在 `initialize` 里检查 `applicationVersion()` 做特性降级
