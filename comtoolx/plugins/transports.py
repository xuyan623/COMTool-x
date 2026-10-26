"""Real serial, TCP/UDP and SSH IO nodes with independent RX and TX workers."""

from __future__ import annotations

import concurrent.futures
import queue
import select
import socket
import threading
import time

from ..sdk import Definition, NodeRuntime, Port, WorkbenchError, checked_properties

IO_PORTS = (Port("tx", "发送", "in", "bytes"), Port("rx", "接收", "out", "bytes"))


class Transport(NodeRuntime):
    """Own actual IO lifetimes; accepted writes and completed writes are distinct."""

    def __init__(self, node, engine):
        super().__init__(node, engine)
        self.resource = None
        self.reader = None
        self.writer = None
        self.tx_queue = queue.Queue(maxsize=4096)
        self.connection_lock = threading.RLock()
        self.want_connected = False
        self.state.update(connected=False, rx_bytes=0, tx_bytes=0, last_peer="")

    def _open(self):
        raise NotImplementedError("transport plugins must implement _open")

    def _read(self):
        raise NotImplementedError("transport plugins must implement _read")

    def _write(self, data):
        raise NotImplementedError("transport plugins must implement _write")

    def _close(self):
        resource, self.resource = self.resource, None
        if resource is not None:
            resource.close()

    def connect(self, changes=None):
        """Open a device on an action worker and reject a connection cancelled by stop."""
        with self.connection_lock:
            if self.state["connected"]:
                return self.snapshot()
            self.want_connected = False
            self._close()
            for worker in (self.reader, self.writer):
                if worker and worker is not threading.current_thread():
                    worker.join(timeout=1)
            definition = self.engine.registry.nodes[self.node["plugin"]]
            self.config = checked_properties(definition.properties, changes or {}, self.config)
            self._open()
            if self.stop_event.is_set() or not self.engine.running:
                self._close()
                raise WorkbenchError("连接已取消：功能图已停止")
            with self.lock:
                self.state.update(connected=True, error="")
            self.want_connected = True
            if not self.reader or not self.reader.is_alive():
                self.reader = threading.Thread(target=self._reader, daemon=True,
                                               name=f"rx-{self.id}")
                self.reader.start()
            if not self.writer or not self.writer.is_alive():
                self.writer = threading.Thread(target=self._writer, daemon=True,
                                               name=f"tx-{self.id}")
                self.writer.start()
        return self.snapshot()

    def _reader(self):
        """Read in batches; reconnect only when the plugin explicitly enables it."""
        while not self.stop_event.is_set() and self.want_connected:
            try:
                if not self.state["connected"]:
                    if not self.config.get("auto_reconnect") or not self.want_connected:
                        return
                    if self.stop_event.wait(float(self.config.get("reconnect_interval", 1))):
                        return
                    with self.connection_lock:
                        # Disconnect may have occurred while waiting for the lock.
                        if not self.want_connected or self.stop_event.is_set():
                            return
                        if self.state["connected"]:
                            continue
                        self._open()
                        if self.stop_event.is_set() or not self.want_connected:
                            self._close()
                            return
                        with self.lock:
                            self.state.update(connected=True, error="")
                data = self._read()
                if data:
                    with self.lock:
                        self.state["rx_bytes"] += len(data)
                    self.emit("rx", bytes(data))
            except Exception as exc:
                intended = self.want_connected and not self.stop_event.is_set()
                with self.lock:
                    self.state.update(connected=False, error=str(exc) if intended else "")
                try:
                    self._close()
                except Exception:
                    self.engine.record("error", f"{self.id} 关闭连接失败")
                if intended:
                    self.engine.record("error", f"{self.node['name']} 接收：{exc}")

    def _writer(self):
        """Serialize writes, report actual byte counts, and surface partial failures."""
        while not self.stop_event.is_set() and self.want_connected:
            try:
                data, completion = self.tx_queue.get(timeout=0.05)
            except queue.Empty:
                continue
            try:
                if not self.state["connected"]:
                    raise WorkbenchError("连接未打开，数据未发送")
                count = self._write(data)
                if count != len(data):
                    raise WorkbenchError(f"仅发送 {count}/{len(data)} 字节")
                with self.lock:
                    self.state["tx_bytes"] += count
                    self.state["last_tx_hex"] = data[-256:].hex(" ")
                    self.state["error"] = ""
                completion.set_result({"sent": count})
            except Exception as exc:
                with self.lock:
                    self.state["error"] = str(exc)
                completion.set_exception(exc)
                self.engine.record("error", f"{self.node['name']} 发送：{exc}")
            finally:
                self.tx_queue.task_done()
        # A stopped connection must not leave accepted operations waiting forever.
        while True:
            try:
                _, completion = self.tx_queue.get_nowait()
                completion.set_exception(WorkbenchError("功能图已停止，数据未发送"))
                self.tx_queue.task_done()
            except queue.Empty:
                break

    def receive(self, port, data):
        """Graph delivery enqueues TX without blocking the graph processing worker."""
        if not isinstance(data, bytes):
            raise WorkbenchError("通信发送端口需要 bytes")
        if not self.state["connected"]:
            raise WorkbenchError("连接未打开，数据未发送")
        completion = concurrent.futures.Future()
        try:
            self.tx_queue.put_nowait((data, completion))
        except queue.Full as exc:
            raise WorkbenchError("发送队列已满，数据未入队") from exc
        return completion

    def action(self, name, args):
        if name == "connect":
            return self.connect(args)
        if name == "disconnect":
            # Explicit disconnect disables reconnect until a later explicit connect.
            self.want_connected = False
            with self.connection_lock:
                with self.lock:
                    self.state.update(connected=False, error="")
                self._close()
                for worker in (self.reader, self.writer):
                    if worker and worker is not threading.current_thread():
                        worker.join(timeout=1)
            return self.snapshot()
        if name == "send":
            if "hex" in args:
                data = bytes.fromhex(args["hex"])
            else:
                data = str(args.get("text", "")).encode(args.get("encoding", "utf-8"))
            return self.receive("tx", data).result(timeout=10)
        if name == "clear_counts":
            with self.lock:
                self.state.update(rx_bytes=0, tx_bytes=0)
            return self.snapshot()
        return super().action(name, args)

    def stop(self):
        self.want_connected = False
        self.stop_event.set()
        with self.lock:
            self.state.update(connected=False, active=False)
        try:
            self._close()
        finally:
            for worker in (self.reader, self.writer):
                if worker and worker is not threading.current_thread():
                    worker.join(timeout=1)


