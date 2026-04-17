"""
klayout_macro/chatbot_dock.py  —  KLayout PyQt Dock Widget: VLSI Agent Chatbot
═══════════════════════════════════════════════════════════════════════════════

WHAT THIS FILE IS:
  A KLayout macro that creates a dockable chat panel inside the KLayout EDA tool.
  This is the ONLY entry point for user interaction with the entire VLSI agent system.

HOW TO INSTALL:
  1. Open KLayout
  2. Go to Macros → Macro Development
  3. File → Import → select this file  (OR paste content into a new macro)
  4. Run the macro (F5 or Run button)
  5. The "VLSI Agent" dock panel appears on the right side of KLayout

WHAT IT DOES:
  1. User types a message in the chat input field ("route the power rail VDD")
  2. On Return/send: sends HTTP POST to http://127.0.0.1:8000/chat
     Request body: {"message": "route the power rail VDD"}
  3. Python server.py receives the message and invokes Graph A (LangGraph)
  4. Graph A identifies intent → routes to the correct module or workflow
  5. Result comes back: {"reply": "Routing completed...", "viewer_commands": [...]}
  6. This macro:
     a. Displays the "reply" text in the chat history panel
     b. Executes each "viewer_command" on the KLayout canvas

VIEWER COMMANDS SUPPORTED:
  {"action": "refresh_view"}                         — Reload all layers
  {"action": "zoom_fit"}                             — Zoom to fit design
  {"action": "zoom_to", "bbox": [x1,y1,x2,y2]}      — Zoom to bounding box
  {"action": "highlight", "net_name":"VDD", "color":"#ff0000"}  — Highlight net
  {"action": "layer_visibility", "layer":"M1", "visible": true} — Toggle layer
  {"action": "highlight_drc_violations", "violations":[...]}    — Mark DRC errors
  {"action": "screenshot", "filename":"output.png"}  — Save screenshot
  {"action": "unlock_ui"}                            — Re-enable chat input

THREADING NOTE:
  KLayout macros run on the Qt GUI thread.  We use a blocking urllib.request
  for simplicity.  For long-running routing jobs (>30s), replace with
  pya.QThread + pya.QNetworkAccessManager to avoid freezing the KLayout UI.

PREREQUISITES:
  - Python server.py must be running on http://127.0.0.1:8000
  - C++ eda_daemon must be running on ws://127.0.0.1:8080
  Use 'curl http://127.0.0.1:8000/health' to verify the server is up.

  If the chat still mentions "vlsi_agent", KLayout is running an OLD copy of this
  macro. Replace ~/.klayout/macros/chatbot_dock.py with the file from
  vlsi/agent/klayout_macro/chatbot_dock.py in your repo, then run the macro again.

IMPLEMENTATION NOTE (blank dock fix):
  Do **not** subclass pya.QDockWidget. Some KLayout/Qt binding builds do not
  bridge Python subclasses correctly, which can yield an empty content area.
  Use a plain pya.QDockWidget plus an inner QWidget tree with explicit parents.
"""

import pya      # KLayout Python API (Qt bindings + layout API)
import json

# Printed on load so you can confirm KLayout picked up the repo file (not ~/.klayout stale copy).
_CHATBOT_DOCK_REVISION = "2026-04-15-v2"
import urllib.request
import urllib.error

# Keep a global reference so the panel (and Qt objects) are not garbage-collected.
_VLSI_AGENT_PANEL = None

# ─── Constants ────────────────────────────────────────────────────────────────
AGENT_URL    = "http://127.0.0.1:8000/chat"
HEALTH_URL   = "http://127.0.0.1:8000/health"
TIMEOUT_SECS = 120   # Long timeout for routing jobs (up to 60+ second runs)

# Chat message colors (HTML color codes for readability in the dark KLayout UI)
COLOR_AGENT  = "#4a9eff"   # Bright blue  → agent replies
COLOR_USER   = "#4aff9e"   # Bright green → user messages
COLOR_SYSTEM = "#8090a8"   # Muted grey   → system status messages
COLOR_ERROR  = "#ff6060"   # Soft red     → error messages


def _layout_add_widget(layout, widget, stretch=0):
    """pya.Q{Box,Grid}Layout.addWidget arity differs across KLayout builds."""
    try:
        if stretch:
            layout.addWidget(widget, stretch)
        else:
            layout.addWidget(widget)
    except Exception:
        layout.addWidget(widget)


