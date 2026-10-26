# 本地 API

## 连接与发现

启动软件后读取 `<data-dir>/api.json`。默认 data-dir 是程序目录下 `.sessions`；可用 `--data-dir` 指定独立实例。

默认文件指向最近启动且仍可连接的窗口。每个窗口另在 `<data-dir>/instances/api-<pid>-<port>.json` 保留独立记录；“帮助 → 本地 API 与 Skill”中的 `discovery` 是该窗口的独立路径，`default_discovery` 是默认路径。关闭任一窗口不会移除其它窗口的记录，默认窗口退出后自动切换到存活窗口。

文件包含 `url`、`token`、`pid`、`version`、`started_at`。连接信息以原子方式写入；窗口每两秒检查并恢复丢失或损坏的记录。CLI 在默认记录失效时也会查找仍可认证的实例，忽略异常退出留下的记录。token 每次启动变化，不写入工程，不应粘贴到共享文档。所有设计和数据接口均需要 `Authorization: Bearer <token>`，`/v1/health` 除外。

可以直接使用无需 Python 的 `COMTool-x-cli.exe api <op> --json '<JSON>'`；复杂请求使用 `--json-file <path>`。使用自定义数据目录时同时传 `--data-dir`。

多个窗口共用同一个数据目录时，先用 `app.state` 的工程名和路径确认目标。需要控制特定窗口时，读取其独立记录并使用其中的地址和认证信息发送 HTTP 请求。没有可连接窗口时，CLI 返回 `ok=false` 的 JSON 和退出码 1。

HTTP 命令统一为：

~~~http
POST /v1/command
Authorization: Bearer <token>
Content-Type: application/json

{"op":"graph.node.add","args":{"id":"serial","plugin":"io.serial","config":{"port":"COM3","baudrate":115200}}}
~~~

返回 `{"ok":true,"result":...}`，失败返回 `{"ok":false,"error":"原因"}` 与 4xx/5xx 状态码。请求体最大 16 MiB。bytes 使用 `{"type":"bytes","base64":"...","length":123}` 表示。

只读入口：`GET /v1/state`、`/v1/project`、`/v1/plugins`、`/v1/schema`、`/v1/jobs/<id>`。以运行中的 `/v1/schema` 和 `registry` 为命令与插件契约的准确来源。

## 设计命令

| 命令 | 参数 |
|---|---|
| `app.state` / `registry` / `project.get` | 无 |
| `project.new` | `name` |
| `project.open` | `path`（`.comtoolx` 工程包）, `read_only=false` |
| `project.save` | `path?`，初次默认保存到 `project/<工程名>.comtoolx`；随后可省略 |
| `project.close` | 无，停止运行、关闭工程并卸载工程插件 |
| `project.patch` | `name?`, `metadata?` |
| `project.validate` | 无，返回 `valid` 和 `issues` |
| `mode.set` | `mode=edit` 或 `runtime` |
| `graph.node.add` | `plugin`, `id?`, `name?`, `position=[x,y]?`, `rotation?`, `config?` |
| `graph.node.patch` | `id`, `name?`, `position?`, `rotation?`, `config?` |
| `graph.node.move` | `id`, `position=[x,y]` |
| `graph.node.remove` | `id` |
| `graph.wire.add` | `plugin?`, `id?`, `source={node,port}`, `target={node,port}`, `properties?` |
| `graph.wire.patch` / `graph.wire.remove` | `id`，修改时附 `properties` |
| `ui.update` | 无，显式生成新增视图并报告失效绑定 |
| `ui.item.add` | `id?`, `plugin` 或 `node/view`, `parent?`, `title?`, `stretch?`, `properties?` |
| `ui.item.patch` | `id`, `title?`, `node?`, `view?`, `stretch?`, `properties?` |
| `ui.item.move` | `id`, `parent`, `index?` |
| `ui.item.remove` | `id`，删除容器同时删除后代视图 |
| `history.undo` / `history.redo` | 无 |
| `batch` | `commands=[{op,args},...]`，最多 1000 条设计命令，整体提交或回滚 |
| `plugin.load` | `path`，向当前工程加载可信本地 Python 插件 |
| `theme.set` | `theme=dark` 或 `light` |
| `workspace.set` | `view=graph`、`layout` 或 `runtime` |
| `graph.fit` | 无 |
| `graph.overlaps` | 无，返回节点 ID 对和实际占位交叠区域 |
| `library.preview` | `plugin`，显示模块预览，返回可截图的 `target=library.preview` |

UI 布局容器由 `registry.containers` 发现。内置 `layout.row`、`layout.column`、`layout.grid`、`layout.tabs`。`parent` 必须指向容器，`stretch` 控制拉伸比例，移动同级元素时 `index` 指定顺序。

