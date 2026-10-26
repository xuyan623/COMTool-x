---
name: comtool-x
description: 通过 COMTool-x 本地 API 编辑功能图和操作界面、配置插件、运行调试并获取内部截图。用于 COMTool-x 工程设计与控制，不用于操纵原版 COMTool。
---

# COMTool-x

使用本地 API 完成设计与运行控制。无需操作桌面或通过鼠标定位控件。

## 连接

定位用户的 COMTool-x 程序目录；读取其中 `.sessions/api.json`，或用户指定的 `--data-dir`。文件包含本次实例的地址、token 和进程信息。不要把 token 写入工程、日志或回答。

默认文件指向最近启动且仍可连接的窗口，每个窗口还在 `.sessions/instances/api-<pid>-<port>.json` 中保存独立记录。若默认文件暂时缺失或失效，检查这些记录，向 `/v1/schema` 发送认证请求确认实例仍可连接，再读取 `app.state` 确认工程名和路径。多窗口时不要假定默认记录就是用户要操作的窗口；也可使用目标窗口帮助菜单给出的独立 `discovery` 路径。CLI 会自动从失效的默认记录回退到存活实例，没有窗口时返回可读 JSON 错误。

可用 `COMTool-x-cli.exe api <op> --json-file <path> --data-dir <dir>` 调用；结构简单时用 `--json`。长动作增加 `--wait`。源码环境可用 `python -m comtoolx api`。也可用 PowerShell HTTP 调用，不依赖额外 Python。

先读取 `app.state`、`registry`、`project.get` 和 `/v1/schema`，使用实际插件及端口 ID。详细参数见 [API 文档](references/API.md)。

## 设计

功能图是功能定义来源。先创建和连接节点，再显式 `ui.update`。新增视图在待放置容器；按 `ui.item.move/patch` 调整排版和样式。沿用已有 UI 对象 ID，避免重新生成全部布局。

修改功能图前停止运行；只读工程应按用户本次设计意图显式进入编辑模式。支持 `batch` 将相关设计操作作为一个可撤销事务提交。每个端口首版只允许一条导线，类型转换、分发和聚合需要实际处理插件。

用 `project.validate` 检查。通过 `capture` 获取 `graph`、`layout`、`runtime` 或窗口的内部截图，读取返回的本地图片检查排版，再按需调整。不要用截图替代工程和运行状态的读取。

完成后保存用户指定的 `.comtoolx` 工程包，并报告保存路径。未指定路径时 `project.save` 默认保存到程序的 `project/` 目录。工程包携带所用插件实现、配置和资源；打开包后无需单独导入插件。关闭或切换工程会卸载旧工程插件。旧 JSON 工程不兼容。打开工程不自动运行、连接或发送。

## 运行

只在用户已授权的范围内启动功能图、连接设备、发送或调参。设计请求本身不意味着可以向硬件发送。

优先使用 `node.action`；通过 `job.get` 等待 complete/failed。发送成功只表示底层写入，固件执行需另有反馈。查询状态和曲线数据以节点快照与动作结果为准。

需要操作某个界面控件时使用 `controls.list/set/click`；图表范围、图片导出等先读取 `view.actions`，再调用 `view.call`。参考 [插件开发](references/PLUGIN_SDK.md) 理解外部插件提供的契约。

用户没有要求丢弃设计时，先保存再关闭；`app.quit` 的 `discard:true` 只用于明确授权丢弃的情形。
