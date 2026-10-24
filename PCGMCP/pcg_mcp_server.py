# -*- coding: utf-8 -*-
"""
PCG MCP Server — UE 5.6 程序化生态的 MCP 工具集
================================================

架构（UE 侧零改动）:

    AI agent (任意 MCP 客户端)
       ↕  MCP stdio
    pcg_mcp_server.py  ← 本文件（纯 Python 适配层，编辑器外进程）
       ↕  TCP 一次性连接: {"code": "<python>"} → {"result": "<stdout>"}
    UE init_unreal.py 桥 (端口 9877, 编辑器启动时自动开启)

内置生产纪律（来自 tutor 项目踩坑经验）:
  - 每个 UE 载荷: 全 try/except 不抛出 / 关键 print 在 risky 调用前 / 收尾 gc.collect()
  - regen 只提交一次 generate 请求，之后适配层轮询实例数稳定（避免多轮叠加死循环）
  - set_editor_property 读写参数（绕开 UE5.6 set_float_parameter 读 0 值 bug）
  - 修改资产前先 backup_asset；save 前解除只读并核对磁盘 mtime（Perforce 教训）
  - 一次调用只加载一张 PCG 图（批量加载会内存溢出崩溃）
  - 只连 9877，绝不回落 9876（会执行进错误编辑器）

配置: 环境变量 PCG_BRIDGE_HOST / PCG_BRIDGE_PORT（默认 127.0.0.1 / 9877）
用法: 由 MCP 客户端以 stdio 方式拉起，或手动 `python pcg_mcp_server.py`
"""

import json
import os
import socket
import time
from typing import Any, Optional

from mcp.server.fastmcp import FastMCP

# --------------------------------------------------------------------------
# 桥客户端
# --------------------------------------------------------------------------

BRIDGE_HOST = os.environ.get("PCG_BRIDGE_HOST", "127.0.0.1")
BRIDGE_PORT = int(os.environ.get("PCG_BRIDGE_PORT", "9877"))  # 只用 9877，绝不回落 9876


class BridgeError(RuntimeError):
    pass


def bridge_call(code: str, read_timeout: float = 300.0) -> str:
    """一次性 TCP 连接: 发 {"code": ...}, 收 {"result": "..."} / {"message": "..."}."""
    s = socket.create_connection((BRIDGE_HOST, BRIDGE_PORT), timeout=10)
    try:
        s.settimeout(read_timeout)
        s.sendall(json.dumps({"code": code}).encode("utf-8"))
        buf = b""
        while True:
            d = s.recv(65536)
            if not d:
                break
            buf += d
    finally:
        s.close()
    r = json.loads(buf.decode("utf-8", "replace"))
    if "result" in r and r["result"] is not None:
        return str(r["result"])
    raise BridgeError(str(r.get("message", "bridge returned empty response")))


def ue_payload(body: str, params: Optional[dict] = None, prelude: str = "import unreal") -> str:
    """把桥纪律包进载荷: P 参数注入 + 全 try/except + gc 收尾.
    注意: P 用 repr() 而非 json.dumps —— json 的 null/true/false 不是合法 Python 字面量;
    traceback 必须打到 stdout —— 桥只回传 stdout, 走 stderr 的异常输出会被丢弃. """
    p = repr(params or {})
    tpl = (
        "{prelude}\n"
        "import gc, sys, traceback\n"
        "P = {params}\n"
        "try:\n"
        "{body}\n"
        "except Exception:\n"
        "    traceback.print_exc(file=sys.stdout)\n"
        "gc.collect()\n"
    )
    # body 每行缩进 4 空格（进 try 块）
    body_indented = "\n".join(("    " + line) if line.strip() else line for line in body.strip("\n").split("\n"))
    return tpl.format(prelude=prelude, params=p, body=body_indented)


def call_payload(body: str, params: Optional[dict] = None, read_timeout: float = 300.0) -> str:
    return bridge_call(ue_payload(body, params), read_timeout=read_timeout)


# --------------------------------------------------------------------------
# MCP 服务
# --------------------------------------------------------------------------

