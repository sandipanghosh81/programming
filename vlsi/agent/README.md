# vlsi-agent

LangGraph-based VLSI EDA orchestration: FastAPI `server.py`, KLayout chatbot macro, and WebSocket clients to the C++ `eda_daemon`.

- **Run:** `python server.py` (from this directory) or `make daemon` from `vlsi/`.
- **Docs:** see `../docs/chatbot_howto.md` and `../docs/agent/`.

Package layout: Python sources live under `src/orchestrator/`.
