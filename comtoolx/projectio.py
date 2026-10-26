"""Portable .comtoolx bundles: exact plugin sources, design data and input resources.

The installed SDK supplies shared dependencies. Each opened bundle has its own
temporary files and Python namespaces, released when its project is replaced.
"""

from __future__ import annotations

import copy
import importlib.util
import io
import json
import os
import sys
import tempfile
import types
import uuid
import zipfile
from pathlib import Path, PurePosixPath

from .model import load_project, validate
from .sdk import Registry, WorkbenchError

BUNDLE_FORMAT = "comtool-x-bundle"
BUNDLE_SCHEMA = 1


def bundle_bytes(project, registry, base_dir, input_files=None):
    """Snapshot only providers used by nodes/wires/layouts; preserve their helpers and resources."""
    used = {"nodes": {node["plugin"] for node in project["nodes"]},
            "wires": {wire["plugin"] for wire in project["wires"]},
            "containers": {item["plugin"] for item in project["ui"]["items"] if item.get("plugin")}}
    selected = {}
    for kind, ids in used.items():
        for ident in sorted(ids):
            owner = registry.owners.get((kind, ident))
            if owner is None:
                raise WorkbenchError(f"插件缺失或没有可打包的实现：{ident}")
            selected.setdefault(owner, {key: [] for key in used})[kind].append(ident)
    manifest = {"format": BUNDLE_FORMAT, "schema_version": BUNDLE_SCHEMA,
                "providers": [], "resources": []}
    payload = {"project.json": json.dumps(project, ensure_ascii=False, indent=2, allow_nan=False).encode("utf-8")}
    for index, (owner, definitions) in enumerate(selected.items()):
        provider = registry.providers[owner]
        folder = "plugins/builtin" if provider["builtin"] else f"plugins/external_{index}"
        for relative, data in provider["files"].items():
            name = folder + "/" + relative
            if name in payload and payload[name] != data:
                raise WorkbenchError("工程中的内置插件来自不一致的实现版本，请重新加载一致的插件")
            payload[name] = data
        manifest["providers"].append({"entry": folder + "/" + provider["entry"],
            "builtin": provider["builtin"], "definitions": definitions})
    for node in project["nodes"]:
        definition = registry.nodes[node["plugin"]]
        for key, schema in definition.properties.items():
            value = node.get("config", {}).get(key, schema.get("default"))
            if schema.get("format") != "file" or not value:
                continue
            resource = (input_files or {}).get((node["id"], key))
            source = Path(resource["path"] if resource and value == resource["value"] else value)
            if not source.is_absolute():
                source = Path(base_dir) / source
            if not source.is_file():
                raise WorkbenchError(f"工程资源不存在：{node['name']}.{key}：{source}")
            relative = f"resources/{len(manifest['resources'])}/{source.name}"
            payload[relative] = source.read_bytes()
            manifest["resources"].append({"node": node["id"], "property": key, "path": relative})
    payload["manifest.json"] = json.dumps(manifest, ensure_ascii=False, indent=2).encode("utf-8")
    if len(payload) > 4096 or sum(len(data) for data in payload.values()) > 256 * 1024 * 1024:
        raise WorkbenchError("工程文件过多或资源超过 256 MiB")
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as archive:
        for name, data in payload.items():
            archive.writestr(name, data)
    return output.getvalue()


def save_bundle(project, registry, path, base_dir, input_files=None):
    """Write atomically only after the complete portable snapshot has been assembled."""
    target = Path(path).resolve()
    if target.suffix.lower() != ".comtoolx":
        raise WorkbenchError("工程必须保存为 .comtoolx 文件")
    payload = bundle_bytes(project, registry, base_dir, input_files)
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=target.parent, prefix=target.name + ".", suffix=".tmp", delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, target)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return str(target)


class ProjectContents:
    """Own all extracted resources and imported modules for one opened project."""

    def __init__(self, baseline):
        self.temporary = tempfile.TemporaryDirectory(prefix="comtoolx-project-")
        self.root = Path(self.temporary.name)
        self.registry = baseline.clone()
        self.project = None
        self.input_files = {}
        self.namespaces = set()

    def close(self):
        """Remove only this project's module names and temporary files after runtimes stop."""
        for name in list(sys.modules):
            if any(name == root or name.startswith(root + ".") for root in self.namespaces):
                del sys.modules[name]
        self.temporary.cleanup()