mcp = FastMCP(
    "PCG Bridge (UE 5.6)",
    instructions=(
        "UE 5.6 PCG 程序化生态工具集。通过 9877 TCP 桥驱动编辑器。"
        "工作纪律: 改图前先 backup_asset；改动后 set→save→regen_volume→diff_instances 验收；"
        "PCGVolume 的 actor_name 必须用 list_pcg_volumes 返回的精确名（UAID 全名）。"
    ),
)

# A/B 快照存于本进程内存（标签 → {mesh: count, ...}）
_SNAPSHOTS: dict[str, dict] = {}


# ============================ 只读诊断 ============================

@mcp.tool()
def ping() -> str:
    """探测桥与编辑器是否存活。返回当前地图名与 PCGVolume 数量。"""
    body = r"""
w = unreal.EditorLevelLibrary.get_editor_world()
print('map=%s' % w.get_name())
try:
    actors = unreal.EditorLevelLibrary.get_all_level_actors()
    n = sum(1 for a in actors if 'PCGVolume' in a.get_class().get_name())
    print('pcg_volumes=%d' % n)
except Exception:
    traceback.print_exc()
"""
    return call_payload(body, read_timeout=30)


@mcp.tool()
def list_pcg_volumes() -> str:
    """列出当前关卡全部 PCGVolume：精确 actor 名（后续工具都用它）、图资产路径、
    触发模式、生成状态、实例总数。只读。"""
    body = r"""
def _safe(fn, default='?'):
    try:
        return fn()
    except Exception:
        return default

def _graph_of(comp):
    # PCGComponent.graph 受保护不可直读; 先走 graph_instance.graph, 再兜底
    gi = _safe(lambda: comp.get_editor_property('graph_instance'), None)
    if gi:
        g = _safe(lambda: gi.get_editor_property('graph'), None)
        if g:
            return g.get_path_name()
    g = _safe(lambda: comp.get_editor_property('graph'), None)
    return g.get_path_name() if g else '(no graph / protected)'

actors = unreal.EditorLevelLibrary.get_all_level_actors()
rows = []
for a in actors:
    cls = a.get_class().get_name()
    if 'PCGVolume' not in cls:
        continue
    comp = a.get_component_by_class(unreal.PCGComponent)
    gen = regen = '?'
    inst = 0
    if comp:
        gen = _safe(lambda: str(comp.get_editor_property('generated')), '?')
        regen = _safe(lambda: str(comp.get_editor_property('regenerate_in_editor')), '?')
        try:
            for c in a.get_components_by_class(unreal.InstancedStaticMeshComponent):
                inst += c.get_instance_count()
        except Exception:
            pass
    rows.append('%s | class=%s | graph=%s | generated=%s | regen_in_editor=%s | instances=%d'
                % (a.get_name(), cls, _graph_of(comp) if comp else '?', gen, regen, inst))
print('\n'.join(rows) if rows else 'NO_PCG_VOLUMES')
"""
    return call_payload(body, read_timeout=60)


@mcp.tool()
def dump_graph(graph_path: str) -> str:
    """加载一张 PCG 图资产并 dump 拓扑：每节点一行（名称/类型/enabled/引脚连线数）。
    这是图审计的基础。一次只加载一张图（内存纪律）。只读。"""
    body = r"""
g = unreal.EditorAssetLibrary.load_asset(P['graph_path'])
if not g:
    print('LOAD_FAIL %s' % P['graph_path'])
else:
    print('graph=%s nodes=%d' % (P['graph_path'], len(g.nodes)))
    for n in g.nodes:
        try:
            enabled = str(n.get_settings().get_editor_property('enabled'))
        except Exception:
            enabled = '?'
        pin_desc = []
        try:
            for pin in n.input_pins:
                try:
                    lbl = pin.get_editor_property('properties').label
                except Exception:
                    lbl = '?'
                try:
                    ne = len(pin.get_editor_property('edges'))
                except Exception:
                    ne = 0
                pin_desc.append('in:%s:%d' % (lbl, ne))
        except Exception:
            pin_desc.append('in:UNREADABLE')
        try:
            for pin in n.output_pins:
                try:
                    lbl = pin.get_editor_property('properties').label
                except Exception:
                    lbl = '?'
                try:
                    ne = len(pin.get_editor_property('edges'))
                except Exception:
                    ne = 0
                pin_desc.append('out:%s:%d' % (lbl, ne))
        except Exception:
            pin_desc.append('out:UNREADABLE')
        print('%s | %s | enabled=%s | %s'
              % (n.get_name(), n.get_class().get_name(), enabled, ' '.join(pin_desc)))
"""
    return call_payload(body, {"graph_path": graph_path}, read_timeout=120)


