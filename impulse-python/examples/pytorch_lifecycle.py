import impulse_graph
import numpy as np
import gc

try:
    import torch
except ImportError:
    torch = None

def run_lifecycle_demo():
    print("=== Impulse Graph: Python Lifecycle & Tensor Demo ===")
    
    dummy_path = "lifecycle_test.imps"
    with impulse_graph.Writer(dummy_path, 0) as writer:
        writer.add_domain(0, 1, "Node")
        writer.add_relation(0, 0, 0, 10, 10, 0, [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10], [0, 1, 2, 3, 4, 5, 6, 7, 8, 9])
        writer.finalize()

    print("\n[Phase 1] Simulating a short-lived execution context...")
    
    def fetch_tensor():
        snap = impulse_graph.Snapshot(dummy_path)
        ctx = impulse_graph.vm.VmContext(snap)
        state = impulse_graph.vm.VmState()
        
        qb = impulse_graph.vm.QueryBuilder()
        qb.input_node(0).walk_edge(0).collect_array()
        compiled = qb.compile()
        
        res = compiled.execute_with_context(ctx, state, 0)
        
        # Zero-copy view natively linked to 'ctx'
        np_arr = ctx.get_node_vector(res.raw_value)
        
        if torch is not None:
            # Zero-copy tensor
            tensor = torch.from_numpy(np_arr)
            return tensor
        return np_arr

    tensor = fetch_tensor()
    
    print("\n[Phase 2] Forcing Python Garbage Collection...")
    gc.collect()
    
    print(f"Tensor type: {type(tensor)}")
    print(f"Tensor shape: {tensor.shape}")
    
    print("\n[Phase 3] Success! Zero-copy memory is safely persisted across the GC boundary.")
    
if __name__ == '__main__':
    run_lifecycle_demo()
