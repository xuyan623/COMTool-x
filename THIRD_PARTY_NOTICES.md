# 来源与第三方组件

COMTool-x 源自 [Neutree/COMTool](https://github.com/Neutree/COMTool) 的通信调试需求、曲线协议与功能设计。本项目保留根目录 LICENSE，采用 LGPL-3.0。原版程序目录独立于本工程。

| 组件 | 本次构建版本 | 用途 | 许可文件 |
|---|---|---|---|
| Qt | 6.12.0，MSVC x64 | 动态链接的 Core/Gui/Qml/Quick/QuickControls2/Network/SerialPort/Svg/Core5Compat | licenses/Qt/ |
| libssh2 | 1.11.1 | 原生 SSH 客户端，静态链接 | licenses/libssh2.txt；third_party/libssh2-1.11.1/COPYING |
| OpenSSL | 3.5.9 | SSH 加密，静态链接 | licenses/OpenSSL.txt（Apache-2.0） |
| Microsoft Visual C++ runtime | 构建机 Qt 部署工具所带版本 | C++ 运行库 | 随 Qt 部署的 Microsoft 可再发行组件 |

Qt 采用对应开源许可动态链接，不禁止用户以匹配 ABI 的自建 Qt 替换随包 DLL。`licenses/Qt/THIRD_PARTY_NOTICES.txt` 来自本次 Qt SDK 的 SPDX 声明；GPL/LGPL 及 Qt 例外文本一同附带。Qt 对应源码可从 [Qt 6.12 源码目录](https://download.qt.io/archive/qt/6.12/6.12.0/single/) 获取。

对应的 COMTool-x C++/QML/Python worker 源码、构建脚本及 libssh2 源码随 `COMTool-x-source.zip` 提供。官方程序不携带 CPython、PySide6、NumPy、pyqtgraph、Paramiko、pyserial、pyte 或 PyInstaller。

用户自选的 Python 环境及其依赖由用户管理；测试工具使用本机 Python，SSH 软件服务器验收使用本机 Paramiko，该环境不进入发布包。