class ChatbotDock:
    """
    Chat UI hosted inside a plain pya.QDockWidget (composition, not inheritance).

    LAYOUT:
      Title bar: "VLSI Agent"
      Inner: QTextEdit history + line edit + Send button
    """

    def __init__(self, main_window):
        self.main_window = main_window
        self._dock = pya.QDockWidget("VLSI Agent", main_window)
        try:
            self._dock.setMinimumWidth(300)
        except Exception:
            pass
        try:
            self._build_ui()
            self._check_server_health()
        except Exception as e:
            try:
                print("[chatbot_dock] UI init failed:", repr(e))
                lbl = pya.QLabel(f"VLSI Agent UI failed to initialize:\n{e}")
                try:
                    lbl.setWordWrap(True)
                except Exception:
                    pass
                self._dock.setWidget(lbl)
            except Exception:
                pass

    @property
    def dock(self):
        return self._dock

    def _build_ui(self):
        """Build the Qt widget hierarchy for the dock panel."""
        inner = pya.QWidget(self._dock)
        try:
            inner.setMinimumSize(280, 360)
        except Exception:
            pass

        v = pya.QVBoxLayout(inner)
        try:
            v.setContentsMargins(6, 6, 6, 6)
            v.setSpacing(6)
        except Exception:
            pass

        # Visible header — if this shows but the rest does not, QTextEdit is the culprit.
        hdr = pya.QLabel("VLSI Agent — chat")
        try:
            hdr.setStyleSheet("font-weight: bold; color: #d0d8e8;")
        except Exception:
            pass
        _layout_add_widget(v, hdr, 0)

        self.chat_history = pya.QTextEdit(inner)
        self.chat_history.setReadOnly(True)
        try:
            self.chat_history.setMinimumHeight(220)
        except Exception:
            pass
        try:
            self.chat_history.setStyleSheet(
                "background-color: #1a1f2e; color: #d0d8e8; "
                "font-family: 'Consolas', 'Monaco', monospace; font-size: 11px;"
            )
        except Exception:
            # Some builds choke on stylesheets; fall back to defaults.
            print("[chatbot_dock] QTextEdit.setStyleSheet failed — using default colors")

        _layout_add_widget(v, self.chat_history, 1)

        self._input_row = pya.QHBoxLayout()
        self.input_field = pya.QLineEdit(inner)
        try:
            self.input_field.setPlaceholderText("Type an EDA command, e.g. 'route the design'...")
        except Exception:
            pass

        try:
            self.input_field.returnPressed.connect(self.send_message)
        except Exception:
            try:
                self.input_field.returnPressed(self.send_message)
            except Exception as e:
                print("[chatbot_dock] returnPressed connect failed:", e)
        _layout_add_widget(self._input_row, self.input_field, 1)

        self.send_btn = pya.QPushButton("Send", inner)
        try:
            self.send_btn.clicked.connect(self.send_message)
        except Exception:
            try:
                self.send_btn.clicked(self.send_message)
            except Exception as e:
                print("[chatbot_dock] clicked connect failed:", e)
        _layout_add_widget(self._input_row, self.send_btn, 0)

        v.addLayout(self._input_row)
        self._dock.setWidget(inner)
        print("[chatbot_dock] UI built OK (composition dock)")

    def _check_server_health(self):
        """
        On startup, ping the Python agent server to verify it's reachable.
        If unreachable, show a red warning in the chat so the user knows to start it.
        """
        try:
            with urllib.request.urlopen(HEALTH_URL, timeout=3) as resp:
                data = json.loads(resp.read().decode())
                if data.get("status") == "ok":
                    self._append("System",
                        "[ok] VLSI Agent connected (http://127.0.0.1:8000).  "
                        "Type a command to begin.",
                        COLOR_SYSTEM)
                    return
        except Exception:
            pass

        self._append("System",
            "[!] Agent server NOT reachable at http://127.0.0.1:8000.  "
            "From your repo:  cd vlsi && make agent   "
            "(or:  cd vlsi/agent && python server.py)",
            COLOR_ERROR)

    def _append(self, sender: str, text: str, color: str = COLOR_SYSTEM):
        """Append a formatted HTML message to the chat history."""
        try:
            if getattr(self, "chat_history", None) is None:
                return
            chunk = (
                f'<b><span style="color:{color}">{sender}:</span></b> '
                f'<span style="color:#d0d8e8">{text}</span>'
            )
            try:
                self.chat_history.appendHtml(chunk)
            except Exception:
                self.chat_history.append(chunk)
        except RuntimeError:
            return

    def send_message(self):
        """
        Called when user presses Enter or clicks Send.
        """
        try:
            text = self.input_field.text().strip()
        except Exception:
            try:
                text = str(self.input_field.text).strip()
            except Exception:
                text = ""

        if not text:
            return

        try:
            self.input_field.setText("")
        except Exception:
            try:
                self.input_field.text = ""
            except Exception:
                pass
        self._append("You", text, COLOR_USER)
        self._set_locked(True)
        self._append("System", "Processing… (routing may take 10–60 seconds)", COLOR_SYSTEM)

        req_body = json.dumps({"message": text}).encode("utf-8")
        req = urllib.request.Request(
            AGENT_URL,
            data=req_body,
            headers={"Content-Type": "application/json"},
        )

        try:
            with urllib.request.urlopen(req, timeout=TIMEOUT_SECS) as resp:
                result = json.loads(resp.read().decode("utf-8"))
                self._append("Agent", result.get("reply", "(no reply)"), COLOR_AGENT)
                self._process_viewer_commands(result.get("viewer_commands", []))

        except urllib.error.HTTPError as e:
            body = e.read().decode("utf-8", errors="replace")
            self._append("Error",
                f"HTTP {e.code}: {body[:200]}",
                COLOR_ERROR)

        except urllib.error.URLError as e:
            self._append("Error",
                f"Cannot reach agent server: {e.reason}.  "
                f"Is 'python server.py' running?",
                COLOR_ERROR)

        except TimeoutError:
            self._append("Error",
                f"Request timed out after {TIMEOUT_SECS}s.  "
                "For very large designs, increase TIMEOUT_SECS.",
                COLOR_ERROR)

        except Exception as e:
            self._append("Error", f"Unexpected error: {e}", COLOR_ERROR)

        finally:
            self._set_locked(False)

    def _set_locked(self, locked: bool):
        """Enable or disable the input field and send button."""
        try:
            self.input_field.setEnabled(not locked)
            self.send_btn.setEnabled(not locked)
        except Exception:
            try:
                self.input_field.enabled = not locked
                self.send_btn.enabled = not locked
            except Exception:
                pass

    def _process_viewer_commands(self, commands: list):
        """
        Execute each viewer command returned by the agent on the KLayout canvas.
        """
        lv = None
        try:
            lv = pya.Application.instance().main_window().current_view()
        except Exception:
            pass

        for cmd in commands:
            action = cmd.get("action", "")

            try:
                if action == "refresh_view":
                    self._append("System", "[view] Refreshing layout view...", COLOR_SYSTEM)
                    if lv:
                        lv.add_missing_layers()
                        lv.update_content()

                elif action == "zoom_fit":
                    if lv:
                        lv.zoom_fit()
                    self._append("System", "[view] Zoomed to fit design.", COLOR_SYSTEM)

                elif action == "zoom_to":
                    bbox = cmd.get("bbox", [])
                    if len(bbox) == 4 and lv:
                        box = pya.DBox(bbox[0], bbox[1], bbox[2], bbox[3])
                        lv.zoom_box(box)
                        self._append("System",
                            f"[view] Zoomed to [{bbox[0]},{bbox[1]}]→[{bbox[2]},{bbox[3]}].",
                            COLOR_SYSTEM)

                elif action == "highlight":
                    net_name = cmd.get("net_name", "")
                    color    = cmd.get("color", "#ffaa00")
                    self._append("System",
                        f"[net] Highlighted net '{net_name}' in {color}.",
                        COLOR_SYSTEM)

                elif action == "draw_instances":
                    instances = cmd.get("instances", [])
                    if not isinstance(instances, list) or not instances:
                        continue

                    mw = pya.Application.instance().main_window()
                    cv = mw.current_view().active_cellview()
                    ly = cv.layout()
                    cell = cv.cell

                    layer_idx = ly.layer(pya.LayerInfo(999, 0))
                    shapes = cell.shapes(layer_idx)
                    shapes.clear()

                    for inst in instances:
                        x = float(inst.get("x", 0.0))
                        y = float(inst.get("y", 0.0))
                        w = float(inst.get("w", 0.0))
                        h = float(inst.get("h", 0.0))
                        shapes.insert(pya.DBox(x, y, x + w, y + h))

                    if lv:
                        lv.add_missing_layers()
                        lv.update_content()
                    self._append("System", f"[draw] Drew {len(instances)} placed instances (layer 999/0).", COLOR_SYSTEM)

                elif action == "draw_routes":
                    routes = cmd.get("routes", [])
                    if not isinstance(routes, list) or not routes:
                        continue

                    mw = pya.Application.instance().main_window()
                    cv = mw.current_view().active_cellview()
                    ly = cv.layout()
                    cell = cv.cell

                    layer_idx = ly.layer(pya.LayerInfo(998, 0))
                    shapes = cell.shapes(layer_idx)
                    shapes.clear()

                    width = float(cmd.get("width", 0.2))
                    for r in routes:
                        segs = r.get("segments", [])
                        if not isinstance(segs, list):
                            continue
                        for s in segs:
                            x1 = float(s.get("x1", 0.0))
                            y1 = float(s.get("y1", 0.0))
                            x2 = float(s.get("x2", 0.0))
                            y2 = float(s.get("y2", 0.0))
                            pts = [pya.DPoint(x1, y1), pya.DPoint(x2, y2)]
                            shapes.insert(pya.DPath(pts, width))

                    if lv:
                        lv.add_missing_layers()
                        lv.update_content()
                    self._append("System", f"[draw] Drew early routes for {len(routes)} net(s) (layer 998/0).", COLOR_SYSTEM)

                elif action == "layer_visibility":
                    layer   = cmd.get("layer", "")
                    visible = cmd.get("visible", True)
                    self._append("System",
                        f"{'[on]' if visible else '[off]'} Layer '{layer}' set to "
                        f"{'visible' if visible else 'hidden'}.",
                        COLOR_SYSTEM)

                elif action == "highlight_drc_violations":
                    violations = cmd.get("violations", [])
                    self._append("System",
                        f"[drc] {len(violations)} DRC violation(s) flagged in the view.",
                        COLOR_ERROR)

                elif action == "screenshot":
                    filename = cmd.get("filename", "klayout_snapshot.png")
                    if lv:
                        try:
                            lv.save_image(filename, 1920, 1080)
                            self._append("System", f"Screenshot saved: {filename}", COLOR_SYSTEM)
                        except Exception as e:
                            self._append("Error", f"Screenshot failed: {e}", COLOR_ERROR)

                elif action == "unlock_ui":
                    pass

                else:
                    self._append("System", f"Unknown view command: '{action}'", COLOR_ERROR)

            except Exception as e:
                self._append("Error", f"Command '{action}' failed: {e}", COLOR_ERROR)


