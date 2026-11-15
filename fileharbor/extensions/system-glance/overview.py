#!/usr/bin/env python3
"""Sample FileHarbor page: prints JSON that the file manager draws. Run it by hand to see the output."""
import json
import os
import platform
import shutil
import time


def meminfo():
    d = {}
    with open("/proc/meminfo") as f:
        for line in f:
            k, _, v = line.partition(":")
            d[k] = int(v.split()[0]) * 1024
    return d


def human(n):
    for u in ("B", "KB", "MB", "GB", "TB"):
        if n < 1000 or u == "TB":
            return ("%d %s" % (n, u)) if u == "B" else ("%.1f %s" % (n, u))
        n /= 1000.0


def uptime():
    with open("/proc/uptime") as f:
        s = int(float(f.read().split()[0]))
    d, r = divmod(s, 86400)
    h, r = divmod(r, 3600)
    return "%dd %dh %dm" % (d, h, r // 60) if d else "%dh %dm" % (h, r // 60)


m = meminfo()
used = m["MemTotal"] - m["MemAvailable"]
du = shutil.disk_usage(os.path.expanduser("~"))
load = os.getloadavg()
blocks = [
    {"type": "text", "text": "%s · %s · kernel %s" % (platform.node(), platform.machine(), platform.release()), "dim": True},
    {"type": "heading", "level": 2, "text": "Memory"},
    {"type": "progress", "value": used / m["MemTotal"], "label": "%s of %s used" % (human(used), human(m["MemTotal"]))},
    {"type": "heading", "level": 2, "text": "Home disk"},
    {"type": "progress", "value": du.used / du.total, "label": "%s of %s used, %s free" % (human(du.used), human(du.total), human(du.free))},
    {"type": "heading", "level": 2, "text": "Load"},
    {"type": "table", "columns": ["1 min", "5 min", "15 min", "Uptime"], "rows": [["%.2f" % load[0], "%.2f" % load[1], "%.2f" % load[2], uptime()]]},
    {"type": "heading", "level": 2, "text": "Quick links"},
    {"type": "list", "items": [
        {"title": "Home", "subtitle": os.path.expanduser("~"), "icon": "user-home-symbolic", "open": os.path.expanduser("~")},
        {"title": "Downloads", "icon": "folder-download-symbolic", "open": os.path.expanduser("~/Downloads")},
        {"title": "Storage page", "subtitle": "What is using your disk", "icon": "drive-harddisk-symbolic", "open": "x-nemo-storage:///"},
    ]},
    {"type": "text", "text": "Updated " + time.strftime("%H:%M:%S"), "dim": True},
]
print(json.dumps({"title": "System", "blocks": blocks}))
