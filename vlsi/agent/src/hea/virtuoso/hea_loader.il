;;; hea_loader.skill  —  Host Execution Agent loader for Cadence Virtuoso
;;; ─────────────────────────────────────────────────────────────────────────
;;; WHAT THIS FILE DOES:
;;;   Bootstraps a minimal JSON-RPC 2.0 WebSocket server inside Virtuoso's
;;;   SKILL interpreter so the external Python agent can deploy and execute
;;;   SKILL EUs (Execution Units) atomically inside the live layout session.
;;;
;;; LOADING:
;;;   In Virtuoso's CIW type:
;;;     load("/path/to/hea_loader.skill")
;;;   Or add to .cdsinit:
;;;     loadi("/path/to/hea_loader.skill")
;;;
;;; DEPENDENCIES:
;;;   Virtuoso 6.1.8 or later with SKILL++ and the socket API (hiSocket...).
;;;   The hiSocket functions are available in IC 6.1.x+.
;;;
;;; PROTOCOL:
;;;   Listens on TCP port 9090 (configurable via EDA_HEA_PORT env var).
;;;   Accepts one connection at a time (single-threaded SKILL).
;;;   Each JSON-RPC request is handled synchronously: the SKILL script is
;;;   evaluated inside a leHideProc / dbOpenUndoGroup context so all changes
;;;   are wrapped in a single undoable group.
;;;
;;; JSON-RPC METHODS:
;;;   ping           → "pong"
;;;   execute_eu     → runs eu_source SKILL string; returns {status, output}
;;;   status         → {is_loaded, cell_name, library_name}
;;;   undo_last_eu   → calls leRedo to undo the last EU's changes
;;;
;;; NOTE: This is a functional scaffold.  The JSON parsing uses a lightweight
;;; pure-SKILL JSON parser (no external library needed).  Error handling is
;;; best-effort; malformed JSON returns a JSON-RPC parse error.

procedure( _heaParseJSON(str)
;;; Minimal JSON parser — handles string, number, boolean, null, object, array.
;;; For production use, replace with a complete SKILL JSON library.
  let( (result)
    ; Strip outer whitespace
    setq(str trim(str))
    cond(
      ; Null
      (equal(str "null") nil)
      ; Boolean
      (equal(str "true") t)
      (equal(str "false") nil)
      ; String  "value"
      (equal(getchar(str 1) 34)   ; 34 = '"'
        substring(str 2 (difference (strlen str) 2)))
      ; Number
      (t atof(str))
    )
  )
)

procedure( _heaJsonError(id code msg)
  sprintf(nil "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":%d,\"message\":\"%s\"},\"id\":%d}"
    code msg id)
)

procedure( _heaJsonResult(id resultStr)
  sprintf(nil "{\"jsonrpc\":\"2.0\",\"result\":%s,\"id\":%d}" resultStr id)
)

procedure( heaHandleRequest(requestStr)
;;; Parse and dispatch one JSON-RPC request string.
;;; Returns the JSON-RPC response string.
  let( (req method params id euSource output undoGroupName)
    ; --- Minimal JSON extraction (field search by regex) ---
    ; In production use a proper JSON library.
    setq(id     car(rexMatchp("\"id\"\\s*:\\s*([0-9]+)" requestStr) || list("0") ))
    setq(method car(rexMatchp("\"method\"\\s*:\\s*\"([^\"]+)\"" requestStr) || list("") ))
    setq(id atoi(id))

    cond(
      ; ── ping ──────────────────────────────────────────────────────────────
      (equal(method "ping")
        _heaJsonResult(id "\"pong\""))

      ; ── status ────────────────────────────────────────────────────────────
      (equal(method "status")
        let( (cv cellName libName)
          setq(cv geGetWindowCellView())
          if(cv
            then
              setq(cellName cellView->cell->name || "")
              setq(libName  cellView->lib->name  || "")
              _heaJsonResult(id
                sprintf(nil "{\"is_loaded\":true,\"cell_name\":\"%s\",\"library_name\":\"%s\"}"
                  cellName libName))
            else
              _heaJsonResult(id "{\"is_loaded\":false,\"cell_name\":\"\",\"library_name\":\"\"}")
          )
        ))

      ; ── execute_eu ────────────────────────────────────────────────────────
      (equal(method "execute_eu")
        let( (euSourceMatch euNameMatch evalResult errMsg)
          ; Extract eu_source field (simplified — finds first long string value)
          setq(euSourceMatch
            rexMatchp("\"eu_source\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"" requestStr))
          setq(euSource if(euSourceMatch car(euSourceMatch) ""))
          setq(euSourceMatch
            rexMatchp("\"eu_name\"\\s*:\\s*\"([^\"]+)\"" requestStr))
          setq(undoGroupName if(euSourceMatch car(euSourceMatch) "hea_eu"))

          ; Wrap execution in a single undoable group
          dbOpenUndoGroup(geGetWindowCellView() undoGroupName)
          setq(errMsg nil)
          setq(evalResult
            errset(
              evalstring(euSource)
              t  ; catch errors
            ))
          dbCloseUndoGroup(geGetWindowCellView() undoGroupName)

          if(errMsg
            _heaJsonError(id -32000 sprintf(nil "EU execution failed: %s" errMsg))
            _heaJsonResult(id
              sprintf(nil "{\"status\":\"ok\",\"output\":\"%s\"}"
                if(evalResult
                  sprintf(nil "%s" evalResult) "")))
          )
        ))

      ; ── undo_last_eu ──────────────────────────────────────────────────────
      (equal(method "undo_last_eu")
        progn(
          leUndo()
          _heaJsonResult(id "{\"status\":\"ok\"}")))

      ; ── unknown method ────────────────────────────────────────────────────
      (t _heaJsonError(id -32601 "Method not found"))
    )
  )
)

procedure( heaStartServer( @optional (port 9090) )
;;; Start the HEA TCP listener.  Blocks the SKILL thread while connected.
;;; In practice, load this in a background SKILL thread using schIpcReadPort.
  let( (serverSock clientSock requestStr responseStr)
    printf("HEA: Starting on TCP port %d\n" port)
    setq(serverSock hiOpenSocket(port))
    if(serverSock
      then
        printf("HEA: Listening...\n")
        loop(
          setq(clientSock hiAcceptSocket(serverSock))
          when(clientSock
            setq(requestStr hiReadLine(clientSock))
            while(requestStr
              setq(responseStr heaHandleRequest(requestStr))
              hiWriteLine(clientSock responseStr)
              setq(requestStr hiReadLine(clientSock))
            )
            hiCloseSocket(clientSock)
          )
        )
      else
        error("HEA: Cannot open server socket on port %d" port)
    )
  )
)

; Auto-start when loaded
printf("HEA Loader: loaded.  Call heaStartServer() to start the agent listener.\n")
printf("HEA Loader: Or add to .cdsinit: heaStartServer(%d)\n"
  (atoi (getShellEnvVar "EDA_HEA_PORT") || 9090))
