"""
drc/  —  KLayout-based DRC gate services
─────────────────────────────────────────
Public surface:
  from orchestrator.drc import run_drc_gate, DRCViolation
"""

from .klayout_client import run_drc_gate
from .rdb_parser import DRCViolation, parse_rdb

__all__ = ["run_drc_gate", "DRCViolation", "parse_rdb"]
