# -*- coding: utf-8 -*-
"""
UE Editor TCP Bridge — editor side of the PCGMCP path.
=====================================================

A ~60-line, dependency-free TCP bridge that auto-starts with the Unreal Editor
and executes Python payloads on the game thread (via a Slate post-tick callback).

Protocol (one-shot connections):
    client ->  {"code": "<python source>"}      UTF-8 JSON, single send, < 64 KB
    client <-  {"result": "<captured stdout>"}  or  {"message": "<error text>"}

Install:
    Paste this file's contents into YourProject/Content/Python/init_unreal.py —
    the Python Editor Script Plugin auto-executes that file at editor startup.

Port:
    Default 9877, override with the UE_BRIDGE_PORT environment variable.
    Use one port per running editor instance — two editors listening on the
    same port will route commands to the wrong one.

Why post-tick instead of a blocking wait:
    The socket thread never blocks on the game thread; payloads are queued and
    executed on the next Slate tick. A busy editor (PCG regen, PIE, a modal
    dialog) delays results instead of deadlocking the bridge.

Payload conventions (enforced by pcg_mcp_server.py on the client side):
    - Keep payloads < 64 KB (single recv; localhost sends of this size arrive
      in one segment).
    - Print diagnostics BEFORE risky calls — the bridge returns stdout only,
      so stderr tracebacks are invisible unless routed
      (`traceback.print_exc(file=sys.stdout)`).
    - End every payload with `import gc; gc.collect()` — delayed GC of stale
      struct wrappers has crashed editors (PyWrapperStruct ensure + AV).
"""

import os as _os
import socket as _sock
import json as _json
import io as _io
import sys as _sys
import threading as _thr

_PORT = int(_os.environ.get('UE_BRIDGE_PORT', '9877'))

_pending = []
_lock = _thr.Lock()


def _on_tick(_dt):
    with _lock:
        if not _pending:
            return
        conn, code = _pending.pop(0)
    out = _io.StringIO()
    orig = _sys.stdout
    _sys.stdout = out
    err = None
    try:
        exec(code, {'unreal': __import__('unreal')})
    except Exception as e:  # noqa: BLE001
        err = str(e)
    finally:
        _sys.stdout = orig
    try:
        if err is None:
            conn.sendall(_json.dumps({'result': out.getvalue()}).encode())
        else:
            conn.sendall(_json.dumps({'message': err}).encode())
    except Exception:
        pass
    finally:
        try:
            conn.close()
        except Exception:
            pass


def _serve():
    srv = _sock.socket(_sock.AF_INET, _sock.SOCK_STREAM)
    srv.setsockopt(_sock.SOL_SOCKET, _sock.SO_REUSEADDR, 1)
    srv.bind(('127.0.0.1', _PORT))
    srv.listen(5)
    print('UE bridge auto-started on port %d' % _PORT)
    while True:
        c = None
        try:
            c, _ = srv.accept()
            msg = _json.loads(c.recv(65536).decode())
            with _lock:
                _pending.append((c, msg.get('code', '')))
        except Exception:
            try:
                if c:
                    c.close()
            except Exception:
                pass


_thr.Thread(target=_serve, daemon=True).start()

import unreal  # noqa: E402

unreal.register_slate_post_tick_callback(_on_tick)
