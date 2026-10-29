# 验证范围

原生 C++ / Qt Quick 工作台使用真实实现，不复用旧 Python/PySide6 的测试结论。

## 原生测试

`tools/build_native.ps1 -Test` 验证 core、editor、workbench_ui、execution、io。本地 Python 扩展检查通过 COMTOOLX_TEST_PYTHON 指定真实解释器。

覆盖类型化工程、单源批次传播、暂停与单步、界面编辑、通信、曲线原始数据与 FFT、三维姿态、ANSI 终端和独立 Python 执行。

## 部署与实际设备

`tools/native_release_check.py` 对完整便携目录检查无开发路径启动、软件回环、仪表、窗口、Python/QML 扩展和本地接口。物理串口、真实设备执行及不同机器仍需另行验证。
