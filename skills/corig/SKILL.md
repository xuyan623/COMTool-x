---
name: corig
description: 通过联调 CoRig 的本地 API 创建工程、搭建功能与界面、配置插件、运行调试并获取内部截图。用于 CoRig 软件的实际操作与结果核验。
---

# 联调 · CoRig

优先使用原生本地 API 完成设计、查询和运行控制。

## 连接与核对

定位运行程序或用户指定的 --data-dir，读取其中 `.sessions/api.json`。凭据不得写入工程、日志或回答。原生程序仅监听本机，接口为 `GET /v3/schema`、`POST /v3/command`，全部需要认证。连接记录和 schema 必须声明 application=CoRig，拒绝其他产品或没有产品标识的旧记录。

使用 `CoRig-cli.exe api <op> --json-file <path> --data-dir <dir> --wait`；简单参数可用 --json。CLI 不需要 Python；默认记录失效时探测 `.sessions/instances/` 的独立进程记录。一个工程的多个子窗口共享同一接口；多个程序实例先核对 app.state 的 project/path。

先读取 app.state、schema、registry 和 project.get，以真实节点/端口/属性/动作契约为准。异步 job 或控件回调返回的 jobs 必须通过 job.get 逐个等待 complete/failed，不能把提交成功当作执行成功；原生 CLI 的 --wait 支持两种返回形式。

## 设计与保存

功能设计定义实际执行。修改结构前暂停运行；保持用户已有设计，未保存时先保存。editor.add 按 page=graph/layout 添加待放置对象，editor.place 落位，editor.withdraw 撤下保留配置；未放置功能节点不参与计算。graph.node.add 是显式直接建图接口，可用 placed=false 保持待放置；每个端口只连一条导线，分发/汇合使用实际处理节点。

双向同步先 sync.plan（direction=graph_to_ui/ui_to_graph），展示默认全选的 changes，再将已确认的 keys 和同一 revision 交给 sync.apply。目标多余对象会列入删除，可取消勾选保留；新增进入目标待放置区，已有位置、图层、ID 保留，重复同步不重复创建。不能把 ui.update 当作完整双向同步。

删除／替换前用 editor.plan 展示影响，确认后 editor.remove/replace 携带 confirmed=true 和 revision。editor.copy/paste 独立复制新 ID，外部连线不继承。功能属性只用 node.configure；界面外观和布局只用 ui.item.patch，不以界面参数覆盖功能初值。layout.canvas 的 width/height 为设计尺寸，grid_size 为 1～128 px；所有界面位置、尺寸及容器排列吸附网格，运行时整图等比缩放。对象栏保留文字缩进以表示容器层级。容器名称仅在对象层级显示，设计与运行均不显示容器标题，也不保留标题栏空间。

project.validate 检查工程结构；graph.overlaps 单独查询节点重叠与间距警告，生成或调整布局后应确认结果为空。capture 获取真实窗口或页面截图并查看本地图片，截图不能替代运行数据。目标窗口需要显示和恢复。共享引擎控件的 ID 是 execution.*，独立于当前工作区。

用 ui.item.schema 查询外观字段，ui.view.bind 修改已有视图绑定并保留布局。graph.connection.options 查询端口兼容性。editor.state/configure 按 page=graph/layout 查询／设置画布 zoom、pan_x、pan_y、left_width、right_width；侧栏宽度 0 表示收起，fit=true 适应画布，graph 的 animation 控制数据活动显示。它们只改显示状态。window.configure 设置窗口几何与 normal/minimized/maximized/fullscreen，可先恢复窗口再截图。

project.save 保存用户指定的单文件 .crpj，未指定路径时默认写入程序数据目录的 project/。v3 工程携带用户插件、辅助文件、QML 和模型资源；官方 C++ 插件由程序提供，解释器路径仅在本机设置中。只读取 CoRig 新格式，不迁移旧工程或录制文件。打开工程不自动运行、连接或发送。

## 运行与观测

只在用户授权范围内启动、连接设备或发送。设计请求本身不授权硬件动作。优先 node.action 并等待 job；发送成功只是本机写入/接收请求，设备执行须有反馈。

