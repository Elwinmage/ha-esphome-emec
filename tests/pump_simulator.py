#!/usr/bin/env python3
"""Minimal EMEC WDPHRH simulator on a pseudo-terminal.

Lets you run the component without the real controller, e.g. with an ESPHome
`host:` build whose `uart: port:` points at the printed (or --link) path.

    python3 tests/pump_simulator.py --link /tmp/emec_pty

It answers read queries, ACKs writes with "<cmd>okend", applies them to its
internal state, and goes silent when the 500 ms heartbeat stops.
"""
from __future__ import annotations

import argparse
import os
import pty
import select
import time
import tty

PREFIX, SLAVE = "34", "01"

REGS = {
    "valuer": ["0723", "0655", "000", "0"],
    "setpntr": ["0770", "0730", "040", "000", "00", "0",
                "0630", "0670", "050", "000", "00", "0"],
    "alldosr": ["0", "000", "0", "000"],
    "allrmr": ["0", "1", "0", "0", "0", "0", "0", "1"],
    "outptr": ["1", "0", "0"],
    "flowstr": ["2", "05"],
    "diginpr": ["0", "0", "1"],
    "servicr": ["0", "1234"],
    "setstkr": ["180", "120"],
    "paramtr": ["0000", "05", "1"],
    "allprbr": ["0", "000", "1", "120"],
}

# write command -> (read group it updates, field widths of the payload)
WRITES = {
    "setpnw": ("setpntr", [4, 4, 3, 3, 2, 1, 4, 4, 3, 3, 2, 1]),
    "alldow": ("alldosr", [1, 3, 1, 3]),
    "setskw": ("setstkr", [3, 3]),
    "paramw": ("paramtr", [4, 2, 1, 2]),      # trailing tau is not readable
    "allprw": ("allprbr", [1, 3, 1, 3]),
    "flowsw": ("flowstr", [1, 2]),
    "diginw": ("diginpr", [1, 1, 1]),
    "clockw": (None, [15]),
    "resalw": (None, [1]),
}


def handle(line: str, state: dict) -> str | None:
    """Return the reply for one master frame (None = stay silent)."""
    now = time.monotonic()
    if line == f"{PREFIX}tb00 #0#":
        state["hb"] = now
        return None
    if now - state.get("hb", 0.0) > 1.2:      # session lost without heartbeat
        return None
    head = PREFIX + SLAVE
    if not line.startswith(head):
        return None
    body = line[len(head):]
    if body in REGS:
        return f"{PREFIX}gpd{SLAVE}&WD#" + "#".join(REGS[body]) + f"#{body}end\r"
    for cmd, (group, widths) in WRITES.items():
        tail = f"{cmd}end"
        if not (body.startswith(cmd) and body.endswith(tail)):
            continue
        payload = body[len(cmd):len(body) - len(tail)]
        if len(payload) != sum(widths):
            return f"{PREFIX}gpd{SLAVE}&WD#no#changeend\r"
        print(f"  write {cmd}: {payload!r}", flush=True)
        if cmd == "resalw":
            REGS["allrmr"] = ["0"] * 8
        if group is not None:
            fields, pos = [], 0
            for w in widths:
                fields.append(payload[pos:pos + w])
                pos += w
            REGS[group] = fields[:len(REGS[group])]
        return f"{PREFIX}gpd{SLAVE}&{cmd}okend\r"
    return None


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--link", help="create this symlink to the pty slave")
    args = ap.parse_args()

    master, slave = pty.openpty()
    tty.setraw(slave)
    path = os.ttyname(slave)
    if args.link:
        if os.path.islink(args.link):
            os.unlink(args.link)
        os.symlink(path, args.link)
        path = args.link
    print(f"EMEC WDPHRH simulator on {path} (38400 8N1)", flush=True)

    buf, state = "", {}
    while True:
        ready, _, _ = select.select([master], [], [], 1.0)
        if not ready:
            continue
        buf += os.read(master, 1024).decode("ascii", errors="replace")
        while True:
            cut = min((i for i in (buf.find("\r"), buf.find("\n")) if i >= 0), default=-1)
            if cut < 0:
                break
            line, buf = buf[:cut], buf[cut + 1:]
            if not line:
                continue
            reply = handle(line, state)
            if reply is not None:
                time.sleep(0.03)
                os.write(master, reply.encode())


if __name__ == "__main__":
    main()
