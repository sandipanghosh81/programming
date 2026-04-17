"""
agent/modules/__init__.py
Exposes the module subgraph factories so Graph A can import them cleanly.
"""
#
# IMPORTANT:
# Keep this package importable even when optional heavy dependencies (langchain,
# langgraph, etc.) are not installed. Some tests import only `_cli_client` and
# should not fail just because graph/LLM deps are missing.
#
# We therefore lazily import the subgraph factories inside these wrappers.

def create_router_subgraph():
    from .m1_router_subgraph import create_router_subgraph as _impl
    return _impl()


def create_placer_subgraph():
    from .m2_placer_subgraph import create_placer_subgraph as _impl
    return _impl()


def create_db_subgraph():
    from .m3_db_subgraph import create_db_subgraph as _impl
    return _impl()


def create_window_subgraph():
    from .m4_window_subgraph import create_window_subgraph as _impl
    return _impl()

__all__ = [
    "create_router_subgraph",
    "create_placer_subgraph",
    "create_db_subgraph",
    "create_window_subgraph",
]
