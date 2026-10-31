# CoRig 本地 API v3

GUI 与 API 共用原生命令入口。运行实例的 `schema` 和 `registry` 是准确契约；命令完整字段也随包保存在 [COMMAND_SCHEMA.json](COMMAND_SCHEMA.json)。旧 v2 端点和旧插件接口不提供兼容。

## 连接

便携版默认数据目录为可执行文件目录；安装版使用 Windows 本地应用数据目录。GUI 的 `--data-dir` 可指定独立目录。读取 `<data-dir>/.sessions/api.json`，其中包含 application=CoRig、url、token、pid、discovery 和 api_version。连接记录与 schema 都声明产品名称，CLI 只接受当前 CoRig 实例。每个进程还在 `.sessions/instances/` 保存独立记录；一个工程拖出的子窗口共用该进程接口。

全部请求要求 `Authorization: Bearer <token>`，包括 `GET /v3/schema`。token 每次启动变化，仅供本机调用，不应写进工程、报告或共享文档。接口只监听 127.0.0.1。

```http
POST /v3/command
Authorization: Bearer <token>
Content-Type: application/json

{"op":"graph.node.add","args":{"id":"number","plugin":"control.number","config":{"value":2}}}
```

返回 `{ok:true,result:...}`；失败为 `{ok:false,error:'原因'}`。请求体最大 8 MiB。设计操作、文件任务、引擎/节点动作可能返回 `{job:'ID'}`；控件操作还可能返回 `jobs:[ID,...]`，表示回调启动的节点动作。用 `job.get` 等待全部任务 state=complete 并读取 result，或读取 state=failed 的 error。拿到 job ID 或控件 updated=true 不代表后台操作已经执行。原生 CLI 的 `--wait` 会等待单个 job 或全部 jobs；多任务返回 jobs_complete 和 job_results。

## 原生命令行

```powershell
.\CoRig-cli.exe api app.state --data-dir D:\CoRigData
.\CoRig-cli.exe api schema
.\CoRig-cli.exe api runtime.start --json '{"debug":false}' --wait
.\CoRig-cli.exe api graph.node.add --json-file D:\request.json --wait
```

CLI 不依赖 Python；默认记录失效或缺失时会探测仍可认证的进程记录。`--discovery` 可指定某一记录；`--timeout` 为毫秒。无窗口或命令失败时返回 ok=false 和退出码 1。多实例先检查 app.state 的 project/path，避免操作错工程。

## 数据、运行与界面

bytes 使用 `{type:'bytes',base64:'...',length:N}`；采样帧为 `{time:秒,values:{通道名:数值}}`。端口、属性、动作由 registry 声明，先读取真实 ID 和默认配置。

功能设计是实际执行来源。`editor.add` 接收 page=graph 或 layout，新增对象先待放置；`editor.place` 单击落位，`editor.withdraw` 撤下并保留配置。未放置的功能节点不参与计算。`graph.node.add` 是显式建图接口，默认直接放置，也可指定 placed=false。结构修改前暂停；未保存修改不能直接丢弃。project.save 未指定路径时默认写入 `<data-dir>/project/<工程名>.crpj`。

双向同步先调用 `sync.plan`，direction 为 graph_to_ui 或 ui_to_graph。返回 changes、revision；显示变更清单后，将用户选中的 key 和同一个 revision 交给 `sync.apply`。目标页多余对象会列入删除项，取消勾选即可保留；新增对象进入待放置区，已有摆放、层序和 ID 不变。旧 `ui.update` 仅补充缺失视图，新增仍待放置；完整双向同步使用清单接口。

`editor.plan` 描述 remove/replace/withdraw 的影响，page 和 ids 指定对象。`editor.remove`、`editor.replace` 要求 confirmed=true，可携带清单的 revision 防止过时确认。独立复制使用 `editor.copy` / `editor.paste`，继承配置和外观，只继承所选子图内部连线。`editor.reorder` 的 index 越小，图层越靠前；界面对象在同一父容器内排序。

