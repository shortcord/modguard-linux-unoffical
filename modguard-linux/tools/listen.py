#!/usr/bin/env python3
"""Print MODGuard Linux exhaust-demand datagrams."""

import os
import socket


path = os.environ.get("MODGUARD_EXHAUST_SOCKET", f"/tmp/modguard-exhaust-{os.getuid()}.sock")
if not os.path.isabs(path):
    raise SystemExit("MODGUARD_EXHAUST_SOCKET must be an absolute path")

try:
    os.unlink(path)
except FileNotFoundError:
    pass

client = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
try:
    client.bind(path)
    while True:
        print(client.recv(4096).decode("utf-8"), flush=True)
except KeyboardInterrupt:
    pass
finally:
    client.close()
    try:
        os.unlink(path)
    except FileNotFoundError:
        pass
