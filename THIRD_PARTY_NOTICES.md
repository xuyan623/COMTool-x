# 来源与第三方组件

COMTool-x 源自 [Neutree/COMTool](https://github.com/Neutree/COMTool) 的通信调试需求、曲线协议与功能设计。

原版采用 LGPL-3.0，项目根目录保留原 LICENSE。COMTool-x 的原生 Qt 外壳、功能图、UI 模型、调度和 API 在独立目录实现，按同一许可证提供源码。旧程序目录保持独立。

主要运行组件：

| 组件 | 用途 | 许可证 |
|---|---|---|
| CPython | Python 运行时 | PSF |
| PySide6 / Qt | 原生桌面界面 | LGPL-3.0 / Qt 组件适用许可证 |
| pyqtgraph | 实时曲线 | MIT |
| NumPy | 数值存储 | BSD-3-Clause |
| pyserial | 串口 | BSD |
| Paramiko | SSH | LGPL-2.1 |
| cryptography / bcrypt / PyNaCl | SSH 加密依赖 | 各发行包附带许可证 |
| pyte | ANSI 终端解析 | LGPL-3.0 |
| PyInstaller | 便携打包 | GPL，含引导程序分发例外 |

精确版本见 `pyproject.toml`、`uv.lock`。重新分发时一并提供本项目对应源码和第三方发行包要求的许可证。