界面根是 layout.canvas，root.properties 的 width、height 是设计尺寸，grid_size 是 1～128 px 的网格粒度。`ui.item.patch` 只接受外观／布局字段；x、y、width、height 会按网格对齐，容器内部排列遵守相同网格。运行界面整体等比缩放并留白。

`ui.item.schema` 查询指定界面对象可用的外观／布局参数。`ui.view.bind` 使用 id、node、view 重新绑定已有视图，保留界面对象 ID、外观、位置和容器归属。`graph.connection.options` 使用 node、port 查询同 GUI 一致的兼容目标及拒绝原因。

`editor.state` 查询 graph 或 layout 的实时 zoom、pan_x、pan_y、left_width、right_width 与视口尺寸；`editor.configure` 修改这些字段，fit=true 适应画布。zoom 范围为 0.1～3，侧栏宽度为 0～420，0 表示完全收起；仅 graph 支持 animation。它们改变显示状态，不修改工程，也可在只读和运行状态使用。侧栏实际宽度在下一次 Qt 布局后稳定，再查询 state 可确认结果。

runtime.pause 保留实验状态；runtime.stop 释放连接但保留当前参数；runtime.reset 恢复工程初值并清除临时覆盖。runtime.debug 控制详细调试；runtime.step 的 mode 为 batch 或 node，source 可选择数据源，开始的批次须沿原游标完成。

只读模式用 preferences.set 的 read_only；node.configure 的 persist=false 仅改变当前实验，工程不会变脏。运行与设备发送仍须有用户授权。发送动作成功仅表示本机层接受/写入，设备实际执行需要独立反馈。

```json
{"op":"node.action","args":{"node":"serial","action":"connect","args":{}}}
{"op":"node.action","args":{"node":"console","action":"send","args":{"text":"1,2,3"}}}
{"op":"view.read","args":{"node":"plot","view":"plot","args":{"width":1200,"follow":true}}}
{"op":"node.action","args":{"node":"plot","action":"read_data","args":{}}}
```

view.read 省略 args 时使用界面当前范围；原始曲线用 read_data，频谱用 view.read 的 fft=true。view.actions 列出公开视图动作，view.call 支持范围、FFT、相机和图片导出。姿态 geometry 动作可分页读取真实顶点/面；不以截图推断数值。

`controls.list` 可按 page 筛选，发现已加载的具名及未命名 Qt Quick 对象。记录包含 id、handle、path、name、page、node、view、ui_item、visible、enabled、bounds、writable、actions 和可读值。显式 objectName 仍可用；每个实际对象还有自动 handle，在该对象存活期间保持不变，移动或兄弟排序不会将它指向另一对象。path 描述当前视觉层级，含页、QML id／类型及同级序号。视图重建或工程切换后重新发现；不要持久保存自动 handle。相同显式名称优先选择可见可用对象，使用 handle 可精确区分多份视图。

`controls.get` 读取指定对象，包括支持控件的 pressed、down、hovered。`controls.set` 只接受其 writable 中的 text、editText、value、checked、currentIndex；滑块、SpinBox、下拉框和开关会触发实际用户操作回调。文本赋值后使用 `controls.commit` 按 Qt 的 Enter 提交规则执行校验与 accepted／editingFinished；`controls.focus`、`controls.click` 操作实际对象。公共引擎控件使用 execution.run/debug/batch/node/reset/end/source/status；execution.run 按运行状态切换运行／暂停，明确的引擎命令仍为 runtime.start 和 runtime.pause。