runtime.pause 保留实验，runtime.stop 释放连接并保留参数，runtime.reset 恢复工程初值；只读临时调参使用 node.configure persist=false。通过 node.state、view.read、节点 read_data/geometry/read_screen 等动作读取真实数据。先查 view.actions 再使用 view.call 操作相机、范围或导出。graph.activity 仅观测实际绘图层，不影响计算。

控制信号统一为 boolean。按钮只发布 true；接收动作只处理本次新到达的 true，边沿识别由 logic.edge 完成。logic.and/or/not/nand/nor/xor/xnor 是七种逻辑门；source.timer 支持 periodic/countdown，start 重新计时，stop 停止。data.fork 默认 a/b 两路，outputs 名称列表限制 2～8；右栏使用数量控件。

信号录制使用 data.record（原始字节日志仍为 data.recorder），同类型 in/out 始终直通，布尔 start/pause 控制采集；动作 start/pause/save(path)/reset。暂停时长从录制时间轴中扣除，信号内容原样保存为独立 .crrc 文件，工程只保存设计。引擎重置清空未保存录制，插件不能豁免全局控制。

data.playback 的设计 path 是默认外部文件引用；load(path) 仅临时导入。导入保持待播放，不自动切换截断开关；cutoff(value=true) 屏蔽实时输入但不自动播放，play/pause 控制进度。seek(position秒) 截断时输出目标记录一次，step(direction=-1/1) 逐条并暂停，set_speed(value=0.125～16) 调倍速。关闭截断恢复直通并暂停文件；暂停、结束和读取错误保持截断。回放只发布信号，不恢复下游历史。reset 清除临时文件和进度，再按设计加载默认引用；外部文件内容不写入工程。

录制控件 ID 为 recording.<node>.toggle/save/reset；回放为 playback.<node>.load/toggle/rewind/previous/next/cutoff/position/speed。底部 execution.run 按真实状态合并运行／暂停，明确操作优先 runtime.start/runtime.pause。controls.get 提供支持控件的 pressed/down/hovered；窗口 exposed/native_visible/frame_count 用于确认真正显示及绘制后再截图。state=normal 可恢复隐藏启动的 Windows 窗口。新增控件仍先 controls.list 再操作，后台任务逐个确认完成。

workspace.open 定位已有页面，插件库打开唯一精简子窗口，不能作为工作区标签；中间卡片左侧功能设计、右侧界面，中间为浅色竖分割线；右侧详情保留上下双预览。顶部为工作区标签，底部为共享引擎；分离窗口默认按聚焦关系排列，仅手动置顶时保持最上层；拖出和关闭页面不会结束引擎。用户没有要求丢弃设计时，先保存再退出。

controls.list 可按 page 发现具名及未命名 QML 控件，记录包含 node/view/ui_item、可读值、writable 与 actions。优先使用明确命名的 ID；自动 handle 在实际对象存活期间稳定，视图重建／切换工程后重新发现，不能持久保存。controls.get 读取状态；controls.set 操作 writable 字段，文本再 controls.commit。标准滑块、下拉框、SpinBox 和勾选框会触发真实操作。自绘控件使用 controls.key/pointer 直接向应用内 Qt 对象投递事件，无需桌面自动化；业务动作仍优先 node.action，文件通过结构化路径传递。

## 本地 Python

官方插件不依赖 Python。用户扩展只使用用户指定的本地 Python 3.10+；preferences.set 选择解释器，python.probe 检查模块。缺依赖时明确报告，不自动安装或修改环境。用户插件由 JSON 声明及 Python/QML 资源组成，使用 plugin.import，不能加载旧 NodeRuntime 或 PyQt 插件。

详细字段见 [API](references/API.md) 和 [插件 SDK](references/PLUGIN_SDK.md)。

交互规则：节点使用小圆角直边；输入为凹口，输出为囊泡。点击输出牵引，接入时填合凹口；取出可接回或改接。右键、Esc、空白或源端短线取消并回母体断线。拿着其他对象时右键也取消，空闲时才快速放置。界面编辑手势由画布统一处理，不能用运行控件动作替代设计拖放。