`project.save` 保存插件实现、配置、布局和资源为单个工程包。`project.open` 加载包内实现，不需要先调用 `plugin.load`，也不自动运行或连接。`project.close/new/open` 替换当前工程时卸载旧工程插件。`app.state.project_dir` 返回默认工程目录；旧 JSON 工程不兼容。

通用 UI 属性见 `registry.ui_properties`：最小/最大宽高、网格单元 `row/column/rowspan/colspan`、`font_size/font_family/color/background`、`label/text`。容器还声明间距、边距、列数；外部插件可以通过 `view_properties` 声明视图专用属性。属性更新为合并，不替换其他属性。

只读模式拒绝设计更改。功能图运行期间拒绝节点和连线变更。UI 编辑只调整显示，不创建新功能实例。移除视图会记录为隐藏，后续“更新 UI”不会重复创建它。

## 运行与异步操作

节点的 `rotation` 使用 0、90、180、270 度，缺省为 0。它属于设计数据，保存、重开、撤销和重做均保留方向；改变方向时导线同步更新。

功能图模块和端口按 `rotation` 旋转，所有文字保持水平正向。竖向模块调整宽度与标签排布，并保持旋转中心及保存坐标稳定。占位重叠属于显示提示，可通过 `graph.overlaps` 查询。

基础导线的 `properties.points` 是按路径顺序排列的画布坐标，例如 `[[300,200],[500,200]]`。用 `graph.wire.patch` 修改它即可直接调整走线，空列表恢复自动路径。`style=curve` 为穿过路径点的平滑曲线，`style=straight` 为经过各点的折线。所有点必须是有限的二维数值。

`runtime.start` 校验后运行功能图；`runtime.stop` 停止并释放设备。

`node.action` 接受 `id`、`action`、`args`，返回 `job_id`。轮询 `job.get`，状态为 `running`、`complete` 或 `failed`；完成结果在 `result`，失败原因在 `error`。CLI 添加 `--wait` 可等待完成并返回适当退出码。

常见操作：

~~~json
{"op":"node.action","args":{"id":"serial","action":"connect","args":{"port":"COM3","baudrate":115200}}}
{"op":"node.action","args":{"id":"console","action":"send","args":{"text":"hello","newline":"CRLF"}}}
{"op":"node.action","args":{"id":"plot","action":"set_visible","args":{"channel":"signal","visible":true}}}
{"op":"node.action","args":{"id":"plot","action":"data","args":{"channel":"signal","limit":100}}}
{"op":"node.action","args":{"id":"input","action":"set_value","args":{"value":10}}}
~~~

节点的准确 actions 及 `requires_running` 约束由 `registry` 提供。通信发送的成功结果表示底层写入完成，不表示对端解析、执行或应答完成。失败发送不会覆盖此前实际发送记录。

## 控件与内部截图

- `controls.list {root?}`：读取 Qt 对象 ID、类型、值、启用状态与可用操作。
- `controls.set {id,value,submit?}`：编辑标准输入控件。
- `controls.click {id}`：触发按钮或菜单动作。
- `view.actions {}`：发现当前插件视图的专用操作。
- `view.call {id,action,args}`：调用专用操作，例如图表范围、导出图片。
- `capture {target,width?,height?,full?}`：内部渲染截图。target 可以是 `window`、`graph`、`layout`、`runtime` 或对象 ID。

优先使用设计命令和 `node.action`；标准控件接口用于确实需要操纵某个控件的操作。设计和运行视图 ID 分别使用 `layout/<item-id>` 与 `runtime/<item-id>` 前缀。

截图返回绝对 `path`、尺寸和鉴权下载路径 `url`，可用 `GET /v1/screenshots/<id>.png` 读取。截图前同步一次已声明的视图刷新，不需要鼠标、屏幕坐标或桌面截图。

图表专用操作示例：

~~~json
{"op":"view.call","args":{"id":"runtime/plot_view","action":"set_range","args":{"x":[0,10],"y":[-1,1]}}}
{"op":"view.call","args":{"id":"runtime/plot_view","action":"export_image","args":{"path":"D:/plot.svg"}}}
~~~

`app.quit {discard:false}` 有未保存设计时拒绝关闭；先保存，或者用户明确希望丢弃时使用 `discard:true`。

## PowerShell 直接调用

~~~powershell
$info = Get-Content -LiteralPath '.\.sessions\api.json' -Raw | ConvertFrom-Json
$headers = @{ Authorization = "Bearer $($info.token)" }
$body = @{ op = 'capture'; args = @{ target = 'graph'; width = 1800 } } | ConvertTo-Json -Depth 20
Invoke-RestMethod -Uri "$($info.url)/v1/command" -Method Post -Headers $headers -ContentType 'application/json; charset=utf-8' -Body ([Text.Encoding]::UTF8.GetBytes($body))
~~~
