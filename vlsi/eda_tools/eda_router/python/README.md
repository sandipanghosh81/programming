# Python integration plan

This folder is reserved for the future Python-facing layer of `routing_genetic_astar`.

Planned responsibilities:

- expose the C++ routing core to Python;
- run quick experiments from notebooks or scripts;
- support LangGraph/agentic orchestration workflows;
- make it easy to compare the original Python prototype with the C++ implementation.

A likely future direction is a small `pybind11` module plus smoke tests written in Python.
