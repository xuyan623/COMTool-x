# CoRig 插件开发

官方和用户插件统一使用 `.crpl` 插件包；C++、Python 和纯 QML 扩展共用导入、查询、启停、工程携带和导出流程。本文对应 CoRig 0.4.1 / C++ SDK 2。SDK 1 的 C++ 插件需要重新编译；Python v3 声明继续支持。

## 统一插件包

`.crpl` 是标准 ZIP，根目录的 `plugin.json` 声明 `format=corig-plugin`、`package_api=1`、`id`、`name`、`version`、`implementation` 和 `plugins`。`implementation` 为 native、python 或 qml；每项的 kind 为 node（默认）、container 或 workspace。纯 qml 包提供容器、工作区和资源，无需虚构计算节点。插件标识不能与其他包冲突。

原生包包含编译好的 DLL，Python 包包含 `.py`；QML、布局脚本和辅助文件随包保存，所有路径相对于包根目录。包内禁止绝对路径、越界路径、符号链接、重复路径和损坏的 ZIP 内容。默认压缩上限 128 MiB、展开上限 512 MiB，可在资源额度中调整。

```json
{
  "format":"corig-plugin", "package_api":1,
  "id":"user.example.gain", "name":"数值增益", "version":"1.0.0",
  "implementation":"python",
  "plugins":[{
    "id":"user.example.gain", "name":"数值增益", "code_file":"gain.py",
    "ports":[{"id":"x","direction":"in","data_type":"number"},{"id":"out","direction":"out","data_type":"number"}],
    "properties":{"gain":{"type":"number","default":2}},
    "views":{"result":{"qml":"Gain.qml"}}
  }]
}
```

本地插件管理按实际文件夹显示可搜索、展开和折叠的目录树，只提供状态与节点启用设置，不混入工程内副本；这页不提供导入、导出或移除按钮。导入位于插件库，工程插件导出位于文件菜单；文件操作 API 的移除和导出按完整包处理。官方每个节点一个包。分类由包文件所在的相对目录生成，根目录包归“其他”；移动包或目录后重启生效。包内不允许 category/category_path 分类字段。工程单独记录包的 relative_path，本机版本使用本机路径。

工程 `.crpj` 的 include_plugins 保存选项决定是否嵌入实际用到的完整包，包括官方插件及随包资源；关闭时保留 plugin_dependencies。首次保存和另存为选择，可随时通过 project.options 修改。打开时可使用工程内副本。版本或内容不同会返回冲突清单，用户必须选择 project（工程内）或 local（本机）；每次重新打开存在冲突的工程都重新选择。保存和导出采用本次实际选择的版本，本机插件库独立管理。

`plugin.import(path)` 导入 `.crpl`，`plugin.state` 查询本地包与节点，`plugin.enable(plugin,enabled)` 勾选节点，`plugin.remove(package)` 移除本机包，`plugin.export(plugin,path)` 导出当前工程使用的包。工程内的包可以直接导出，无需先安装到本机。`plugin.tree(scope=project/library)` 分别查询工程使用的包和本地文件目录树；`plugin.export(plugins,directory,scope)` 在目标已存在时先返回覆盖清单，`plugin.export(...,overwrite=true)` 执行已确认覆盖的批量导出并保留目录。`project.options` 查询或设置 include_plugins。当前工程仍引用的节点或包不能停用、移除，会列出引用。打开工程时，其必需插件在工程内启用，本机的其他勾选设置保留。

## 版本与兼容

每个插件包拥有独立版本，由插件声明决定；软件升级不会自动改写插件版本。工程记录依赖的包版本与内容校验值。打开工程时先比较版本：版本相同即视为兼容，不因重新打包产生的字节差异弹出冲突；版本不同才要求选择工程内或本机构建，冲突窗口默认选中工程内版本。未改动的插件不需要随软件发版重新编号。

已加载的原生模块仍按实际哈希判断是否需要重启：同一版本重新编译后，磁盘版本不会自动替换内存中的旧模块。

## Qt/C++ SDK

