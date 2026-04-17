import pya
import json
import urllib.request
import urllib.error

# Note: This file is intended to be run INSIDE the KLayout Python Macro environment.

class ChatbotDock(pya.QDockWidget):
    """
    A PyQt dock widget representing the Chatbot UI inside KLayout.
    """
    def __init__(self, main_window):
        super(ChatbotDock, self).__init__("VLSI Agent", main_window)
        self.main_window = main_window
        
        # Setup UI
        self.widget = pya.QWidget()
        self.layout = pya.QVBoxLayout(self.widget)
        
        self.chat_history = pya.QTextEdit()
        self.chat_history.setReadOnly(True)
        self.layout.addWidget(self.chat_history)
        
        self.input_field = pya.QLineEdit()
        self.input_field.returnPressed(self.send_message)
        self.layout.addWidget(self.input_field)
        
        self.setWidget(self.widget)
        self.append_message("System", "VLSI Agent connected. Type your layout command (e.g., 'route the design').")

    def append_message(self, sender, text):
        color = "#4a9eff" if sender == "Agent" else "#4aff9e" if sender == "You" else "#8090a8"
        self.chat_history.appendHtml(f'<b><span style="color:{color}">{sender}:</span></b> {text}')

    def send_message(self):
        text = self.input_field.text
        if not text:
            return
            
        self.input_field.text = ""
        self.append_message("You", text)
        
        # Disable interaction in KLayout while we wait
        self.append_message("System", "Locking KLayout UI during processing...")
        self.lock_ui(True)
        
        # We must use pya.QTimer or similar for async operation to not freeze Qt if this takes a long time,
        # but for simplicity in this macro skeleton we'll do a blocking urllib request.
        # Ideally, use QThread or QNetworkAccessManager.
        
        req_data = json.dumps({"message": text}).encode('utf-8')
        req = urllib.request.Request(
            'http://127.0.0.1:8000/chat', 
            data=req_data, 
            headers={'Content-Type': 'application/json'}
        )
        
        try:
            with urllib.request.urlopen(req) as resp:
                res_data = json.loads(resp.read().decode('utf-8'))
                
                # Output agent reply
                self.append_message("Agent", res_data.get("reply", ""))
                
                # Process Viewer Commands (Window Agnosticism!)
                self.process_commands(res_data.get("viewer_commands", []))
                
        except urllib.error.URLError as e:
            self.append_message("System Error", f"Failed to connect to LangGraph server: {str(e)}")
        finally:
            self.lock_ui(False)

    def process_commands(self, commands):
        for cmd in commands:
            action = cmd.get("action")
            if action == "refresh_view":
                self.append_message("System", "Refreshing Layout View...")
                # KLayout specific refresh
                lv = pya.Application.instance().main_window().current_view()
                if lv:
                    lv.add_missing_layers()
                    lv.update_content()
            elif action == "zoom_to":
                bbox = cmd.get("bbox", [])
                if len(bbox) == 4 and lv:
                    # KLayout zoom
                    box = pya.DBox(bbox[0], bbox[1], bbox[2], bbox[3])
                    lv.zoom_box(box)
            elif action == "unlock_ui":
                # Already handled in finally block, but explicit is good
                pass

    def lock_ui(self, locked):
        """Disable the main layout view."""
        self.input_field.enabled = not locked
        lv = pya.Application.instance().main_window().current_view()
        # You could place an overlay, disable specific tools, or disable the entire main window
        # For safety, we just disable the input field and warn the user.

def register_chatbot_macro():
    app = pya.Application.instance()
    mw = app.main_window()
    if mw:
        dock = ChatbotDock(mw)
        mw.add_dock_widget(pya.Qt.RightDockWidgetArea, dock)

# Auto-start if executed from KLayout Macro Editor
register_chatbot_macro()
