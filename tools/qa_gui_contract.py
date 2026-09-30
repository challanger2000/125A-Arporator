#!/usr/bin/env python3
from pathlib import Path
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
UIDESC = ROOT / "resource" / "Arporator.uidesc"

W, H = 1180.0, 680.0

def pair(text):
    a, b = text.split(",", 1)
    return float(a), float(b)

tree = ET.parse(UIDESC)
root = tree.getroot()
template = root.find(".//template[@name='view']")
assert template is not None, "missing view template"
tw, th = pair(template.attrib["size"])
assert (tw, th) == (W, H), f"editor size must be {W}x{H}, got {tw}x{th}"

views = list(template.iter("view"))
for view in views:
    if "origin" not in view.attrib or "size" not in view.attrib:
        continue
    x, y = pair(view.attrib["origin"])
    w, h = pair(view.attrib["size"])
    assert w >= 0 and h >= 0, f"negative view size: {view.attrib}"
    assert x >= 0 and y >= 0, f"negative view origin: {view.attrib}"
    assert x + w <= W + 1e-9, f"view exceeds editor width: {view.attrib}"
    assert y + h <= H + 1e-9, f"view exceeds editor height: {view.attrib}"

custom = {}
for view in views:
    name = view.attrib.get("custom-view-name")
    if name:
        custom.setdefault(name, []).append(view)

required = {
    "ArpFaceplate": 1,
    "ArpLogo": 1,
    "ArpUIScale": 1,
    "ArpStepGrid": 1,
    "ArpVariate": 1,
}
for name, count in required.items():
    actual = len(custom.get(name, []))
    assert actual == count, f"{name}: expected {count}, found {actual}"

assert not custom.get("ArpSelectedStep"), (
    "separate selected-step panel must not return; step editing belongs in the pattern grid"
)

grid = custom["ArpStepGrid"][0]
gw, gh = pair(grid.attrib["size"])
assert gw >= 1000 and gh >= 240, f"direct-edit step grid became too small: {gw}x{gh}"

# Seven global dimension locks are required by the V0.1 behaviour contract.
lock_tags = sorted(
    int(v.attrib["control-tag"])
    for v in custom.get("ArpLock", [])
    if "control-tag" in v.attrib
)
expected_locks = sorted([1014, 1015, 1016, 1017, 1018, 1019, 1022])
assert lock_tags == expected_locks, (
    f"dimension lock controls mismatch: {lock_tags} != {expected_locks}"
)

# Critical global control tags must occur exactly once in the custom UI.
critical_tags = [1000,1001,1002,1003,1004,1005,1006,1007,1008,1009,
                 1010,1011,1012,1013,1020,1021]
counts = {tag: 0 for tag in critical_tags}
for view in views:
    if "control-tag" not in view.attrib:
        continue
    tag = int(view.attrib["control-tag"])
    if tag in counts:
        counts[tag] += 1
for tag, count in counts.items():
    assert count == 1, f"critical control tag {tag} occurs {count} times"

print("Arporator GUI contract PASS")
