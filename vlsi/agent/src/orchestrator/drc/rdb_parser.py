"""
rdb_parser.py  —  KLayout RDB (Rule Database) report parser
─────────────────────────────────────────────────────────────────────────────
Parses the XML .rdb file produced by KLayout's DRC engine and returns a
structured list of DRCViolation objects.

RDB FORMAT (KLayout .rdb XML subset):
  <rdb>
    <categories>
      <category id="1" name="M1_width"> <description>M1 min width violation</description> </category>
    </categories>
    <items>
      <item category="1">
        <values>
          <value>box: (10,20;200,40)</value>
        </values>
      </item>
    </items>
  </rdb>

USAGE:
  from orchestrator.drc.rdb_parser import parse_rdb, DRCViolation
  violations = parse_rdb("/eda_share/drc_report_001.rdb")
  for v in violations:
      print(v.rule, v.x1, v.y1, v.x2, v.y2)
"""

from __future__ import annotations

import logging
import re
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

logger = logging.getLogger(__name__)


@dataclass
class DRCViolation:
    rule:        str
    description: str
    x1: float = 0.0
    y1: float = 0.0
    x2: float = 0.0
    y2: float = 0.0

    def to_dict(self) -> dict:
        return {
            "rule":        self.rule,
            "description": self.description,
            "bbox":        [self.x1, self.y1, self.x2, self.y2],
        }


def parse_rdb(rdb_path: str | Path) -> list[DRCViolation]:
    """
    Parse a KLayout .rdb XML file and return all violations.

    Returns an empty list if the file doesn't exist or has no items.
    """
    rdb_path = Path(rdb_path)
    if not rdb_path.exists():
        logger.warning("[RDB Parser] File not found: %s", rdb_path)
        return []

    try:
        tree = ET.parse(str(rdb_path))
        root = tree.getroot()
    except ET.ParseError as e:
        logger.error("[RDB Parser] XML parse error in %s: %s", rdb_path, e)
        return []

    # Build category id → (name, description) map
    categories: dict[str, tuple[str, str]] = {}
    for cat in root.iter("category"):
        cat_id   = cat.get("id", "")
        cat_name = cat.get("name", "UNKNOWN")
        desc_el  = cat.find("description")
        desc     = desc_el.text.strip() if desc_el is not None and desc_el.text else cat_name
        categories[cat_id] = (cat_name, desc)

    violations: list[DRCViolation] = []

    for item in root.iter("item"):
        cat_id    = item.get("category", "")
        rule, desc = categories.get(cat_id, ("UNKNOWN", ""))

        x1 = y1 = x2 = y2 = 0.0
        for val_el in item.iter("value"):
            text = (val_el.text or "").strip()
            bbox = _parse_bbox(text)
            if bbox:
                x1, y1, x2, y2 = bbox
                break

        violations.append(DRCViolation(
            rule=rule, description=desc,
            x1=x1, y1=y1, x2=x2, y2=y2,
        ))

    logger.info("[RDB Parser] %d violation(s) in %s", len(violations), rdb_path.name)
    return violations


# Pattern: "box: (x1,y1;x2,y2)"  (KLayout value format)
_BOX_PATTERN = re.compile(
    r'\(\s*([+-]?[\d.]+)\s*,\s*([+-]?[\d.]+)\s*;\s*([+-]?[\d.]+)\s*,\s*([+-]?[\d.]+)\s*\)'
)


def _parse_bbox(text: str) -> Optional[tuple[float, float, float, float]]:
    """Extract (x1, y1, x2, y2) from a KLayout RDB value string."""
    m = _BOX_PATTERN.search(text)
    if m:
        return float(m.group(1)), float(m.group(2)), float(m.group(3)), float(m.group(4))
    return None


def violation_summary(violations: list[DRCViolation]) -> dict[str, int]:
    """Return {rule_name: count} summary."""
    counts: dict[str, int] = {}
    for v in violations:
        counts[v.rule] = counts.get(v.rule, 0) + 1
    return counts
