"""
Impulse Graph Engine Python SDK & Zero-Copy C-ABI Binding
"""

try:
    from _impulse_native import Snapshot, Writer
except ImportError:
    # Fallback for development / uncompiled native extension
    Snapshot = None
    Writer = None

__version__ = "2.4.0"
__all__ = ["Snapshot", "Writer"]
