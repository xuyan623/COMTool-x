# 插件开发

核心外壳负责工程、注册表、端口检查、调度、编辑器和 API。通信、协议、曲线、控件、布局与导线均由插件注册。

## 最小可运行示例

`docs/plugin_examples/gain.py` 是实际工作的数值增益节点。通过“插件 → 加载本地插件”选择它，或者调用 `plugin.load`。在功能图中连接：

~~~text
数值输入.value → 数值增益.in → 数值显示.value
~~~

运行后修改数值，输出等于输入乘以 `gain`。示例还提供自己的视图与可发现的 `prefix` 属性。

插件通过菜单或 `plugin.load` 导入当前工程，启动时不扫描外部插件目录。保存 `.comtoolx` 时自动保存实际使用的插件实现快照，包含内置实现；未使用的外部插件不会打包。打开工程从包内加载实现，关闭或切换工程后卸载，失败打开保留当前工程。旧 JSON 工程不受支持。

插件入口可以通过相对导入引用同目录的 Python 辅助模块，保存时会一并收集。用 `PROJECT_FILES = ["assets", "calibration.json"]` 声明还需要随插件携带的文件或子目录，路径不能越出插件目录。资源在打开后的插件目录中保持相对位置，可通过 `Path(__file__).parent` 读取。源代码与这些资源在导入时形成快照；修改源文件后需要重新导入，工程包不会静默采用磁盘上的新版本。

配置中引用输入文件时，属性声明使用 `type="string", format="file"`；保存时携带文件，打开后为运行实例提供包内文件路径。设计配置原值保留，重保存时仍携带包内资源。输出文件路径无需此标记。

## 节点契约

入口必须实现 `register(registry)`，并注册 `Definition`：

- `id`：稳定且唯一，升级时保持相同含义。
- `ports`：`Port(id,label,direction,data_type)`；direction 为 `in/out`，同一节点内各端口使用不同 ID。
- `properties`：属性声明，支持 string、number、integer、boolean、json，以及 default/min/max/choices/pattern。
- `factory(node,engine)`：创建 `NodeRuntime` 实例，构造阶段不得连接设备或发送数据。
- `views`：`{view_name: factory(runtime, request, properties)}`，返回 QWidget。
- `actions`：暴露可操作能力与参数说明；`requires_running` 默认 true；轻量操作可设置 `immediate:true`。
- `view_properties`：按视图名称声明额外可编辑属性，供属性校验和 AI 发现。
- `breaks_cycle`：仅真正通过异步 IO 隔断同步回路的节点才能声明。

`NodeRuntime` 提供线程锁、停止事件、快照和输出检查。`receive(port,data)` 处理输入；`action(name,args)` 执行声明的操作；`emit(port,data)` 返回 Future，表示这次下游交付的结果。需要可靠发送结果时等待该 Future，不能把“入队”写成“已发送”。

端口当前使用 bytes、string、number、boolean、object、samples、trigger、any；导线不执行隐式转换。samples 是 `{channel: {"x":[...],"y":[...]}}`。每个端口一对一，分发与聚合由后续处理插件实现。

## 线程与视图

图处理、动作、设备 RX/TX 与 Qt 绘图分别执行。普通动作在有序工作线程执行；`immediate` 动作在调用线程执行，因此只适合短时状态调整。禁止工作线程直接操作 QWidget。

视图读取 `runtime.snapshot()` 或通过 `request(action,args)` 发起动作。定时器应以视图 QWidget 为 parent，使删除视图时一同停止。初始化控件时先赋值再连接信号，避免打开工程就发送指令。

可给根视图提供：

~~~python
widget.refresh_from_runtime = refresh
widget.api_actions = {"custom_action": callback}
~~~

`refresh()` 无参数、只更新显示，内部截图前会调用。专用 API action 接收 args，执行于 Qt 线程，不得阻塞。为重要控件设置稳定的 `objectName`，外壳会附加 `layout/<item-id>/` 或 `runtime/<item-id>/` 命名空间。

## 导线与布局插件

`WireDefinition(id,name,properties,factory)` 的 factory 接收 `(wire,source_point,target_point)`，返回 QGraphicsItem；编辑端点时调用其 `update_endpoints(source,target)`。内置导线使用原生曲线和箭头。

渲染时的 wire 还包含 `source_rotation`、`target_rotation`，表示端点模块当前的角度；它们由外壳计算，不重复写入工程导线数据。导线插件可以据此调整曲线切线和箭头方向。内置导线显示在模块主体上方，端点附近的鼠标事件会让给端口。

内置导线在 `properties.points` 保存可编辑路径点，选中后提供拖动、插入、删除及重置交互，通过统一的 `graph.wire.patch` 提交为可撤销操作。`type=json, format=points` 声明有限二维坐标列表，供图形编辑器和 API 使用相同校验。

`ContainerDefinition(id,name,properties,factory)` 的 factory 接收 `(item,children)`，children 为 `(child_item,child_widget)` 列表。负责拉伸、网格或标签页排版，不创建新的功能实例。

外部插件使用便携包已包含的依赖时可直接加载。工程包携带插件实现和声明资源，公共 SDK、Qt 及其他第三方运行库由 COMTool-x 提供；新增运行库需要在构建环境安装并重新打包程序。工程包中的 Python 插件及自定义 Python 协议会执行本地代码，只打开可信来源的工程。
