import json
import websockets
from typing import TypedDict, Annotated, Sequence
import operator

from langchain_core.messages import BaseMessage, AIMessage
from langgraph.graph import StateGraph, START, END

class RouterState(TypedDict):
    """State for the routing subgraph."""
    messages: Annotated[Sequence[BaseMessage], operator.add]
    routing_status: str

async def mcp_request_node(state: RouterState):
    """
    Connects via WebSocket to the C++ Master Daemon.
    """
    print("[Router Subgraph] Connecting to C++ EDA Daemon over ws://127.0.0.1:8080...")
    
    payload = json.dumps({
        "jsonrpc": "2.0",
        "method": "route_nets",
        "params": {}
    })
    
    response_msg = "Failed to connect to C++ daemon."
    
    try:
        async with websockets.connect("ws://127.0.0.1:8080") as ws:
            print(f"[Router Subgraph] Sending JSON-RPC: {payload}")
            await ws.send(payload)
            response = await ws.recv()
            print(f"[Router Subgraph] Received RPC response: {response}")
            
            res_data = json.loads(response)
            if "result" in res_data:
                response_msg = f"C++ daemon successfully completed routing using engine: {res_data['result'].get('engine')}"
            elif "error" in res_data:
                response_msg = f"C++ daemon routing error: {res_data['error']}"
                
    except Exception as e:
        print(f"[Router Subgraph] WebSocket Error: {e}")
        response_msg = f"Exception: {str(e)}"
    
    return {
        "routing_status": "completed",
        "messages": [AIMessage(content=response_msg)]
    }

def create_router_subgraph():
    """Builds the router subgraph workflow."""
    workflow = StateGraph(RouterState)
    
    workflow.add_node("mcp_request", mcp_request_node)
    
    workflow.add_edge(START, "mcp_request")
    workflow.add_edge("mcp_request", END)
    
    return workflow.compile()