@mcp.tool()
def get_node_settings(graph_path: str, node_name: str, properties: Optional[list[str]] = None) -> str:
    """定向读取一个节点的设置属性。properties 是要读的 editor property 名列表；
    不传则只返回节点类型与设置类名。每个属性独立 try/except，读不到会标注 MISSING。
    只读。"""
    body = r"""
g = unreal.EditorAssetLibrary.load_asset(P['graph_path'])
if not g:
    print('LOAD_FAIL %s' % P['graph_path'])
else:
    node = None
    for n in g.nodes:
        if n.get_name() == P['node_name']:
            node = n
            break
    if node is None:
        print('NODE_NOT_FOUND %s' % P['node_name'])
    else:
        s = node.get_settings()
        print('node=%s | node_class=%s | settings_class=%s'
              % (P['node_name'], node.get_class().get_name(), s.get_class().get_name()))
        for prop in (P.get('properties') or []):
            try:
                print('%s = %s' % (prop, s.get_editor_property(prop)))
            except Exception as e:
                print('%s MISSING (%s)' % (prop, e))
"""
    return call_payload(body, {"graph_path": graph_path, "node_name": node_name,
                               "properties": properties or []}, read_timeout=120)


@mcp.tool()
def get_volume_status(actor_name: str) -> str:
    """读取一个 PCGVolume 的生成状态与逐网格实例数。regen_volume 的轮询也用它。只读。"""
    body = r"""
def _find(name):
    for a in unreal.EditorLevelLibrary.get_all_level_actors():
        if 'PCGVolume' in a.get_class().get_name() and a.get_name() == name:
            return a
    return None

a = _find(P['actor_name'])
if a is None:
    print('VOLUME_NOT_FOUND %s' % P['actor_name'])
else:
    comp = a.get_component_by_class(unreal.PCGComponent)
    for prop in ('generated', 'regenerate_in_editor'):
        try:
            print('%s=%s' % (prop, comp.get_editor_property(prop)))
        except Exception:
            print('%s=UNREADABLE' % prop)
    total = 0
    try:
        for c in a.get_components_by_class(unreal.InstancedStaticMeshComponent):
            m = c.static_mesh
            nm = m.get_name() if m else 'None'
            cnt = c.get_instance_count()
            print('mesh %s = %d' % (nm, cnt))
            total += cnt
    except Exception:
        traceback.print_exc(file=sys.stdout)
    print('TOTAL_INSTANCES=%d' % total)
"""
    return call_payload(body, {"actor_name": actor_name}, read_timeout=60)


# ============================ 安全层 ============================

@mcp.tool()
def backup_asset(asset_path: str, suffix: Optional[str] = None) -> str:
    """修改资产前的强制纪律：duplicate_asset 生成带时间戳后缀的备份，返回备份路径。
    之后所有改动应发生在原资产或备份副本上，可随时回滚。"""
    body = r"""
import time
src = P['asset_path']
sfx = P.get('suffix') or ('MCP_Backup_' + time.strftime('%Y%m%d_%H%M%S'))
folder, base = src.rsplit('/', 1)
base = base.split('.')[0]
dst = folder + '/' + base + '_' + sfx
out = unreal.EditorAssetLibrary.duplicate_asset(src, dst)
if out:
    print('BACKUP_OK %s' % out.get_path_name())
else:
    print('BACKUP_FAIL %s' % dst)
"""
    return call_payload(body, {"asset_path": asset_path, "suffix": suffix}, read_timeout=120)


