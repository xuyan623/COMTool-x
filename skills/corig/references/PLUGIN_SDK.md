# CoRig 用户插件 v3

用户插件以 JSON 声明、Python 逻辑及可选 QML 视图组成。官方 C++ 插件随软件提供；旧 PySide6/PyQt5 NodeRuntime 接口不兼容。

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

只使用 bridge，不依赖宿主的私有 C++ 对象。视图资源、Python 代码和辅助文件随 `.corig` 保存；切换工程会卸载旧工程插件，解释器路径不会带给其他电脑。

## AI 控件操作

已加载的 QML 控件无需额外注解即可被 controls.list 发现，包括未设置 objectName 的控件。每个对象提供存活期间稳定的 handle，以及描述当前视觉树的 path；可通过 page、node、view 和 ui_item 找到正确视图。明确命名仍适合长期脚本；自动 handle 在视图重建或工程切换后需要重新发现。

标准输入控件使用 controls.set／commit／click；set 的 value、currentIndex 和 checked 会执行相应的用户操作回调。自绘控件可通过 controls.key／pointer 接收应用内部 Qt 事件，无需控制系统鼠标或键盘。业务动作和文件路径优先通过声明的 node.action 传递，避免依赖文件对话框。控件回调产生的 jobs 必须逐个等待完成；原生 CLI 的 --wait 会自动处理。
