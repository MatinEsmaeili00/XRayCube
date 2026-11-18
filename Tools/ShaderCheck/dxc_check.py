"""Compile HLSL with the engine's libdxcompiler.dylib via ctypes.

The Mac engine install ships DXC only as a library (no dxc binary), and
ShaderCompileWorker no longer has a direct-compile mode. This drives the
library's COM-style API directly so the plugin's shaders can be checked
*before* launching the editor (a broken global shader stops the editor
from starting). Normally run through check_shaders.sh.

usage: python3 -I dxc_check.py <file.hlsl> <entry> <profile> [-DNAME=VAL ...]
env:   UE_ROOT  engine root (default /Users/Shared/Epic Games/UE_5.8)
"""
import ctypes, os, sys, uuid

UE_ROOT = os.environ.get("UE_ROOT", "/Users/Shared/Epic Games/UE_5.8")
LIB = os.path.join(UE_ROOT, "Engine/Binaries/ThirdParty/ShaderConductor/Mac/libdxcompiler.dylib")
dxc = ctypes.CDLL(LIB)

class GUID(ctypes.Structure):
    _fields_ = [("Data1", ctypes.c_uint32), ("Data2", ctypes.c_uint16), ("Data3", ctypes.c_uint16), ("Data4", ctypes.c_ubyte * 8)]
    @classmethod
    def from_str(cls, s):
        u = uuid.UUID(s)
        g = cls()
        g.Data1, g.Data2, g.Data3 = u.fields[0], u.fields[1], u.fields[2]
        for i, b in enumerate(u.bytes[8:]):
            g.Data4[i] = b
        return g

CLSID_DxcLibrary = GUID.from_str("6245d6af-66e0-48fd-80b4-4d271796748c")
IID_IDxcLibrary = GUID.from_str("e5204dc7-d18c-4c3c-bdfb-851673980fe7")
CLSID_DxcCompiler = GUID.from_str("73e22d93-e6ce-47f3-b5bf-f0664f39c1b0")
IID_IDxcCompiler = GUID.from_str("8c210bf3-011f-4422-8d70-6f9acb8db617")

HRESULT = ctypes.c_int32
dxc.DxcCreateInstance.restype = HRESULT
dxc.DxcCreateInstance.argtypes = [ctypes.POINTER(GUID), ctypes.POINTER(GUID), ctypes.POINTER(ctypes.c_void_p)]

# This DXC build: QueryInterface, AddRef, Release (no virtual dtor slots)
BASE = 3

def method(obj, index, restype, *argtypes):
    vtbl = ctypes.cast(ctypes.c_void_p.from_address(obj).value, ctypes.POINTER(ctypes.c_void_p))
    fn = ctypes.CFUNCTYPE(restype, ctypes.c_void_p, *argtypes)(vtbl[index])
    return lambda *a: fn(obj, *a)

def create(clsid, iid):
    p = ctypes.c_void_p()
    hr = dxc.DxcCreateInstance(ctypes.byref(clsid), ctypes.byref(iid), ctypes.byref(p))
    if hr != 0:
        raise SystemExit(f"DxcCreateInstance failed 0x{hr & 0xffffffff:08x}")
    return p.value

class DxcDefine(ctypes.Structure):
    _fields_ = [("Name", ctypes.c_wchar_p), ("Value", ctypes.c_wchar_p)]

def main():
    path, entry, profile = sys.argv[1:4]
    defines = [a[2:].split("=", 1) for a in sys.argv[4:] if a.startswith("-D")]
    src = open(path, "rb").read()

    lib = create(CLSID_DxcLibrary, IID_IDxcLibrary)
    comp = create(CLSID_DxcCompiler, IID_IDxcCompiler)

    # IDxcLibrary: SetMalloc, CreateBlobFromBlob, CreateBlobFromFile, CreateBlobWithEncodingFromPinned
    blob = ctypes.c_void_p()
    buf = ctypes.create_string_buffer(src, len(src))
    hr = method(lib, BASE + 3, HRESULT, ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.POINTER(ctypes.c_void_p))(
        ctypes.cast(buf, ctypes.c_void_p), len(src), 65001, ctypes.byref(blob))
    assert hr == 0, hex(hr)

    dvals = (DxcDefine * max(len(defines), 1))(*[DxcDefine(k, v) for k, v in defines])
    args = ["-HV", "2021", "-WX"] if "--werror" in sys.argv else ["-HV", "2021"]
    argv = (ctypes.c_wchar_p * len(args))(*args)
    result = ctypes.c_void_p()
    # IDxcCompiler::Compile(blob, name, entry, profile, args, argc, defines, ndefines, include, result)
    hr = method(comp, BASE + 0, HRESULT, ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_wchar_p,
                ctypes.POINTER(ctypes.c_wchar_p), ctypes.c_uint32, ctypes.POINTER(DxcDefine), ctypes.c_uint32,
                ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p))(
        blob, path, entry, profile, argv, len(args), dvals, len(defines), None, ctypes.byref(result))
    assert hr == 0, hex(hr)

    # IDxcOperationResult: GetStatus, GetResult, GetErrorBuffer
    status = HRESULT()
    method(result.value, BASE + 0, HRESULT, ctypes.POINTER(HRESULT))(ctypes.byref(status))
    errs = ctypes.c_void_p()
    method(result.value, BASE + 2, HRESULT, ctypes.POINTER(ctypes.c_void_p))(ctypes.byref(errs))
    msg = ""
    if errs.value:
        ptr = method(errs.value, BASE + 0, ctypes.c_void_p)()
        size = method(errs.value, BASE + 1, ctypes.c_size_t)()
        if ptr and size:
            msg = ctypes.string_at(ptr, size).decode("utf-8", "replace").replace("\x00", "")
    print(f"{entry} [{profile}]: {'OK' if status.value == 0 else 'FAILED'}")
    if msg.strip():
        print(msg)
    sys.exit(0 if status.value == 0 else 1)

main()