class Serial(Transport):
    def _open(self):
        import serial
        cfg = self.config
        if not cfg["port"]:
            raise WorkbenchError("请选择串口")
        flow = cfg["flow_control"]
        candidate = serial.serial_for_url(
            cfg["port"], baudrate=cfg["baudrate"], bytesize=cfg["bytesize"],
            parity=cfg["parity"], stopbits=cfg["stopbits"], timeout=0.02,
            write_timeout=1, rtscts=flow == "RTS/CTS", xonxoff=flow == "XON/XOFF",
            dsrdtr=flow == "DSR/DTR")
        try:
            candidate.rts, candidate.dtr = cfg["rts"], cfg["dtr"]
        except (OSError, ValueError):
            # URL transports such as loop:// may not implement modem control.
            if "://" not in cfg["port"]:
                candidate.close()
                raise
        self.resource = candidate
        with self.lock:
            self.state["port"] = cfg["port"]

    def _read(self):
        resource = self.resource
        return resource.read(max(1, min(resource.in_waiting, 65536)))

    def _write(self, data):
        return self.resource.write(data)

    def action(self, name, args):
        if name == "list_ports":
            from serial.tools import list_ports
            return [{"port": port.device, "description": port.description,
                     "hwid": port.hwid} for port in list_ports.comports()]
        if name in ("rts", "dtr"):
            if not self.state["connected"]:
                raise WorkbenchError("请先打开串口")
            value = bool(args["value"])
            setattr(self.resource, name, value)
            return {name: value}
        return super().action(name, args)


