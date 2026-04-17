# `routing_genetic_astar`

This subproject is the planned **C++23 port/scaffold** for the routing idea explored in `python_programs/optimizations/routing_deap_rustworkx_astar.py`.

## Reference algorithm identified from the Python file

The Python reference is a **hybrid multi-net, multi-layer routing algorithm** built from several parts working together:

1. a **weighted 3D routing graph**;
2. **Dijkstra** precomputation for pin-to-pin costs;
3. a **genetic algorithm** to optimize the **pin order** for each net;
4. a second **genetic algorithm** to optimize the **global order of nets**;
5. **A\*** search with a **KD-tree-guided heuristic** to grow each route toward an already-routed tree, which makes the behavior **Steiner-tree-like**;
6. congestion, blockage, and short/open penalties during evaluation.

So the best short description is:

> **Hybrid evolutionary detailed router: GA + Dijkstra + A\* on a weighted 3D grid graph.**

## Folder layout

```text
routing_genetic_astar/
  Makefile
  README.md
  include/
  src/
  tests/
  python/
  docs/
```

## Build locally

From this folder:

```bash
make
make test
```

From the repo root:

```bash
make routing_genetic_astar
make routing_genetic_astar-test
```

## Current status

This is a **project scaffold** rather than the full router implementation. The first version focuses on:

- a portable C++23 structure;
- a documented architecture;
- a root/subproject build flow;
- a clear path to future Python bindings and algorithm implementation.

## Planned C++ components

- `RoutingGridGraph`
- `PinOrderOptimizer`
- `NetOrderOptimizer`
- `AStarTreeRouter`
- `CongestionTracker`
- `RouteEvaluator`
- Python binding layer under `python/`
