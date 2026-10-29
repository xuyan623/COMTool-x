# CoRig 构建与发布

[项目首页](../README.md) · [源码仓库](https://github.com/xuyan623/CoRig) · [验证范围](VERIFICATION.md)

## 开发环境

当前 Windows x64 发布使用：

- MSVC 2022 x64、CMake 3.25+ 和 Ninja。
- Qt 6.12.0 MSVC x64：Core、Test、Gui、Qml、Quick、QuickControls2、Network、SerialPort、Svg、Core5Compat。
- 仓库自带的 libssh2 1.11.1 源码，以及本机 OpenSSL 3.5.9 静态开发库。

```powershell
git clone https://github.com/xuyan623/CoRig.git
cd CoRig
```

`tools/build_native.ps1` 中的 Visual Studio、Qt 路径和 CMake 中的 OpenSSL 默认路径对应维护者环境；在其他机器上需按实际安装位置调整。Qt 可通过 `-QtPrefix` 传入，OpenSSL 可通过 CMake 的 `OPENSSL_ROOT_DIR` 指定。使用 Qt 官方安装工具准备 SDK；`tools/install_qt_sdk.py` 是可选安装辅助工具，需要单独的 Python/下载环境。

## 构建和原生测试

```powershell
.\tools\build_native.ps1 -Test
# Qt 安装在其他位置时：
.\tools\build_native.ps1 -QtPrefix C:\Qt\6.12.0\msvc2022_64 -Test
```

脚本在当前进程导入 MSVC 环境，使用 CMake/Ninja 构建，并运行七套 CTest。输出在 `build/native/`，包括 GUI、CLI 和测试程序。

构建和官方 C++ 测试无需安装 Python。若需验证用户 Python 插件，可设置 `CORIG_TEST_PYTHON` 为实际解释器。界面与性能验收驱动使用 Python 标准库；本机 SSH 服务器验收额外需要 Paramiko。

## 生成便携包和源码包

```powershell
.\tools\package_native.ps1
```

脚本调用 Qt 官方部署工具准备 `build/native-release/CoRig/`，复制文档、技能、许可证、三个公开示例及已有验证摘要，然后生成：

| 文件 | 用途 |
|---|---|
| `dist/CoRig-Windows-x64.zip` | 完整 Windows x64 便携程序，可直接解压运行 |
| `dist/CoRig-source.zip` | 对应项目源码、libssh2、测试、构建工具及文档 |

打包会重建自身的生成目录。便携包不包含 Python 运行时；源码包不包含本地凭据、私人项目、虚拟环境或构建缓存。

部署目录已经准备好，仅需刷新文档、示例、验证摘要与归档时，可使用：

```powershell
.\tools\package_native.ps1 -ArchiveOnly
```

## 部署验收

使用单独的数据目录运行真实部署程序，验证清除开发 Qt/Python 搜索路径后仍能启动、打开示例、处理数据并完成界面与本地 API 操作：

```powershell
python .\tools\native_release_check.py `
  --executable .\build\native-release\CoRig\CoRig.exe `
  --data-dir .\build\release-check-data `
  --output .\verification\corig-packaged
```

该驱动会启动实际窗口、软件回环及仅监听本机的 SSH 测试服务器；测试 Python 环境需安装 Paramiko。数据目录应为本次验收专用目录。详细覆盖和实机边界见[验证范围](VERIFICATION.md)。验收后再次使用 `-ArchiveOnly`，将最新摘要纳入最终归档。

## GitHub Release

正式仓库为 [xuyan623/CoRig](https://github.com/xuyan623/CoRig)，发布页为 [Releases](https://github.com/xuyan623/CoRig/releases)。发布标签应与 CMake 的项目版本及[版本说明](RELEASE_NOTES.md)一致。

发布时附上便携包、对应源码包和 `SHA256SUMS.txt`。GitHub 自动生成的 Source code 归档对应标签源码；额外提供的 `CoRig-source.zip` 方便离线查看和构建。
