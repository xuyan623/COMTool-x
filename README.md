# COMTool-x

原生 Qt 通信与仪表工作台。功能图定义行为，界面设计器决定操作界面的排版；本地 API 与人工编辑使用同一套工程模型。

源码仓库：[xuyan623/COMTool-x](https://github.com/xuyan623/COMTool-x)。

## 直接使用

解压整个便携包，双击 `COMTool-x.exe`。保留同目录的 `_internal` 文件夹。

1. 在“工程 → 打开工程”选择 `examples/SerialPlot.comtoolx`。
2. 切换到“运行界面”，点击顶部“运行”。
3. 在连接面板选择实际串口和波特率，再点击“连接”。
4. 使用实时曲线。点击“停止”释放连接。

打开工程只恢复设计，不自动运行、连接或发送。

命令行打开指定工程：

~~~powershell
.\COMTool-x.exe --project .\examples\SerialPlot.comtoolx
.\COMTool-x.exe --read-only .\examples\SerialPlot.comtoolx
~~~

“只读运行”锁定设计，仍允许连接、发送和调参。需要改设计时显式选择“进入编辑”。

## 设计自己的工具

1. 在“功能图”从插件库拖入节点，拖拽输出端口到输入端口连线。
   鼠标停留在插件条目上会显示模块与端口预览；移出条目后关闭。预览只读取插件声明。
2. 在右侧属性面板修改参数。每个端口首版只连接一条导线；类型不匹配会被拒绝。
   选中模块后按空格，每次顺时针旋转 90°；支持多选一起旋转、撤销和保存方向。运行或只读期间禁止旋转。
   模块和端口按方向旋转，文字在四个方向都保持水平。竖向模块中标题居中、端口标签分列上下，宽度按端口数量调整；重叠模块以黄色边框提示，可悬停查看。
   选中导线后，拖动空心圆点新增路径点，拖动实心圆点调整路径；双击导线也可加点。右键实心圆点删除，右键导线切换平滑曲线/折线或重置自动路径。路径可保存、撤销和重做。
3. 点击“更新 UI”。新节点的视图进入待放置区，已有排版保持。
4. 在“界面设计”拖动结构树，调整横向、纵向、网格、标签页容器，以及拉伸权重和尺寸。
5. 保存为 `.comtoolx` 工程包，默认放在程序目录的 `project/` 文件夹。

工程包包含所用插件的实现快照、节点配置、连线、界面布局和必要资源。把这一个文件分享给使用 COMTool-x 的人，直接打开即可恢复工程，无需另行导入插件。打开时采用包内实现，插件仅属于当前工程；关闭、新建或切换工程会卸载它们。软件空白启动只显示通用插件。旧 JSON 工程不受支持。

修改节点或连线前先停止功能图。界面布局可以独立调整。移除节点后，原视图会列为失效绑定，需重新绑定或删除。

官方随包只提供“串口＋波形图”通用示例。用户自定义插件随其工程包保存，公开发布包不包含私人项目。

## 当前插件

- 串口：收发、串口枚举、流控、RTS/DTR、可选断线重连。
- 网络：TCP 客户端与多客户端服务端、UDP 客户端与服务端、SSH 密码/密钥登录。
- 收发调试：文本/HEX、编码与转义、换行、时间戳、历史、快捷指令、定时发送、文件发送与取消、保存接收。
- 协议：原 COMTool ASCII/二进制曲线帧、文本编解码、JSON、自定义 Python 编解码及 CRC 辅助函数。
- 终端：ANSI 颜色与屏幕控制、键盘输入、SSH 终端尺寸传递。
- 曲线：原生 pyqtgraph、缩放平移、右侧通道列表与显隐/颜色、暂停、跟随、时间窗、FFT 菜单、CSV/PNG/SVG 导出。
- 基础控件：按钮、数值、滑块、文本、复选框、选项、数值显示、定时器、记录器。

原版 PyQt5 插件不能直接加载；插件行为需按新 SDK 迁移。原版 `config.json` 也不直接兼容，需要按新版工程模型重建。

## AI 设计与控制

启动后，在“帮助 → 本地 API 与 Skill”查看连接信息。默认入口为 `.sessions/api.json`，每个窗口另有 `.sessions/instances/` 下的独立记录，仅监听本机。关闭一个窗口不会影响其它窗口的连接信息；记录丢失时会自动恢复。多个窗口同时运行时，先核对 API 返回的工程名和路径。

~~~powershell
.\COMTool-x-cli.exe api app.state
.\COMTool-x-cli.exe api registry
.\COMTool-x-cli.exe api capture --json '{"target":"graph","width":1800}'
~~~

GUI 程序负责窗口；`COMTool-x-cli.exe` 提供带标准输出的命令行客户端。两者共用同一份运行库，无需安装 Python。

详见 [API 文档](docs/API.md)、[插件开发](docs/PLUGIN_SDK.md)、[配套 Skill](skills/comtool-x/SKILL.md) 和 [验证范围](docs/VERIFICATION.md)。

配套 Skill 随包提供。需要在 Codex 中自动发现时，将整个 `skills/comtool-x` 文件夹复制到自己的 `.codex/skills` 目录；其 references 也需一起保留。

## 开发与构建

依赖由 `uv.lock` 锁定，构建使用 CPython 3.15、PySide6 6.12、pyqtgraph 0.14。

~~~powershell
git clone https://github.com/xuyan623/COMTool-x.git
cd COMTool-x
uv sync --locked --group build --python 3.15
.\.venv\Scripts\python.exe -m comtoolx
.\build.ps1
~~~

运行源码测试：

~~~powershell
$env:QT_QPA_PLATFORM = 'offscreen'
.\.venv\Scripts\python.exe -m unittest discover -s tests -v
~~~

对外发布包为 `dist/COMTool-x-Windows-x64.zip`。程序暂存目录和源码归档位于 `build/release/`；`dist` 只存放最新通用用户版发布包。

本项目按 LGPL-3.0 提供，保留原 COMTool 来源及许可证，详见 [第三方说明](THIRD_PARTY_NOTICES.md)。