@mcp.tool()
def save_asset(asset_path: str) -> str:
    """保存资产到磁盘。内置 Perforce 只读处理: 解除文件只读位 → 保存 → 核对磁盘 mtime。
    mtime 未变化会明确报告 SILENT_SAVE_FAIL（Perforce 只读保存静默失败的教训）。"""
    body = r"""
import os, stat
src = P['asset_path']
asset = unreal.EditorAssetLibrary.load_asset(src)
if not asset:
    print('LOAD_FAIL %s' % src)
else:
    # 解除 uasset 只读（Perforce checkout 前的 attrib -r 等价）
    pkg = asset.get_path_name().split('.')[0]
    rel = pkg.replace('/Game/', '') + '.uasset'
    fpath = unreal.Paths.convert_relative_path_to_full(
        os.path.join(unreal.Paths.project_content_dir(), rel.replace('/', '\\')))
    try:
        if os.path.exists(fpath):
            os.chmod(fpath, stat.S_IWRITE)
            print('readonly_cleared %s' % fpath)
    except Exception:
        traceback.print_exc(file=sys.stdout)
    m0 = os.path.getmtime(fpath) if os.path.exists(fpath) else 0
    asset.modify()
    ok = unreal.EditorAssetLibrary.save_loaded_asset(asset, False)
    m1 = os.path.getmtime(fpath) if os.path.exists(fpath) else 0
    print('save_api=%s | mtime_changed=%s' % (ok, m1 != m0))
    print('SAVE_OK' if (ok and m1 != m0) else 'SILENT_SAVE_FAIL %s' % fpath)
"""
    return call_payload(body, {"asset_path": asset_path}, read_timeout=120)


# ============================ 写入 ============================

@mcp.tool()
def set_node_property(graph_path: str, node_name: str, property_name: str, value: Any) -> str:
    """设置 PCG 节点的 editor property（内部用 set_editor_property，绕开 UE5.6
    set_float_parameter 写 0 值 bug）。写后立即读回验证（write-verify）。
    注意: 改的是图资产；volume 上的 graph_instance 不会自动传播——
    改完调用 regen_volume 才能在场景看到效果。"""
    body = r"""
g = unreal.EditorAssetLibrary.load_asset(P['graph_path'])
if not g:
    print('LOAD_FAIL %s' % P['graph_path'])
else:
    node = None
    for n in g.nodes:
        if n.get_name() == P['node_name']:
            node = n
            break
    if node is None:
        print('NODE_NOT_FOUND %s' % P['node_name'])
    else:
        s = node.get_settings()
        print('before: %s' % s.get_editor_property(P['property_name']))
        s.set_editor_property(P['property_name'], P['value'])
        print('after:  %s' % s.get_editor_property(P['property_name']))
        print('SET_OK (remember: save_asset then regen_volume)')
"""
    return call_payload(body, {"graph_path": graph_path, "node_name": node_name,
                               "property_name": property_name, "value": value}, read_timeout=120)