自绘或非标准 QML 控件可以使用 `controls.key` 和 `controls.pointer`，事件直接送入应用自身 Qt 窗口，不移动系统鼠标，也不发送操作系统键盘输入。key 使用 Qt 名称，如 Return、A；modifiers 可为 ControlModifier 或以 | 分隔的组合。text 支持 Unicode。pointer 的 type 为 click、press、move、release、double_click 或 wheel；x、y 是控件局部坐标，省略时使用中心；button／buttons 使用 LeftButton、RightButton、NoButton 等 Qt 名称，滚轮用 delta_x／delta_y。拖动通过 press、move、release 完成。对象需可见、启用且有所属窗口；业务操作优先使用已声明的 node.action。

capture 的 target 可为 window、graph、layout、runtime、logs、library 或 `view.<node>.<view>`。目标窗口需已显示且未最小化，完成的 job 返回本地 PNG 路径。graph.activity 返回实际绘图层采样频率、亮条位置、速度和间距，不修改计算调度。

workspace.open 定位已有页面。插件库始终以唯一精简子窗口打开；workspace.detach/move 保持共享会话，window.pin 控制置顶。标签栏位于顶部，公共引擎位于底部。功能设计和界面设计的对象栏、属性栏可用分隔条拖到 0 宽度隐藏，再拖回或用图标展开。插件库不能移入工作区标签。

`window.configure` 按窗口 ID 设置 x、y、width、height、state 和 focus；state 为 normal、minimized、maximized 或 fullscreen。可将最小化窗口恢复为 normal，再内部截图。app.state 的 workspaces.windows 同时返回实际窗口位置、尺寸、state、focused 和 native_owned。分离页面为无原生 owner 的普通窗口，可与主窗口交替聚焦；始终置顶只由 window.pin 控制。

窗口记录另提供 visible、native_visible、exposed 和 frame_count。隐藏启动的 Windows 窗口也可通过 window.configure 的 state=normal 恢复实际显示；截图前应确认 exposed 或绘制帧已更新。

## 录制、回放与布尔控制

控制信号采用 boolean；触发型输入只对本次新到达的 true 执行动作，false 忽略。相同值的新消息仍是一次更新，条件变化检测使用 logic.edge。control.button 保留 trigger 这个端口 ID，其数据类型为 boolean，每次点击输出 true，复杂操作由下游完成。

| 插件 | 端口和配置 | 主要动作 |
|---|---|---|
| data.record | 同类型 in/out；布尔 start/pause；data_type 选择信号类型 | start、pause、save(path)、reset |
| data.playback | 同类型 in/out；data_type；path 为默认外部文件 | load(path)、cutoff(value)、play、pause、seek(position)、step(direction)、set_speed(value) |
| source.timer | 布尔 start/stop 输入、tick 输出；mode=periodic/countdown，interval_ms | start、stop |
| data.fork | 同类型 in 与 2～8 个输出；outputs 名称列表，默认 a/b | 通过 node.configure 设置 outputs |
| logic.and/or/not/nand/nor/xor/xnor | 布尔 a/b 输入（非门仅 a）、out 输出 | 输入更新时求值 |
| logic.edge | 布尔 in/out；edge=rising/falling/both，初始状态 false | 指定变化发生时输出一次 true |

录制状态包括 recording、saved、records、duration、written_bytes、saved_path 和 last_error。暂停时不累计录制时长，信号内容原样保存。save 使用独立的 .crrc 文件并结束当前录制；之后 start 开始新的一次，reset 清空本次缓存。引擎重置直接清空尚未保存的内容。

回放状态包括 loaded、path、position、duration、records、sample_index、speed、playing、cutoff 和 last_error。导入先验证完整文件及信号类型，失败保持原文件与状态；成功后暂停，截断开关仍由用户控制。关闭截断始终实时直通；打开截断保持待播放。暂停、播放结束或读文件错误均保留截断状态，关闭截断才恢复实时。

position 为秒，direction 为 -1/1，倍速为 0.125～16。截断打开时，seek 输出目标时刻之前最近的一条记录并保持原播放／暂停状态，step 输出相邻记录并暂停；关闭截断时仅预选位置。跳转不重置、重算或恢复下游历史，原始信号内的时间字段保持不变。

