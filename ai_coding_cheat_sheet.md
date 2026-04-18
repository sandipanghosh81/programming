# AI workflow — $100/mo, premier models, 4–6 h/day

**Requirement:** Stay within **~$100/mo total** while maximizing premier models. Typical split: **Cursor Pro+** + **ChatGPT Plus (Codex)** for all engineering steps; **Google One / AI** (~$20) only if you want the bundle — it is **not** used in the table below so each step has exactly one tool and one model.

**Premier usage without burning out limits:** **Claude Sonnet** is still a **frontier** model — use it for **all long sessions** (requirements, planning, coding). Reserve **Claude Opus** for **two short windows per day** only (architecture + code review). Run **all** test/fix/retry loops in **Codex** so Cursor request counters are not spent on terminal churn.

---

## One table — one choice per step

| Step | Use this product | Use this model (only) | **Ollama (local)** — one model per step | Why |
|------|------------------|------------------------|----------------------------------------|-----|
| **1. Requirements & discussion** | **Cursor Pro+** | **Claude Sonnet** | **`llama3.1:70b`** | Long back-and-forth; Sonnet is premier and sustainable for multi-hour threads. Local: strongest general reasoning in your pull. |
| **2. Planning & refinement** | **Cursor Pro+** | **Claude Sonnet** | **`llama3.1:70b`** | Same thread style; keeps repo context without Opus burn. Local: same as requirements. |
| **3. Architecture document & refinement** | **Cursor Pro+** | **Claude Opus** | **`llama3.1:70b`** | Single clearest “max premier” slot for boundaries, risks, and migrations. Local: best offline reasoning for system design. |
| **4. Coding iterations** | **Cursor Pro+** | **Claude Sonnet** | **`qwen2.5-coder:32b`** | Default implementation workhorse (premier, high daily ceiling). Local: best code-specialized model in your list. |
| **5. Testing iterations** | **Codex** (via **ChatGPT Plus**) | **Default / auto frontier** (do not pick a smaller model) | **`qwen2.5-coder:32b`** | Test runs and retries burn **ChatGPT/Codex** quota, **not** Cursor — protects 4–6 h Cursor days. Local: code + log reasoning when cloud/offline. |
| **6. Code review** | **Cursor Pro+** | **Claude Opus** | **`qwen2.5-coder:32b`** | Second fixed Opus window for merge-critical review (security, concurrency, invariants). Local: code-focused review (same as coding/testing). |

**Installed Ollama models (your `ollama list`):** `gemma4:latest`, `llama3.1:70b`, `qwen2.5:32b`, `qwen2.5-coder:32b`.

**Local fallbacks (no second choice in-table):** Use **`gemma4:latest`** only when RAM/time is tight or for quick smoke prompts. Use **`qwen2.5:32b`** if **`llama3.1:70b`** is too heavy on memory for steps 1–3 (same role: general reasoning, smaller footprint).

**Daily rhythm (limits-safe):** **Sonnet** for steps 1, 2, and 4 across the full session. **Opus** only for step 3 and step 6 (short, focused chats). **Codex** for step 5 whenever tests run. If Cursor still feels tight, shorten Opus windows (finish architecture, then close chat; same for review) — **do not** switch steps 1–2–4 to a cheaper model; narrow `@` files and start fresh chats instead. For **offline** work, mirror the same rhythm: **`llama3.1:70b`** for 1–3, **`qwen2.5-coder:32b`** for 4–6 (use **Continue** or another Ollama-backed client, or `ollama run <model>` in terminal).