class Network(Transport):
    def __init__(self, node, engine):
        super().__init__(node, engine)
        self.clients = {}
        self.peer = None

    def _open(self):
        cfg = self.config
        kind = socket.SOCK_DGRAM if cfg["protocol"] == "udp" else socket.SOCK_STREAM
        candidate = socket.socket(socket.AF_INET, kind)
        candidate.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        candidate.settimeout(3)
        try:
            if cfg["mode"] == "server":
                candidate.bind((cfg["host"], cfg["port"]))
                if kind == socket.SOCK_STREAM:
                    candidate.listen(16)
            elif kind == socket.SOCK_STREAM:
                candidate.connect((cfg["host"], cfg["port"]))
            else:
                candidate.bind(("", cfg["bind_port"]))
            candidate.settimeout(0.05)
        except Exception:
            candidate.close()
            raise
        self.resource = candidate
        self.peer = (cfg["host"], cfg["port"]) if cfg["mode"] == "client" else None
        with self.lock:
            self.state["local_address"] = list(candidate.getsockname())

    def _read(self):
        resource = self.resource
        cfg = self.config
        if cfg["protocol"] == "udp":
            try:
                data, peer = resource.recvfrom(65535)
                self.peer = peer
                with self.lock:
                    self.state["last_peer"] = f"{peer[0]}:{peer[1]}"
                return data
            except socket.timeout:
                return b""
        if cfg["mode"] == "client":
            try:
                data = resource.recv(65536)
            except socket.timeout:
                return b""
            if not data:
                raise WorkbenchError("对端已关闭 TCP 连接")
            return data
        with self.lock:
            clients = list(self.clients)
        ready, _, _ = select.select([resource, *clients], [], [], 0.05)
        for sock in ready:
            if sock is resource:
                client, address = resource.accept()
                client.settimeout(1)
                with self.lock:
                    self.clients[client] = address
                    self.state["clients"] = [f"{p[0]}:{p[1]}" for p in self.clients.values()]
            else:
                try:
                    data = sock.recv(65536)
                except OSError:
                    data = b""
                if data:
                    with self.lock:
                        peer = self.clients[sock]
                        self.state["last_peer"] = f"{peer[0]}:{peer[1]}"
                    return data
                with self.lock:
                    self.clients.pop(sock, None)
                    self.state["clients"] = [f"{p[0]}:{p[1]}" for p in self.clients.values()]
                sock.close()
        return b""

    def _write(self, data):
        cfg = self.config
        if cfg["protocol"] == "udp":
            if self.peer is None:
                raise WorkbenchError("UDP 服务端尚未收到对端地址")
            return self.resource.sendto(data, self.peer)
        if cfg["mode"] == "client":
            self.resource.sendall(data)
        else:
            with self.lock:
                clients = list(self.clients.items())
            selected = cfg["target_client"]
            clients = [(sock, peer) for sock, peer in clients
                       if not selected or selected == f"{peer[0]}:{peer[1]}"]
            if not clients:
                raise WorkbenchError("没有已连接的 TCP 客户端")
            for sock, _ in clients:
                sock.sendall(data)
        return len(data)

    def _close(self):
        with self.lock:
            clients, self.clients = list(self.clients), {}
            self.state["clients"] = []
        for client in clients:
            client.close()
        super()._close()