右栏 node.configure 的 path 是随工程保存的默认文件引用；node.action 的 load 是本次运行的临时文件。工程不包含录制内容。重置清空播放状态和临时导入，按设计重新加载默认文件；默认文件不可用时仍可实时直通。导入、保存、暂停、调整倍速与录制重置允许在引擎未运行时操作；开始录制、截断、播放、定位与逐条操作需要已建立运行会话，暂停的会话仍可单次定位。

```json
{"op":"node.action","args":{"node":"rec","action":"start","args":{}}}
{"op":"node.action","args":{"node":"rec","action":"save","args":{"path":"D:/captures/test.crrc"}}}
{"op":"node.action","args":{"node":"play","action":"load","args":{"path":"D:/captures/test.crrc"}}}
{"op":"node.action","args":{"node":"play","action":"cutoff","args":{"value":true}}}
{"op":"node.action","args":{"node":"play","action":"seek","args":{"position":1.5}}}
{"op":"node.action","args":{"node":"play","action":"set_speed","args":{"value":2}}}
{"op":"node.action","args":{"node":"play","action":"play","args":{}}}
```

以上例子要求先在工程中放置、连接相应节点，并按动作要求建立会话。完整无硬件示例见 examples/RecordPlayback.crpj；控件 ID 为 recording.<node>.toggle/save/reset 和 playback.<node>.load/toggle/rewind/previous/next/cutoff/position/speed。先查询 registry 或 view.actions 的真实动作契约，并逐个等待返回的任务完成。

## 本地插件、工程依赖和维护

`plugin.state` 只报告本地插件文件夹的包、版本、相对路径、节点启用设置、当前工程引用及待重启状态，不合并工程副本。`plugin.tree(scope=library)` 是本地目录树，`scope=project` 是当前工程所用包及其实际选定版本。树的 folder 节点提供 children，plugin 叶节点提供包 id、version 和 relative_path。包在根目录归“其他”；外部文件或目录调整重启后重新扫描。

`plugin.settings` 打开本地树状管理窗口，支持搜索、展开/折叠及选中插件后启停。右侧提供用途介绍、功能节点、界面预览、端口与默认参数，多视图可切换；`plugin.directory` 打开本地插件文件夹。导入由插件库或 plugin.import 完成；工程副本在工程导出和打开版本选择流程处理。本地设置与工程内激活是分开的，不能用工程内版本覆盖本地状态查询。

`library.preview(plugin, scope)` 返回 definition、config、geometry 和 view_urls。scope 默认为 project，使用工程当前选定且启用的插件；library 使用本机安装版本，包括停用节点。预览读取默认参数和打包的真实 QML，不创建运行节点。原生版本已加载冲突时，geometry.error 提示重启，仍返回本机介绍与界面资源。

管理树的插件和分类复选框控制本地启用状态，分类覆盖其完整子树，包含折叠或搜索隐藏的插件。`plugin.enable` 接受单个 plugin 或 plugins 节点 ID 数组（二者选一）和 enabled；整批先检查是否存在及工程引用，任何一个不允许停用时整批保持原状。分类显示全启用、部分启用、全停用三种状态。

`project.options` 查询或修改 include_plugins；修改会标记未保存。project.save 沿用设置，project.export 可单独指定并保持当前工程路径不变。携带时写入用到的全部官方/用户包和资源；不携带时保留 plugin_dependencies，包括 id、version、sha256 和 relative_path。目录关系仅存在工程中，插件包内不保存分类。

project.open 在更换工程前返回 requires_plugins/missing_plugins、requires_plugin_selection/conflicts 或 requires_restart/plugins。版本冲突需要明确传入 plugin_selections={包ID:project/local} 再打开；每次重新打开存在冲突的工程重新选择。打开不自动运行。