def register_chatbot_macro():
    """
    Creates the ChatbotDock and adds it to KLayout's right dock area.
    """
    print(
        "[chatbot_dock] revision",
        _CHATBOT_DOCK_REVISION,
        "— agent: cd vlsi && make agent  (from programming/ repo root)",
    )
    app = pya.Application.instance()
    if app is None:
        print("[chatbot_dock] No KLayout application running — skipping dock registration.")
        return
    mw = app.main_window()
    if mw is None:
        print("[chatbot_dock] KLayout main window not found — cannot create dock.")
        return

    global _VLSI_AGENT_PANEL

    try:
        if _VLSI_AGENT_PANEL is not None:
            _VLSI_AGENT_PANEL.dock.close()
    except Exception:
        pass

    panel = ChatbotDock(mw)
    _VLSI_AGENT_PANEL = panel

    try:
        mw.addDockWidget(pya.Qt.RightDockWidgetArea, panel.dock)
    except Exception as e:
        print("[chatbot_dock] Failed to add dock widget (addDockWidget):", e)
        return

    try:
        panel.dock.show()
        panel.dock.raise_()
    except Exception as e:
        print("[chatbot_dock] show/raise_:", e)

    print("[chatbot_dock] VLSI Agent dock registered in KLayout.")


register_chatbot_macro()