class SSH(Transport):
    def __init__(self, node, engine):
        super().__init__(node, engine)
        self.client = None

    def _open(self):
        import paramiko
        cfg = self.config
        candidate = paramiko.SSHClient()
        candidate.load_system_host_keys()
        if cfg["allow_unknown_host"]:
            candidate.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        try:
            candidate.connect(cfg["host"], port=cfg["port"], username=cfg["user"],
                              password=cfg["password"] or None,
                              key_filename=cfg["key_file"] or None, timeout=4,
                              banner_timeout=4, auth_timeout=4)
            channel = candidate.invoke_shell(term="xterm-256color", width=80, height=24)
            channel.settimeout(0.05)
        except Exception:
            candidate.close()
            raise
        self.client, self.resource = candidate, channel

    def _read(self):
        try:
            data = self.resource.recv(65536)
        except socket.timeout:
            return b""
        if not data:
            raise WorkbenchError("SSH 会话已关闭")
        return data

    def _write(self, data):
        self.resource.sendall(data)
        return len(data)

    def receive(self, port, data):
        if port == "resize":
            if self.resource is not None:
                self.resource.resize_pty(width=int(data["columns"]), height=int(data["rows"]))
            return None
        return super().receive(port, data)

    def _close(self):
        super()._close()
        if self.client is not None:
            self.client.close()
            self.client = None


def transport_view(runtime, request, properties):
    """Connection controls belong to the transport plugin, including port discovery."""
    from PySide6.QtCore import QTimer
    from PySide6.QtWidgets import (QWidget, QFormLayout, QComboBox, QLineEdit,
                                  QSpinBox, QPushButton, QLabel, QHBoxLayout, QVBoxLayout)
    widget = QWidget()
    layout = QVBoxLayout(widget)
    form = QFormLayout()
    layout.addLayout(form)
    editors = {}
    if isinstance(runtime, Serial):
        fields = ("port", "baudrate", "bytesize", "parity", "stopbits", "flow_control")
    elif isinstance(runtime, Network):
        fields = ("protocol", "mode", "host", "port")
    else:
        fields = ("host", "port", "user", "password", "key_file")
    labels = {"port": "串口" if isinstance(runtime, Serial) else "端口",
              "baudrate": "波特率", "bytesize": "数据位", "parity": "校验",
              "stopbits": "停止位", "flow_control": "流控", "protocol": "协议",
              "mode": "模式", "host": "地址", "user": "用户", "password": "密码",
              "key_file": "密钥文件"}
    definition = runtime.engine.registry.nodes[runtime.node["plugin"]]
    for key in fields:
        spec = definition.properties[key]
        value = runtime.config[key]
        if key == "port" and isinstance(runtime, Serial):
            editor = QComboBox()
            editor.setEditable(True)
            from serial.tools import list_ports
            editor.addItems([p.device for p in list_ports.comports()])
            editor.setCurrentText(value)
        elif "choices" in spec:
            editor = QComboBox()
            for choice in spec["choices"]:
                editor.addItem(str(choice), choice)
            editor.setCurrentIndex(max(0, editor.findData(value)))
        elif spec["type"] == "integer":
            editor = QSpinBox()
            editor.setRange(spec.get("min", 0), spec.get("max", 2147483647))
            editor.setValue(value)
        else:
            editor = QLineEdit(str(value))
            if key == "password":
                editor.setEchoMode(QLineEdit.EchoMode.Password)
        editor.setObjectName(f"{runtime.id}.connection.{key}")
        form.addRow(labels[key], editor)
        editors[key] = editor
    row = QHBoxLayout()
    connect = QPushButton("连接")
    connect.setObjectName(f"{runtime.id}.connect")
    disconnect = QPushButton("断开")
    disconnect.setObjectName(f"{runtime.id}.disconnect")
    row.addWidget(connect)
    row.addWidget(disconnect)
    layout.addLayout(row)
    status = QLabel("未连接")
    status.setWordWrap(True)
    status.setObjectName(f"{runtime.id}.status")
    layout.addWidget(status)
    counts = QLabel("接收 0 B　发送 0 B")
    layout.addWidget(counts)
    layout.addStretch()
    def connect_requested():
        changes = {}
        for key, editor in editors.items():
            if isinstance(editor, QSpinBox):
                changes[key] = editor.value()
            elif isinstance(editor, QComboBox):
                changes[key] = editor.currentData() if editor.currentData() is not None else editor.currentText()
            else:
                changes[key] = editor.text()
        request("connect", changes)
    connect.clicked.connect(connect_requested)
    disconnect.clicked.connect(lambda: request("disconnect", {}))
    def refresh():
        state = runtime.snapshot()
        connected = state["connected"]
        status.setText(state.get("error") or ("已连接" if connected else "未连接"))
        counts.setText(f"接收 {state['rx_bytes']:,} B　发送 {state['tx_bytes']:,} B")
        connect.setEnabled(not connected)
        disconnect.setEnabled(connected)
        for editor in editors.values():
            editor.setEnabled(not connected)
    timer = QTimer(widget)
    timer.timeout.connect(refresh)
    timer.start(200)
    refresh()
    widget.refresh_from_runtime = refresh
    return widget