plugin.export 的单包参数为 plugin/path；批量参数为 plugins（包ID列表）、directory、scope。批量导出保留目录，存在目标时返回 requires_confirmation/conflicts，未写入任何包；确认后传 overwrite=true。scope=project 导出工程当前选定版本，scope=library 导出本地版本。plugin.export.dialog 打开相同树状勾选界面。正在使用的节点不能停用，包不能移除；失败返回引用。

update.state 查询安装维护支持；update.check 返回 job，完成后 available/updates 来自真实维护工具。update.install 在工程已保存后启动安装维护并退出应用，完成安装由维护工具负责。app.restart 保留数据目录、工程路径与指定的插件版本选择。正式更新源需部署后才能在线使用；生成本地仓库不代表已经发布。

## 插件属性面板、草稿与扩展类型

`property.get(scope,target)` 返回绑定、schema、values、draft、text_draft、dirty、pending、error 和 editor。scope 为 node、ui 或 workspace；target 是节点 ID、界面对象 ID 或工作区插件 ID。editor 是插件提供的自定义 QML 面板 URL；为空时使用默认表单。`property.stage` 写入完整字段值，`property.text` 保留尚未解析的数值或 JSON 原文，提交时才解析；无效文本会保留草稿和失败原因。`property.commit` 支持 confirmed 与 revision：返回 requires_confirmation 和 affected_wires 时表示端口变化会断开连线，原草稿保持；带同一 revision、confirmed=true 再提交，属性变化与断线作为一步撤销。`property.discard` 放弃草稿，`property.drafts` 列出所有未应用草稿。

节点配置的 `node.configure.plan(node,changes)` 预演端口与连线影响，不修改工程。GUI 在切换选择、切换工作区、关闭工程或退出前检查草稿，提示应用、放弃或取消；异步提交完成前保持 pending。插件可用 property_commit=batch 决定一次提交，宿主记录一步撤销。

插件包 `implementation` 支持 native、python 和 qml。声明 `kind=node` 为计算节点；`kind=container` 使用 layout_script 和可选 qml；`kind=workspace` 使用 qml。纯 QML 包用于容器和工作区，不生成占位计算节点。容器 `container.layout(plugin,properties,children,width,height,grid)` 查询同一布局结果；`workspace.list` 列出当前工作区配置，`workspace.configure` 提交配置，`workspace.remove` 移除配置。工作区配置保存在 `project.workspaces`，完整插件随工程携带。

Python 插件按包使用独立工作进程；同包节点各自持有 namespace 和 state，生产者通过 `ServiceContext` 发布，异步动作返回可查询、可取消的 `python.background` 任务。`job.list` 列出任务，`job.cancel(id)` 请求协作取消；不可协作时终止该包进程。`resources.get` 查询资源额度，`resources.set(limits,reset)` 调整或恢复。`node.configure.plan` 与 resources 都可由 GUI 和 AI/API 共用。

## 命令字段

以下表格由本次原生实例的 schema 生成；节点/视图动作内层 args 以 registry 或 view.actions 为准。