@mcp.tool()
def regen_volume(actor_name: str, timeout_s: float = 240.0, poll_interval_s: float = 3.0,
                 stable_polls: int = 2) -> str:
    """对 volume 提交一次 generate(force=True)，然后在本进程内轮询实例数，
    直到连续 stable_polls 次读数相同（PCG 生成是异步的——立即返回时读到 0 是误诊）。
    纪律: 只提交一次生成请求，绝不叠加（多轮叠加会单核满载死循环）。"""
    body = r"""
def _find(name):
    for a in unreal.EditorLevelLibrary.get_all_level_actors():
        if 'PCGVolume' in a.get_class().get_name() and a.get_name() == name:
            return a
    return None

a = _find(P['actor_name'])
if a is None:
    print('VOLUME_NOT_FOUND %s' % P['actor_name'])
else:
    comp = a.get_component_by_class(unreal.PCGComponent)
    print('submitting generate(force=True)')
    comp.generate(force=True)
    print('SUBMITTED')
"""
    submit = call_payload(body, {"actor_name": actor_name}, read_timeout=60)
    if "SUBMITTED" not in submit:
        return submit + "\n(未提交生成请求，不进入轮询)"

    # 适配层轮询: 实例数连续 N 次稳定即认为完成
    t0 = time.time()
    history: list[int] = []
    last_status = ""
    while time.time() - t0 < timeout_s:
        time.sleep(poll_interval_s)
        status = get_volume_status(actor_name)
        last_status = status
        total = None
        for line in status.splitlines():
            if line.startswith("TOTAL_INSTANCES="):
                total = int(line.split("=", 1)[1])
                break
        if total is None:
            continue
        history.append(total)
        if len(history) >= stable_polls and len(set(history[-stable_polls:])) == 1:
            return (f"REGEN_DONE in {time.time() - t0:.0f}s | "
                    f"instance history: {history}\n--- 最后状态 ---\n{status}")
    return f"REGEN_TIMEOUT after {timeout_s}s | instance history: {history}\n--- 最后状态 ---\n{last_status}"


@mcp.tool()
def run_python(code: str) -> str:
    """逃生舱: 在 UE 编辑器内执行任意 Python 并返回 stdout。
    载荷自动套用桥纪律（try/except 不抛出 + gc.collect 收尾）。
    优先用上面的语义工具；复杂/一次性操作才用它。"""
    # 用户代码不进 try 缩进模板，而是自带纪律包装后直接执行
    wrapped = (
        "import gc, sys, traceback\n"
        "try:\n"
        + "\n".join(("    " + ln) if ln.strip() else ln for ln in code.strip("\n").split("\n"))
        + "\nexcept Exception:\n    traceback.print_exc(file=sys.stdout)\ngc.collect()\n"
    )
    return bridge_call(wrapped, read_timeout=600)


# ============================ A/B 验收 ============================

@mcp.tool()
def snapshot_instances(actor_name: str, label: str) -> str:
    """给一个 volume 的当前逐网格实例数拍照存档（存于本服务进程内存）。
    A/B 验收流程: 改动前 snapshot(label='before') → 改动+regen → snapshot(label='after')
    → diff_instances('before','after')。"""
    status = get_volume_status(actor_name)
    if "VOLUME_NOT_FOUND" in status:
        return status
    mesh_counts: dict[str, int] = {}
    for line in status.splitlines():
        if line.startswith("mesh ") and " = " in line:
            nm, cnt = line[5:].rsplit(" = ", 1)
            mesh_counts[nm] = mesh_counts.get(nm, 0) + int(cnt)
    _SNAPSHOTS[label] = mesh_counts
    return f"SNAPSHOT_SAVED label=%s | meshes=%d | total=%d" % (
        label, len(mesh_counts), sum(mesh_counts.values()))


@mcp.tool()
def diff_instances(label_a: str, label_b: str) -> str:
    """对比两次 snapshot 的逐网格实例数差值（面积型 A/B 验收的核心）。
    输出每个网格的 before/after/delta 与总差。"""
    if label_a not in _SNAPSHOTS or label_b not in _SNAPSHOTS:
        missing = [l for l in (label_a, label_b) if l not in _SNAPSHOTS]
        return "SNAPSHOT_MISSING: %s | 现有标签: %s" % (missing, list(_SNAPSHOTS))
    a, b = _SNAPSHOTS[label_a], _SNAPSHOTS[label_b]
    keys = sorted(set(a) | set(b))
    lines = []
    ta = tb = 0
    for k in keys:
        va, vb = a.get(k, 0), b.get(k, 0)
        ta += va
        tb += vb
        if va != vb:
            lines.append("%-40s %6d -> %6d  (%+d)" % (k, va, vb, vb - va))
    lines.append("TOTAL %d -> %d (%+d)" % (ta, tb, tb - ta))
    return "\n".join(lines) if len(lines) > 1 else "NO_DIFF total %d -> %d" % (ta, tb)


# --------------------------------------------------------------------------

if __name__ == "__main__":
    mcp.run()
