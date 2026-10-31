# CoRig 插件 SDK 2

SDK 与 CoRig 0.4.1 Windows x64 配套。当前构建使用 Qt 6.12.0 / MSVC 1944；准备匹配架构与运行模式的 Qt、MSVC v14 工具和 CMake 3.29+，无需主程序源码。SDK 1 的 C++ 插件需要重新编译。

```powershell
.\examples\native_gain\build.ps1 -Sdk D:\CoRigSdk -QtPrefix D:\Qt\6.12.0\msvc2022_64
```

将生成的 `examples/native_gain/build/NativeGain.crpl` 从 CoRig 的“导入插件”导入。连接“数值输入 → C++ 数值增益 → 数值显示”，默认输出为输入乘以 2，并按上下界限幅；参数与外观由 C++ 检查。示例视图的“重置”按钮通过同一动作接口输出 0。

修改自己的工程时保留顶层 `format`、`package_api` 和 `implementation`，替换包 ID 与节点 ID，再实现 gain.cpp 中的计算。plugin.json 的 files 列出 QML 与辅助文件；打包工具自动读取 DLL 并记录宿主兼容信息。C++ 代码只需包含 `<corig/plugin.h>`。

调试使用独立的 CoRig `--data-dir`，通过 Visual Studio 或 VS Code 启动、附加 CoRig 进程；更新已加载的原生包后重启测试实例。生产 SDK 为 Release，插件也应使用 Release 构建。完整生命周期、端口类型、动作、QML bridge、工程携带和 AI 接口见 [插件开发](docs/PLUGIN_SDK.md)。

`examples/python_signal_lab` 提供自定义面板、动态端口、后台合成信号与可取消的数值计算；`examples/qml_workspace` 提供无需 Python 的容器和工作区。两者可用 `bin/CoRig-pack.exe pack <plugin.json> - <output.crpl>` 打包，资源会随包保存。