| 命令 | 参数 |
|---|---|
| `app.quit` | discard |
| `app.restart` | discard, path, plugin_selections |
| `app.state` |  |
| `batch` | commands |
| `capture` | target, path, width, height |
| `clipboard.read` |  |
| `container.layout` | id, width, height, grid |
| `controls.click` | id, page |
| `controls.commit` | id, page |
| `controls.focus` | id, page |
| `controls.get` | id, page |
| `controls.key` | id, page, key, text, modifiers |
| `controls.list` | page |
| `controls.pointer` | id, page, type, x, y, button, buttons, delta_x, delta_y, modifiers |
| `controls.set` | id, page, property, value |
| `editor.add` | id, page, plugin |
| `editor.configure` | page, zoom, pan_x, pan_y, left_width, right_width, fit, animation |
| `editor.copy` | page, ids |
| `editor.paste` | page, payload |
| `editor.place` | page, id, ids, position, parent, index |
| `editor.plan` | page, ids, operation, plugin |
| `editor.remove` | page, id, ids, confirmed, revision |
| `editor.reorder` | page, id, index |
| `editor.replace` | page, id, ids, plugin, confirmed, revision |
| `editor.state` | page |
| `editor.withdraw` | page, id, ids |
| `graph.activity` |  |
| `graph.connection.options` | node, port |
| `graph.fit` |  |
| `graph.node.add` | id, plugin, title, config, position, orientation, placed |
| `graph.node.move` | id, position |
| `graph.node.patch` | id, changes |
| `graph.node.remove` | id, ids |
| `graph.node.rotate` | id, degrees |
| `graph.overlaps` |  |
| `graph.select` | id, kind, ids |
| `graph.wire.add` | id, source, target, routing, color, width, points |
| `graph.wire.handles` | id, x, y |
| `graph.wire.patch` | id, changes |
| `graph.wire.remove` | id |
| `history.redo` |  |
| `history.undo` |  |
| `io.ports` |  |
| `job.cancel` | id |
| `job.get` | id |
| `job.list` |  |
| `library.configure` | view, size |
| `library.favorite` | plugin, enabled |
| `library.preview` | plugin, scope |
| `library.search` | query, category, scope |
| `library.state` |  |
| `log.clear` |  |
| `log.configure` | detail |
| `log.export` | path, node, level, text, batch |
| `log.query` | after, limit, node, level, text, batch |
| `node.action` | node, action, args |
| `node.configure` | node, changes, persist, confirmed, revision |
| `node.configure.plan` | node, changes |
| `node.state` | node, detailed |
| `plugin.directory` |  |
| `plugin.enable` | plugin, plugins, enabled |
| `plugin.export` | plugin, path, plugins, directory, overwrite, scope |
| `plugin.export.dialog` | scope, plugin |
| `plugin.import` | path, manifest |
| `plugin.remove` | plugin, package |
| `plugin.settings` |  |
| `plugin.state` |  |
| `plugin.tree` | scope |
| `preferences.set` | theme, python, read_only |
| `project.export` | path, include_plugins |
| `project.get` |  |
| `project.new` | name, discard |
| `project.open` | path, discard, plugin_selections |
| `project.options` | include_plugins |
| `project.rename` | name |
| `project.save` | path, include_plugins |
| `project.validate` |  |
| `property.commit` | scope, target, changes, confirmed, revision |
| `property.discard` | scope, target |
| `property.drafts` |  |
| `property.get` | scope, target |
| `property.stage` | scope, target, changes |
| `property.text` | scope, target, field, text |
| `python.probe` | dependencies |
| `registry` |  |
| `resources.get` |  |
| `resources.set` | limits, reset |
| `runtime.debug` | enabled |
| `runtime.emit` | node, outputs |
| `runtime.pause` |  |
| `runtime.reset` |  |
| `runtime.start` | debug |
| `runtime.state` |  |
| `runtime.step` | mode, source |
| `runtime.stop` |  |
| `schema` |  |
| `sync.apply` | direction, keys, revision |
| `sync.plan` | direction |
| `ui.container.add` | id, parent, plugin, properties |
| `ui.item.move` | id, parent, index |
| `ui.item.patch` | id, properties |
| `ui.item.remove` | id |
| `ui.item.schema` | id |
| `ui.update` |  |
| `ui.view.add` | id, parent, node, view, properties |
| `ui.view.bind` | id, node, view |
| `update.check` |  |
| `update.install` |  |
| `update.state` |  |
| `view.actions` | node, view |
| `view.call` | node, view, action, args |
| `view.read` | node, view, args |
| `window.close` | window, discard |
| `window.configure` | window, x, y, width, height, state, focus |
| `window.pin` | window, enabled |
| `workspace.close` | page, discard |
| `workspace.configure` | page, changes |
| `workspace.detach` | page, x, y |
| `workspace.list` |  |
| `workspace.move` | page, window, index |
| `workspace.open` | page, window |
| `workspace.remove` | page |
