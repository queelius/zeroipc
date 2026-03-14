"""
C FFI backend for atomic queue/stack operations.

Loads libzeroipc_ffi.so at import time. If not found, AVAILABLE is False
and callers fall back to pure-Python struct.pack_into (SPSC-only).
"""

import ctypes
import ctypes.util
import os

AVAILABLE: bool = False
_lib = None

# Return codes matching ffi.c
OK = 0
EMPTY_OR_FULL = -1
MISMATCH = -2


def _find_library():
    """Search for libzeroipc_ffi.so."""
    # 1. Explicit env var (set to "none" to disable)
    env_path = os.environ.get("ZEROIPC_FFI_LIB")
    if env_path is not None:
        if env_path.lower() == "none":
            return None
        if os.path.isfile(env_path):
            return env_path

    # 2. Relative to this package (installed alongside or in-tree development)
    pkg_dir = os.path.dirname(os.path.abspath(__file__))
    for candidate in [
        os.path.join(pkg_dir, "libzeroipc_ffi.so"),
        os.path.join(pkg_dir, "..", "..", "c", "libzeroipc_ffi.so"),
    ]:
        if os.path.isfile(candidate):
            return os.path.abspath(candidate)

    # 3. System library path
    return ctypes.util.find_library("zeroipc_ffi")


def _setup_functions():
    """Set argtypes/restype for all FFI functions."""
    c_void_p = ctypes.c_void_p
    c_size_t = ctypes.c_size_t
    c_uint32 = ctypes.c_uint32
    c_int = ctypes.c_int

    # Queue
    for fn_name, argtypes, restype in [
        ("zeroipc_raw_queue_push", [c_void_p, c_size_t, c_void_p, c_uint32], c_int),
        ("zeroipc_raw_queue_pop", [c_void_p, c_size_t, c_void_p, c_uint32], c_int),
        ("zeroipc_raw_queue_size", [c_void_p, c_size_t], c_int),
        ("zeroipc_raw_queue_empty", [c_void_p, c_size_t], c_int),
        ("zeroipc_raw_queue_full", [c_void_p, c_size_t], c_int),
        # Stack
        ("zeroipc_raw_stack_push", [c_void_p, c_size_t, c_void_p, c_uint32], c_int),
        ("zeroipc_raw_stack_pop", [c_void_p, c_size_t, c_void_p, c_uint32], c_int),
        ("zeroipc_raw_stack_top", [c_void_p, c_size_t, c_void_p, c_uint32], c_int),
        ("zeroipc_raw_stack_size", [c_void_p, c_size_t], c_int),
        ("zeroipc_raw_stack_empty", [c_void_p, c_size_t], c_int),
        ("zeroipc_raw_stack_full", [c_void_p, c_size_t], c_int),
    ]:
        fn = getattr(_lib, fn_name)
        fn.argtypes = argtypes
        fn.restype = restype


def _load():
    global _lib, AVAILABLE
    path = _find_library()
    if path is None:
        return

    try:
        _lib = ctypes.CDLL(path)
        _setup_functions()
        AVAILABLE = True
    except OSError:
        _lib = None


_load()


def _base_ptr(memory):
    """Get a ctypes void pointer to the mmap'd region."""
    return ctypes.c_void_p(ctypes.addressof(
        (ctypes.c_char * len(memory.mmap)).from_buffer(memory.mmap)))


# --- Queue operations ---

def queue_push(memory, offset, value_bytes, elem_size):
    base = _base_ptr(memory)
    buf = (ctypes.c_char * elem_size).from_buffer_copy(value_bytes)
    return _lib.zeroipc_raw_queue_push(base, offset, buf, elem_size)


def queue_pop(memory, offset, elem_size):
    base = _base_ptr(memory)
    buf = (ctypes.c_char * elem_size)()
    rc = _lib.zeroipc_raw_queue_pop(base, offset, buf, elem_size)
    return rc, bytes(buf) if rc == OK else None


def queue_size(memory, offset):
    return _lib.zeroipc_raw_queue_size(_base_ptr(memory), offset)


def queue_empty(memory, offset):
    return bool(_lib.zeroipc_raw_queue_empty(_base_ptr(memory), offset))


def queue_full(memory, offset):
    return bool(_lib.zeroipc_raw_queue_full(_base_ptr(memory), offset))


# --- Stack operations ---

def stack_push(memory, offset, value_bytes, elem_size):
    base = _base_ptr(memory)
    buf = (ctypes.c_char * elem_size).from_buffer_copy(value_bytes)
    return _lib.zeroipc_raw_stack_push(base, offset, buf, elem_size)


def stack_pop(memory, offset, elem_size):
    base = _base_ptr(memory)
    buf = (ctypes.c_char * elem_size)()
    rc = _lib.zeroipc_raw_stack_pop(base, offset, buf, elem_size)
    return rc, bytes(buf) if rc == OK else None


def stack_top(memory, offset, elem_size):
    base = _base_ptr(memory)
    buf = (ctypes.c_char * elem_size)()
    rc = _lib.zeroipc_raw_stack_top(base, offset, buf, elem_size)
    return rc, bytes(buf) if rc == OK else None


def stack_size(memory, offset):
    return _lib.zeroipc_raw_stack_size(_base_ptr(memory), offset)


def stack_empty(memory, offset):
    return bool(_lib.zeroipc_raw_stack_empty(_base_ptr(memory), offset))


def stack_full(memory, offset):
    return bool(_lib.zeroipc_raw_stack_full(_base_ptr(memory), offset))
