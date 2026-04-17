from __future__ import annotations

from dataclasses import dataclass
from typing import Any


@dataclass(frozen=True)
class Pt:
    x: float
    y: float


def _pin_offset_from_problem(analog_problem: dict[str, Any], inst_id: str, pin_name: str) -> Pt | None:
    for inst in analog_problem.get("instances", []):
        if inst.get("id") != inst_id:
            continue
        variants = inst.get("variants") or []
        if not variants:
            return None
        v0 = variants[0]
        for p in v0.get("pins", []) or []:
            if p.get("name") == pin_name:
                return Pt(float(p.get("x", 0.0)), float(p.get("y", 0.0)))
    return None


def _inst_rect_from_placed(placed: list[dict[str, Any]], inst_id: str) -> dict[str, float] | None:
    for p in placed:
        if p.get("id") == inst_id:
            return {
                "x": float(p.get("x", 0.0)),
                "y": float(p.get("y", 0.0)),
                "w": float(p.get("w", 0.0)),
                "h": float(p.get("h", 0.0)),
            }
    return None


def _pin_point(analog_problem: dict[str, Any], placed: list[dict[str, Any]], inst_id: str, pin_name: str) -> Pt | None:
    r = _inst_rect_from_placed(placed, inst_id)
    if r is None:
        return None
    off = _pin_offset_from_problem(analog_problem, inst_id, pin_name)
    if off is None:
        # Fallback: center of instance
        return Pt(r["x"] + r["w"] * 0.5, r["y"] + r["h"] * 0.5)
    return Pt(r["x"] + off.x, r["y"] + off.y)


def _manhattan(a: Pt, b: Pt) -> float:
    return abs(a.x - b.x) + abs(a.y - b.y)


def _l_route(a: Pt, b: Pt, *, prefer_hv: bool = True) -> list[tuple[Pt, Pt]]:
    """
    Return 2 segments: horizontal-then-vertical (HV) or vertical-then-horizontal (VH).
    """
    if prefer_hv:
        mid = Pt(b.x, a.y)
    else:
        mid = Pt(a.x, b.y)
    segs: list[tuple[Pt, Pt]] = []
    if (a.x, a.y) != (mid.x, mid.y):
        segs.append((a, mid))
    if (mid.x, mid.y) != (b.x, b.y):
        segs.append((mid, b))
    return segs


def _mst_edges(points: list[Pt]) -> list[tuple[int, int]]:
    """
    Prim MST under Manhattan metric (O(n^2), fine for early routing visuals).
    """
    n = len(points)
    if n <= 1:
        return []
    in_tree = [False] * n
    dist = [1e100] * n
    parent = [-1] * n
    dist[0] = 0.0
    edges: list[tuple[int, int]] = []
    for _ in range(n):
        # pick min
        u = -1
        best = 1e100
        for i in range(n):
            if not in_tree[i] and dist[i] < best:
                best = dist[i]
                u = i
        if u == -1:
            break
        in_tree[u] = True
        if parent[u] != -1:
            edges.append((parent[u], u))
        for v in range(n):
            if in_tree[v]:
                continue
            d = _manhattan(points[u], points[v])
            if d < dist[v]:
                dist[v] = d
                parent[v] = u
    return edges


def build_early_routes(
    *,
    analog_problem: dict[str, Any],
    placed: list[dict[str, Any]],
    net_limit: int = 60,
) -> dict[str, Any]:
    """
    Build simple Manhattan polyline routes for visualization in KLayout.

    Returns: {"routes":[{"net":"N1","segments":[{"x1":..,"y1":..,"x2":..,"y2":..}, ...]}, ...]}
    """
    nets = list(analog_problem.get("nets") or [])
    # Prefer heavier nets first.
    nets.sort(key=lambda n: float(n.get("weight", 1.0)), reverse=True)
    nets = nets[: max(0, int(net_limit))]

    out_routes: list[dict[str, Any]] = []
    for net in nets:
        pins = net.get("pins") or []
        pts: list[Pt] = []
        for pr in pins:
            inst = pr.get("inst")
            pin = pr.get("pin")
            if not inst or not pin:
                continue
            pt = _pin_point(analog_problem, placed, str(inst), str(pin))
            if pt is not None:
                pts.append(pt)
        if len(pts) < 2:
            continue

        edges = _mst_edges(pts)
        segs: list[dict[str, float]] = []
        for i, j in edges:
            # Alternate HV/VH to reduce “all elbows aligned” visuals.
            prefer_hv = ((i + j) % 2 == 0)
            for a, b in _l_route(pts[i], pts[j], prefer_hv=prefer_hv):
                segs.append({"x1": a.x, "y1": a.y, "x2": b.x, "y2": b.y})

        out_routes.append({"net": net.get("name", ""), "segments": segs})

    return {"routes": out_routes}

