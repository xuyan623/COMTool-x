# CoRig 插件开发

官方和用户插件统一使用 `.crpl` 插件包；C++ 与 Python 共用导入、查询、启停、工程携带和导出流程。旧 PySide6/PyQt5 NodeRuntime 接口不兼容。

## 统一插件包

`.crpl` 是标准 ZIP，根目录的 `plugin.json` 声明 `format=corig-plugin`、`package_api=1`、`id`、`name`、`version`、`implementation` 和 `plugins`。`implementation` 为 native 或 python；一个包可以提供多个节点，节点仍拥有各自的 id、ports、properties、actions 和可选 views。节点标识不能与其他包冲突。

原生包包含编译好的 DLL，Python 包包含 `.py`；QML 和辅助文件随包保存，所有路径相对于包根目录。包内禁止绝对路径、越界路径、符号链接、重复路径和损坏的 ZIP 内容。包上限 128 MiB，展开上限 512 MiB。

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

## Qt/C++ SDK

安装可选的“C++ 插件开发 SDK”，或解压 `CoRig-SDK-Windows-x64.zip`。SDK 提供公开头文件、导入库、打包工具和 `examples/native_gain` 完整示例，不需要主程序源码。当前 Windows x64 SDK 需要 Qt 6.12.0、MSVC 2022（编译器版本 1944）与 CMake 3.25+。普通用户只需安装 CoRig。

```powershell
.\examples\native_gain\build.ps1 -Sdk D:\CoRigSdk -QtPrefix D:\Qt\6.12.0\msvc2022_64
```

构建生成 `build/NativeGain.crpl`，可直接从“导入插件”导入。示例包含真实增益计算、reset 动作和 QML 视图。插件在 CoRig 进程内运行，使用与宿主匹配的 Release SDK；内存错误可能影响宿主。C++ 插件更新后重启生效，热重载不属于当前契约。

入口为 `<corig/plugin.h>`。实现 `corig::NativePlugin::create`，返回派生于 `corig::Node` 的节点；通过 `Q_PLUGIN_METADATA` 和 `Q_INTERFACES` 声明公开接口。`process(changes,context,tick)` 读取保留的 inputs_ 与本次 changes，返回 `NodeResult::output`；`action` 实现声明动作。引擎拥有运行、暂停、停止、重置、配置提交和批次调度。

静态端口和参数使用声明；额外参数规则与动态端口可覆盖 `configuration`、`ports`。`source_interval_ms` 用于 Python 自主信号源，C++ 节点覆盖 `sourceInterval()`；`source=true` 声明可由动作启动的信号源，`defer_inputs=true` 将具有外部副作用的输入安排在普通计算之后。动作型布尔输入应只处理本次 changes 中到达的 true。

Node 的计算和生命周期方法在引擎线程调用。同步发布使用 host_.publish；自有后台设备通过 `PluginService` 与线程安全的 `PluginEvents.receive/observe` 向引擎交付数据。QML 只消费观察快照，通过 bridge.action 调用同一动作入口，不直接访问活节点或设备。插件应及时响应暂停、停止及取消，不占用 GUI 线程执行长计算。

`CoRig-pack pack plugin.json plugin.dll output.crpl` 将原生二进制和 manifest 的 files 列表资源打包，并读取 DLL 的实际 Qt 插件元数据。SDK、Qt、系统架构、编译器和构建类型由工具记录，导入时校验。普通开发者不需要手工填写 native 元数据；辅助 DLL 应与主插件 DLL 同放在 bin/ 下。

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
- properties 描述参数类型、默认值、choices、min/max 等。ports 显式声明方向和数据类型；一个端口只连接一条导线。
- dependencies 是 Python 导入名列表。每次加载前检查；没有隐式 pip 安装。
- resource_files 将声明目录内的辅助文件加入资源，resources 可直接提供文本或 bytes。Python 可用 `from .helper import ...`；文件在独立包中展开。禁止越出资源目录。
- source_interval_ms 可让插件作为自主信号源；普通插件由上游输入更新触发。
- actions 的每项描述 args、可选 requires_paused；实现 `action_<名称>(inputs,state)`，从 inputs['args'] 读参数。动作同时收到当前端口输入、`inputs['__config__']` 和 `inputs['__context__']`，返回 `{'result':动作结果,'outputs':输出字典}`。
- timeout_ms 参数控制单次调用上限，默认 1000 ms；主机允许 10～60000 ms。超时会关闭工作进程，后续调用可重新启动。

插件 print/stderr 会接入节点日志。结果与 state 不能包含非有限数值、文件句柄或任意 Python 对象；每次请求/响应均限制 16 MiB。内置 math、json、struct、crc16 也可直接使用。

## QML 视图

views 的键是 view ID，值的 qml 指向打包资源。视图根对象必须声明 `required property var bridge`，由宿主加载时传入。

- bridge.state 是节点视图快照，包含 state、inputs、outputs 等；用户 process 的 state 位于 `bridge.state.state.script_state`。
- bridge.config 是当前参数。
- bridge.action(name,args) 使用与 GUI/API 相同的节点动作入口。

只使用 bridge，不依赖宿主的私有 C++ 对象。视图资源、Python 代码和辅助文件随 `.crpj` 保存；切换工程会卸载旧工程插件，解释器路径不会带给其他电脑。

## AI 控件操作

已加载的 QML 控件无需额外注解即可被 controls.list 发现，包括未设置 objectName 的控件。每个对象提供存活期间稳定的 handle，以及描述当前视觉树的 path；可通过 page、node、view 和 ui_item 找到正确视图。明确命名仍适合长期脚本；自动 handle 在视图重建或工程切换后需要重新发现。

标准输入控件使用 controls.set／commit／click；set 的 value、currentIndex 和 checked 会执行相应的用户操作回调。自绘控件可通过 controls.key／pointer 接收应用内部 Qt 事件，无需控制系统鼠标或键盘。业务动作和文件路径优先通过声明的 node.action 传递，避免依赖文件对话框。控件回调产生的 jobs 必须逐个等待完成；原生 CLI 的 --wait 会自动处理。