def open_bundle(path, baseline):
    """Stage a complete project in isolation; a failed open leaves the active project intact."""
    if Path(path).suffix.lower() != ".comtoolx" or not zipfile.is_zipfile(path):
        raise WorkbenchError("请选择 .comtoolx 工程包；不支持 JSON 工程")
    contents = ProjectContents(baseline)
    try:
        with zipfile.ZipFile(path) as archive:
            entries = archive.infolist()
            if len(entries) > 4096 or sum(item.file_size for item in entries) > 256 * 1024 * 1024:
                raise WorkbenchError("工程包文件过多或解压后超过 256 MiB")
            names = [item.filename for item in entries]
            if len(names) != len({name.casefold() for name in names}):
                raise WorkbenchError("工程包包含重复文件")
            for item in entries:
                relative = PurePosixPath(item.filename)
                if (not item.filename or "\\" in item.filename or relative.is_absolute()
                        or ".." in relative.parts or any(":" in part for part in relative.parts)
                        or not (contents.root / relative).resolve().is_relative_to(contents.root)):
                    raise WorkbenchError("工程包包含不安全的文件路径")
                destination = contents.root.joinpath(*relative.parts)
                if item.is_dir():
                    destination.mkdir(parents=True, exist_ok=True)
                else:
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    destination.write_bytes(archive.read(item))
        manifest = json.loads((contents.root / "manifest.json").read_text(encoding="utf-8"))
        if manifest.get("format") != BUNDLE_FORMAT or manifest.get("schema_version") != BUNDLE_SCHEMA:
            raise WorkbenchError("不支持的 COMTool-x 工程包版本")
        contents.project = load_project(path)
        namespace_id = uuid.uuid4().hex
        assigned = set()
        for index, provider in enumerate(manifest["providers"]):
            entry = (contents.root / provider["entry"]).resolve(strict=True)
            if not entry.is_relative_to(contents.root) or entry.suffix != ".py":
                raise WorkbenchError("工程包插件入口无效")
            namespace = f"comtoolx._project_{namespace_id}_" + ("builtin" if provider["builtin"] else str(index))
            if namespace not in contents.namespaces:
                package = types.ModuleType(namespace)
                package.__path__ = [str(entry.parent)]
                sys.modules[namespace] = package
                contents.namespaces.add(namespace)
            module_name = namespace + "." + entry.stem
            module = sys.modules.get(module_name)
            if module is None:
                spec = importlib.util.spec_from_file_location(module_name, entry)
                module = importlib.util.module_from_spec(spec)
                sys.modules[module_name] = module
                spec.loader.exec_module(module)
            staged = Registry()
            owner = staged.register_provider(module, entry, builtin=provider["builtin"])
            snapshot = staged.providers[owner]
            snapshot["definitions"] = copy.deepcopy(provider["definitions"])
            for kind in ("nodes", "wires", "containers"):
                for ident in provider["definitions"][kind]:
                    if (kind, ident) in assigned:
                        raise WorkbenchError(f"工程包包含重复的插件类型：{ident}")
                    assigned.add((kind, ident))
                    definition = getattr(staged, kind).get(ident)
                    if definition is None:
                        raise WorkbenchError(f"工程包插件没有实现声明的类型：{ident}")
                    old_owner = baseline.owners.get((kind, ident))
                    if old_owner and not provider["builtin"]:
                        raise WorkbenchError(f"工程插件不能覆盖内置类型：{ident}")
                    getattr(contents.registry, kind)[ident] = definition
                    contents.registry.owners[kind, ident] = owner
            contents.registry.providers[owner] = snapshot
            if not provider["builtin"]:
                contents.registry.external.append(str(entry))
        for resource in manifest["resources"]:
            file = (contents.root / resource["path"]).resolve(strict=True)
            if not file.is_relative_to(contents.root) or not file.is_file():
                raise WorkbenchError("工程资源路径无效")
            node = next(node for node in contents.project["nodes"] if node["id"] == resource["node"])
            definition = contents.registry.nodes[node["plugin"]]
            key = resource["property"]
            if definition.properties.get(key, {}).get("format") != "file":
                raise WorkbenchError("工程资源必须绑定文件输入属性")
            contents.input_files[node["id"], key] = {
                "value": node.get("config", {}).get(key, definition.properties[key].get("default")), "path": str(file)}
        issues = validate(contents.project, contents.registry)
        missing = [issue["message"] for issue in issues if issue["code"] in ("missing_plugin", "missing_wire", "ui_plugin")]
        if missing:
            raise WorkbenchError("; ".join(missing))
        return contents
    except Exception:
        contents.close()
        raise