SDK 提供公开头文件、导入库、打包工具和 `examples/native_gain` 完整示例，不需要主程序源码。当前 Windows x64 SDK 使用 Qt 6.12.0、MSVC 1944 构建；独立工程要求 CMake 3.29+。SDK 查找允许同主版本中不低于 SDK 的 Qt，MSVC 使用 v14 ABI 家族；链接器不能早于 SDK 导入库的工具版本。运行时要求相同 SDK 主版本、系统、架构与 Debug/Release 模式，Qt 插件次版本不能高于宿主。其他组合的二进制仍需实际加载验证。

这些范围来自 [Qt 插件加载规则](https://doc.qt.io/qt-6/deployment-plugins.html) 与 [Microsoft C++ 二进制兼容规则](https://learn.microsoft.com/en-us/cpp/porting/binary-compat-2015-2017)。Microsoft 还要求运行库至少覆盖最新组件的工具版本，跨工具版本的 `/GL`、`/LTCG` 产物不在上述保证内。实际 DLL 依赖缺失会报告加载错误。当前随附 SDK 的最低 Qt 版本与宿主可加载旧 Qt 插件的范围是两个条件。

```powershell
.\examples\native_gain\build.ps1 -Sdk D:\CoRigSdk -QtPrefix D:\Qt\6.12.0\msvc2022_64
```

构建生成 `build/NativeGain.crpl`，可直接从“导入插件”导入。示例包含真实增益计算、reset 动作和 QML 视图。插件在 CoRig 进程内运行，使用与宿主匹配的 Release SDK；内存错误可能影响宿主。C++ 插件更新后重启生效，热重载不属于当前契约。

入口为 `<corig/plugin.h>`。实现 `corig::NativePlugin::create`，返回派生于 `corig::Node` 的节点；通过 `Q_PLUGIN_METADATA` 和 `Q_INTERFACES` 声明公开接口。`process(changes,context,tick)` 读取保留的 inputs_ 与本次 changes，返回 `NodeResult::output`；`action` 实现声明动作。引擎拥有运行、暂停、停止、重置、配置提交和批次调度。

静态端口和参数使用声明；额外功能规则与动态端口可覆盖 `configuration`、`ports`，界面属性规则覆盖 `presentationConfiguration(scope,definition,changes,previous)`。宿主提供当前完整属性 schema；返回值仍接受类型和可保存性检查。`source_interval_ms` 用于 Python 自主信号源，C++ 节点覆盖 `sourceInterval()`；`source=true` 声明可由动作启动的信号源，`defer_inputs=true` 将具有外部副作用的输入安排在普通计算之后。动作型布尔输入应只处理本次 changes 中到达的 true。

Node 的计算和生命周期方法在引擎线程调用。同步发布使用 host_.publish；自有后台设备通过 `PluginService` 与线程安全的 `PluginEvents.receive/observe` 向引擎交付数据。QML 只消费观察快照，通过 bridge.action 调用同一动作入口，不直接访问活节点或设备。插件应及时响应暂停、停止及取消，不占用 GUI 线程执行长计算。

`corig_add_plugin` 将原生二进制和 manifest 的 files 列表资源打包，读取 DLL 的实际 Qt 插件元数据，并传入实际 MSVC_VERSION。手工调用为 `CoRig-pack pack plugin.json plugin.dll output.crpl --compiler-version 1944`；省略编译器参数表示使用打包工具本身的工具链版本。辅助 DLL 应与主插件 DLL 同放在 bin/ 下。SDK 2 的接口 IID 为 `org.corig.NativePlugin/2.0`。

## Python 声明兼容入口

现有 v3 `.json` 声明仍可导入，导入后转入相同的插件包管理。以下保留其完整运行契约。

## 环境

在设置中选定本机 Python 3.10+，或调用 `preferences.set` 的 `python` 绝对路径。`python.probe` 可检查 `dependencies` 模块列表。软件不安装解释器或依赖；缺少依赖会显示模块名。只运行官方节点时不启动 Python。

## 运行示例

在插件库点击“导入用户插件”，选择同目录的 `plugin_examples/gain.json`。该声明引用 `gain.py` 和 `Gain.qml`。添加后连接 `control.number.value → user.example.gain.x → display.value.value`，运行后输出等于输入乘以 gain（默认 2）。

```json
{
  "plugin_api": 3,
  "id": "user.example.gain",
  "name": "数值增益",
  "code_file": "gain.py",
  "resource_files": ["Gain.qml"],
  "ports": [
    {"id":"x","direction":"in","data_type":"number"},
    {"id":"out","direction":"out","data_type":"number"}
  ],
  "properties": {"gain":{"type":"number","default":2}},
  "views": {"result":{"qml":"Gain.qml"}}
}
```

`process(inputs, state)` 读取完整输入，返回输出端口名称到值的字典；返回 None 表示本次不发布。`inputs['__config__']` 是当前参数，`inputs['__context__']` 包含 time（引擎时间，秒）、batch、resumed、changed（本次新到达的输入名列表）和 source_tick（是否为自主信号源调度）。state 是可序列化的私有状态，失败调用不提交新值。

输入/输出类型为 number、bytes、string、boolean、object、samples 或 any。samples 为 `{'time':秒,'values':{'通道名':数值}}`；bytes 在 Python 内为 bytes，外部 JSON 用 `{type:'bytes',base64:'...',length:N}`。

控制输入统一用 boolean 声明。动作型控制只在端口属于本次 changed 且值为 true 时执行；其他输入更新时，完整输入中保留的 true 不能重复当成新命令。重复到达的 true 仍各算一次命令；只识别状态变化时接边沿触发插件。用户动作的上下文中 changed 为空、source_tick 为 false。

```python
def process(inputs, state):
    changed = inputs['__context__']['changed']
    if 'start' in changed and inputs['start']:
        state['starts'] = state.get('starts', 0) + 1
    return {'count': state.get('starts', 0)}
```

```python
def process(inputs, state):
    result = inputs['x'] * inputs['__config__']['gain']
    state['last'] = result
    return {'out': result}
```

## 声明、资源和动作

- `plugin_api=3`、id、name、code/code_file 必填；ID 不可覆盖官方或已有插件。
- properties 描述 string、boolean、number、integer、json、object、array 类型、默认值、choices、min/max 等。嵌套数据也必须可保存，不能包含 NaN、无穷大或宿主对象。ports 显式声明方向和数据类型；一个端口只连接一条导线。
- dependencies 是 Python 导入名列表。每次加载前检查；没有隐式 pip 安装。
- resource_files 将声明目录内的辅助文件加入资源，resources 可直接提供文本或 bytes。Python 可用 `from .helper import ...`；文件在独立包中展开。禁止越出资源目录。
- source_interval_ms 可让插件作为自主信号源；普通插件由上游输入更新触发。
- actions 的每项描述 args、可选 requires_paused；实现 `action_<名称>(inputs,state)`，从 inputs['args'] 读参数。动作同时收到当前端口输入、`inputs['__config__']` 和 `inputs['__context__']`，返回 `{'result':动作结果,'outputs':输出字典}`。
- timeout_ms 参数控制普通同步调用时限，并受当前 python_timeout_ms 额度限制，默认 1000 ms。超时结束当前插件包的工作进程；其他包独立运行。长任务使用下述异步动作，不依靠无限延长同步调用。

插件 print/stderr 会被捕获；同步调用进入节点日志，后台任务日志随任务快照提供。结果与 state 不能包含非有限数值、文件句柄或任意 Python 对象；默认每条消息 16 MiB，可调整 python_message_mib。内置 math、json、struct、crc16 也可直接使用。

## QML 视图

views 的键是 view ID，值的 qml 指向打包资源。视图根对象必须声明 `required property var bridge`，由宿主加载时传入。

- bridge.state 是节点视图快照，包含 state、inputs、outputs 等；用户 process 的 state 位于 `bridge.state.state.script_state`。
- bridge.config 是当前参数。
- bridge.action(name,args) 使用与 GUI/API 相同的节点动作入口。
- 实际界面中的 bridge 按 UI 对象 ID 隔离，bridge.properties 是此外观实例的已提交属性；bridge.commit(changes) 修改此外观，bridge.configure(changes) 修改对应计算节点。两个视图即使绑定同一节点，也保留各自外观。
- bridge.info 提供 scope、target、node、view 与 schema 等信息。设计预览只读，不能通过预览 bridge 执行动作或提交。

只使用 bridge，不依赖宿主的私有 C++ 对象。视图资源、Python 代码和辅助文件随 `.crpj` 保存；切换工程会卸载旧工程插件，解释器路径不会带给其他电脑。

## AI 控件操作

已加载的 QML 控件无需额外注解即可被 controls.list 发现，包括未设置 objectName 的控件。每个对象提供存活期间稳定的 handle，以及描述当前视觉树的 path；可通过 page、node、view 和 ui_item 找到正确视图。明确命名仍适合长期脚本；自动 handle 在视图重建或工程切换后需要重新发现。

标准输入控件使用 controls.set／commit／click；set 的 value、currentIndex 和 checked 会执行相应的用户操作回调。自绘控件可通过 controls.key／pointer 接收应用内部 Qt 事件，无需控制系统鼠标或键盘。业务动作和文件路径优先通过声明的 node.action 传递，避免依赖文件对话框。控件回调产生的 jobs 必须逐个等待完成；原生 CLI 的 --wait 会自动处理。

## 属性面板、草稿与动态端口

功能 properties、通用 view_properties 和每个 views[view].properties 分开声明；容器和工作区使用各自 properties。界面位置、尺寸、网格、父子关系继续采用宿主语义。默认表单把基础属性折叠显示；插件字段支持 label、description、choices、visible_if 等。property_commit 为 immediate（默认，在一次字段编辑结束后提交）或 batch（显式应用整组）。

完整自定义面板通过 editors.node、editors.view、editors.container、editors.workspace 指定 QML 文件；单个视图可用 views[view].editor 覆盖。根项声明 `required property var bridge`。插件决定何时 stage 和 commit，一次成功提交对应一步撤销：

| bridge 成员 | 含义 |
|---|---|
| values / schema | 已提交值与可编辑字段 |
| draft / textDraft | 合并后的类型草稿，以及尚未解析的原始文本 |
| stage(changes) / stageText(field,text) | 保存草稿，不修改工程 |
| commit(changes={}) | 合并当前草稿并提交，可能返回 job 或确认清单 |
| discard() | 放弃尚未提交的草稿，正在提交时不可放弃 |
| dirty / pending / error / editable | 草稿、异步提交、错误与可编辑状态 |
| configure(changes) / action(name,args) | 修改功能配置或调用节点动作 |
| invoke(op,args) | 调用相同的公开编辑器命令 |

数字、JSON 尚未输入完整时也保留原文。离开脏面板可应用、放弃或取消；异步应用等到实际完成后再离开。修改同一字段的外部操作会使旧草稿冲突，保留草稿供检查。自定义面板应在文本变化时调用 stageText，不能仅把未提交文本存在局部 QML 变量中。

Python 通过 hooks.configure 与 hooks.ports 声明校验和动态端口函数；hooks.view_configure 校验界面外观。函数形如 `validate(config,context)`、`ports(config,context)`，context 包含 plugin、scope、schema/previous 或 declared。校验返回完整配置对象，端口回调返回带 id、direction、data_type 的数组。纯 QML 包可指定 hooks.script，用包内 JavaScript 提供相同的纯函数。校验阶段不允许通过 service 发布数据、修改运行状态或启动任务，模块导入也应保持无外部副作用。

`node.configure.plan` 返回规范化 config、ports、affected_wires 与 revision。受影响连线非空时，`node.configure` 先返回清单；调用方确认后携带 confirmed=true 与同一个 revision 重试。候选配置或设计改变会使旧确认失效。配置与断线一起提交、一起撤销；运行中改变端口契约需要先暂停。

## Python 后台服务和异步动作

每个插件包使用独立 Python 进程，同包节点拥有各自 namespace 和 state；校验 namespace 与运行节点分开。不能依赖不同节点之间隐式共享模块全局变量。需要明确协作时可使用 service.shared，它仅在当前包进程内共享。

`on_start(service)`、`on_resume(service)`、`on_pause(service)`、`on_stop(service)` 可管理生命周期。start 准备后台任务，resume 才推进；暂停保留工作，停止取消并清理，重置恢复工程初值。删除节点也会清理其任务和 namespace。

| ServiceContext | 行为 |
|---|---|
| config | 当前功能配置的快照，运行调参后同步更新 |
| background(function,*args,name=...) | 启动当前节点拥有的 producer，函数接收 context |
| publish(outputs) | 通过正常引擎入口发布已声明输出 |
| observe(state) | 合并观察字段，script_state 仍由状态提交管理 |
| checkpoint() / sleep(seconds) | 响应全局暂停、停止和取消 |
| cancelled() | 查询取消标志 |
| progress(value,message="") | 当前任务进度，value 为 0～1 |

actions[name].async=true 使用 `action_name(inputs,state,context)`，立即返回 public job，实际函数结束后才完成。局部 state 只在成功时提交；并发修改不同字段可合并，冲突字段会明确失败。普通同步动作继续使用两个参数。requires_paused=true 的异步动作可在暂停状态工作；其他任务随全局暂停等待。

`job.get` 返回 running、complete、failed 或 cancelled，进度和消息随真实任务更新；`job.cancel` 请求取消。任务必须定期调用 checkpoint/sleep。若未响应取消，宿主结束该包进程，同包其他任务会失败，其他包继续运行。取消前已发布的实时数据不回滚。界面的“设置 → 后台任务”使用相同任务入口。后台日志可从任务快照读取。

## 容器和工作区

kind=container 提供 layout_script，可选 qml 作为背景或附加界面。布局脚本实现 `arrange(properties,context)`；context 含 children、width、height、grid。返回与 children 同顺序的矩形数组，每项包含 x、y、width、height，可选 boolean visible。宿主检查返回值并对齐网格，用相同结果绘制设计与运行界面。子对象仍通过常规对象管理、拖放、复制与保存操作，不写入功能节点。

容器 QML 接收按 UI 实例隔离的 bridge。kind=workspace 提供 qml，可配置 properties 和自定义属性面板；根项接收 scope=workspace 的 bridge。workspace.open 首次添加配置，workspace.configure 一次提交属性，workspace.remove 从工程移除配置。关闭标签只隐藏页面；分离和回接移动同一个页面，不复制运行会话。工作区配置保存在 project.workspaces，使用的完整插件包随工程携带。

graph.symbol 是节点中心符号，graph.icon 指向包内图片；有非空 symbol 时优先显示符号。

## 资源额度与可运行示例

resources.get/set 与“设置 → 资源额度”共用设置。修改前停止运行会话；limits 只更新指定值，reset=true 恢复默认。包可用 requirements 声明所需额度，超限会说明需求与当前值。

默认额度：archive_mib=128、expanded_mib=512、package_files=4096、ports=128、resource_mib=128、python_message_mib=16、python_timeout_ms=1000、callback_timeout_ms=1000、event_queue_mib=32、event_queue_count=65536。event_queue_mib 约束接收字节缓存，其他数据也受队列条数约束；这些额度不是进程总内存承诺。

| SDK 示例 | 可直接验证的能力 |
|---|---|
| examples/native_gain | SDK 2 独立编译、真实增益/限幅、C++ 功能及外观校验、符号 |
| examples/python_signal_lab | 合成信号、多路动态端口、跨字段校验、自定义面板、每视图外观、后台生产与异步 RMS |
| examples/qml_workspace | 无 Python 的容器布局、JavaScript 校验、工作区分离与工程携带 |

后两个示例使用 `CoRig-pack pack <plugin.json> - <output.crpl>` 打包。信号示例计算合成数据和数值 RMS，不代表任何实机传感器测量。
