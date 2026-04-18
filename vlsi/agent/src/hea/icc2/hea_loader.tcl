### hea_loader.tcl  —  Host Execution Agent loader for Synopsys ICC2
### ─────────────────────────────────────────────────────────────────────────
### WHAT THIS FILE DOES:
###   Bootstraps a JSON-RPC 2.0 TCP server inside ICC2's Tcl interpreter.
###   Allows the Python agent to deploy and execute Tcl EUs atomically inside
###   the live ICC2 session.
###
### LOADING:
###   In ICC2's icc2_shell or Makefile startup:
###     source /path/to/hea_loader.tcl
###
### DEPENDENCIES:
###   ICC2 2022.03 or later.  Uses standard Tcl socket API (socket, fileevent).
###   No external packages required.
###
### PROTOCOL:
###   TCP server on port 9090 (override with env EDA_HEA_PORT).
###   Non-blocking event-driven via fileevent so ICC2's event loop runs normally.
###
### JSON-RPC METHODS:
###   ping           → "pong"
###   execute_eu     → evaluates eu_source Tcl in the ICC2 context; returns output
###   status         → {is_loaded, design_name}
###   undo_last_eu   → calls undo on the last change_set

# ── Minimal JSON helpers ─────────────────────────────────────────────────────

proc _hea_json_result {id result_json} {
    return "{\"jsonrpc\":\"2.0\",\"result\":${result_json},\"id\":${id}}"
}

proc _hea_json_error {id code msg} {
    set msg [string map {"\\" "\\\\" "\"" "\\\""} $msg]
    return "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":${code},\"message\":\"${msg}\"},\"id\":${id}}"
}

proc _hea_extract_field {json field} {
    # Minimal regex-based field extraction for strings
    # Handles: "field": "value"  and  "field": number/bool
    if {[regexp "\"${field}\"\\s*:\\s*\"((?:\\\\.|\[^\"\])*)\"" $json -> value]} {
        return $value
    }
    if {[regexp "\"${field}\"\\s*:\\s*(\[0-9.truefals\]+)" $json -> value]} {
        return $value
    }
    return ""
}

# ── Request dispatcher ───────────────────────────────────────────────────────

proc _hea_handle_request {request_str} {
    set id     [_hea_extract_field $request_str "id"]
    set method [_hea_extract_field $request_str "method"]

    if {$id eq ""} { set id 0 }

    switch -- $method {
        "ping" {
            return [_hea_json_result $id "\"pong\""]
        }

        "status" {
            # Check if a design is open in ICC2
            set is_loaded false
            set design_name ""
            if {![catch {set design_name [get_attribute [current_design] full_name]}]} {
                set is_loaded true
            }
            return [_hea_json_result $id \
                "{\"is_loaded\":${is_loaded},\"design_name\":\"${design_name}\"}"]
        }

        "execute_eu" {
            set eu_source [_hea_extract_field $request_str "eu_source"]
            set eu_name   [_hea_extract_field $request_str "eu_name"]
            if {$eu_name eq ""} { set eu_name "hea_eu" }

            # Execute inside an ICC2 change_group for undo support
            set output ""
            set err_msg ""

            if {[catch {
                # Open undo group
                open_lib_in_memory -force
                # Evaluate the EU source
                set output [uplevel #0 $eu_source]
                # Commit change (close undo group handled by ICC2 implicitly)
            } err_msg]} {
                return [_hea_json_error $id -32000 $err_msg]
            }

            set output [string map {"\\" "\\\\" "\"" "\\\""} $output]
            return [_hea_json_result $id \
                "{\"status\":\"ok\",\"output\":\"${output}\"}"]
        }

        "undo_last_eu" {
            catch { undo }
            return [_hea_json_result $id "{\"status\":\"ok\"}"]
        }

        default {
            return [_hea_json_error $id -32601 "Method not found: $method"]
        }
    }
}

# ── Server plumbing ──────────────────────────────────────────────────────────

proc _hea_client_readable {sock} {
    if {[catch {gets $sock line} len] || $len < 0} {
        close $sock
        return
    }
    if {[string length $line] == 0} return
    set response [_hea_handle_request $line]
    puts $sock $response
    flush $sock
}

proc _hea_accept {chan addr port} {
    fconfigure $chan -translation lf -buffering line
    fileevent  $chan readable [list _hea_client_readable $chan]
    puts stderr "HEA: Client connected from $addr:$port"
}

proc hea_start_server {{port 9090}} {
    set env_port [env EDA_HEA_PORT 9090]
    if {$env_port ne ""} { set port $env_port }
    socket -server _hea_accept $port
    puts stderr "HEA: Listening on TCP port $port"
}

proc env {name default} {
    if {[info exists ::env($name)]} { return $::env($name) }
    return $default
}

# ── Auto-start ───────────────────────────────────────────────────────────────
puts stderr "HEA Loader (ICC2): loaded.  Call hea_start_server to listen."
hea_start_server [env EDA_HEA_PORT 9090]
