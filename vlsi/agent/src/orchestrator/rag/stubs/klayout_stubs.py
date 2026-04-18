"""
klayout_stubs.py  —  Minimal KLayout Python API stubs for REPL sandbox
─────────────────────────────────────────────────────────────────────────────
Provides the subset of klayout.db and klayout.lay APIs needed to dry-run
generated KLayout-Python EUs without a full KLayout installation.

COMPLETENESS:
  Only the most-used classes are stubbed.  If a generated EU calls an
  unrecognised method, it raises AttributeError — which is what we want:
  the REPL validation will catch it and report the missing API.

USAGE:
  This file is injected into sys.path by python_repl.py before the
  user script runs.  Generated scripts import klayout.db as normal.
"""

import sys
import types


# ─── Stub classes ─────────────────────────────────────────────────────────────
class _Box:
    def __init__(self, x1=0, y1=0, x2=0, y2=0): pass
    def width(self): return 0
    def height(self): return 0


class _Point:
    def __init__(self, x=0, y=0): self.x = x; self.y = y


class _Path:
    def __init__(self, pts=None, w=0): pass


class _Polygon:
    def __init__(self, pts=None): pass


class _LayerInfo:
    def __init__(self, layer=0, datatype=0, name=""): pass


class _Layout:
    def __init__(self): pass
    def create_cell(self, name): return _Cell()
    def layer(self, info): return 0
    def insert_layer(self, info): return 0
    def write(self, path): pass
    def read(self, path): pass
    def top_cells(self): return []
    def cell(self, idx): return _Cell()
    def dbu(self): return 0.001


class _Cell:
    def __init__(self): pass
    def shapes(self, layer): return _Shapes()
    def bbox(self): return _Box()
    def name(self): return ""


class _Shapes:
    def __init__(self): self._list = []
    def insert(self, shape): self._list.append(shape)
    def each(self): return iter(self._list)


class _Region:
    def __init__(self): pass
    def insert(self, shape): pass
    def merged(self): return self
    def not_interacting(self, other): return self
    def size(self, d): return self


# ─── Build fake module hierarchy ──────────────────────────────────────────────
def _build_klayout_module() -> types.ModuleType:
    klayout    = types.ModuleType("klayout")
    klayout_db = types.ModuleType("klayout.db")
    klayout_lay = types.ModuleType("klayout.lay")

    klayout_db.Box      = _Box
    klayout_db.Point    = _Point
    klayout_db.Path     = _Path
    klayout_db.Polygon  = _Polygon
    klayout_db.LayerInfo = _LayerInfo
    klayout_db.Layout   = _Layout
    klayout_db.Cell     = _Cell
    klayout_db.Shapes   = _Shapes
    klayout_db.Region   = _Region

    # klayout.lay stubs (viewer — no-op in sandbox)
    class _LayoutView:
        def zoom_box(self, box): pass
        def set_current_cell_name(self, name): pass
        def refresh(self): pass
        current_view = property(lambda self: None)

    klayout_lay.LayoutView = _LayoutView

    klayout.db  = klayout_db
    klayout.lay = klayout_lay

    sys.modules["klayout"]     = klayout
    sys.modules["klayout.db"]  = klayout_db
    sys.modules["klayout.lay"] = klayout_lay
    return klayout


_build_klayout_module()