def register(registry):
    actions = {"connect": {"description": "打开连接，可传入本次连接参数"},
               "disconnect": {"description": "断开连接并停止自动重连"},
               "send": {"description": "发送 text/encoding 或 hex，任务完成后返回实际发送字节数"},
               "clear_counts": {"description": "清空收发计数"}}
    common = {"auto_reconnect": {"type": "boolean", "default": False},
              "reconnect_interval": {"type": "number", "default": 1.0, "min": 0.1, "max": 300}}
    registry.register(Definition("io.serial", "串口", "通信", "串口收发、流控、RTS/DTR、断线重连",
        IO_PORTS, {
            "port": {"type": "string", "default": ""},
            "baudrate": {"type": "integer", "default": 115200, "min": 50, "max": 12000000},
            "bytesize": {"type": "integer", "default": 8, "choices": [5, 6, 7, 8]},
            "parity": {"type": "string", "default": "N", "choices": ["N", "E", "O", "M", "S"]},
            "stopbits": {"type": "number", "default": 1, "choices": [1, 1.5, 2]},
            "flow_control": {"type": "string", "default": "None",
                             "choices": ["None", "RTS/CTS", "XON/XOFF", "DSR/DTR"]},
            "rts": {"type": "boolean", "default": False},
            "dtr": {"type": "boolean", "default": False}, **common
        }, Serial, {"connection": transport_view},
        {**actions, "list_ports": {"description": "枚举实际串口", "requires_running": False},
         "rts": {"description": "设置 RTS", "args": {"value": "boolean"}},
         "dtr": {"description": "设置 DTR", "args": {"value": "boolean"}}}, breaks_cycle=True))
    registry.register(Definition("io.network", "TCP / UDP", "通信", "TCP 客户端/多客户端服务端、UDP 收发",
        IO_PORTS, {
            "protocol": {"type": "string", "default": "tcp", "choices": ["tcp", "udp"]},
            "mode": {"type": "string", "default": "client", "choices": ["client", "server"]},
            "host": {"type": "string", "default": "127.0.0.1"},
            "port": {"type": "integer", "default": 2345, "min": 1, "max": 65535},
            "bind_port": {"type": "integer", "default": 0, "min": 0, "max": 65535},
            "target_client": {"type": "string", "default": ""}, **common
        }, Network, {"connection": transport_view}, actions, breaks_cycle=True))
    registry.register(Definition("io.ssh", "SSH", "通信", "密码/密钥登录、交互式终端、窗口尺寸传递",
        (*IO_PORTS, Port("resize", "终端尺寸", "in", "object")), {
            "host": {"type": "string", "default": "127.0.0.1"},
            "port": {"type": "integer", "default": 22, "min": 1, "max": 65535},
            "user": {"type": "string", "default": "root"},
            "password": {"type": "string", "default": "", "secret": True},
            "key_file": {"type": "string", "default": "", "format": "file"},
            "allow_unknown_host": {"type": "boolean", "default": False}, **common
        }, SSH, {"connection": transport_view}, actions, breaks_cycle=True))
