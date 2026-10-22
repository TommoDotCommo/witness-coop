# Shared by the Windows-side probes (run with Windows Python 3, started from WSL; see tools/config.sh).
# Read-only unless attach(pid, write=True). Offsets are for Steam build 11370412 (see README).
import ctypes, ctypes.wintypes as wt, struct

GLOBALS, CAM_POS, CAM_ANG = 0x62D0A0, 0x6304B0 + 0x10, 0x6303EC
TI_PANEL, TI_DOOR = 0x62E670, 0x62DE70          # type descriptors (module-relative)
MAX_ID = 0x40000

k = ctypes.WinDLL('kernel32', use_last_error=True)
k.OpenProcess.restype = wt.HANDLE

class _Module(ctypes.Structure):
    _fields_ = [('dwSize', wt.DWORD), ('th32ModuleID', wt.DWORD), ('th32ProcessID', wt.DWORD), ('GlblcntUsage', wt.DWORD),
                ('ProccntUsage', wt.DWORD), ('modBaseAddr', ctypes.c_void_p), ('modBaseSize', wt.DWORD), ('hModule', wt.HMODULE),
                ('szModule', ctypes.c_char * 256), ('szExePath', ctypes.c_char * 260)]

class Proc:
    def __init__(self, pid, write=False):
        snap = k.CreateToolhelp32Snapshot(0x8 | 0x10, pid)
        me = _Module(); me.dwSize = ctypes.sizeof(_Module); self.base = None
        ok = k.Module32First(snap, ctypes.byref(me))
        while ok:
            if me.szModule.lower() == b'witness64_d3d11.exe': self.base = me.modBaseAddr; break
            ok = k.Module32Next(snap, ctypes.byref(me))
        access = 0x0010 | 0x0400 | ((0x0020 | 0x0008) if write else 0)    # VM_READ, QUERY_INFORMATION, [VM_WRITE, VM_OPERATION]
        self.h = k.OpenProcess(access, False, pid)
    def rd(self, addr, n):
        buf = ctypes.create_string_buffer(n); got = ctypes.c_size_t()
        return buf.raw if k.ReadProcessMemory(self.h, ctypes.c_void_p(addr), buf, n, ctypes.byref(got)) else None
    def wr(self, addr, data):
        n = ctypes.c_size_t(); return bool(k.WriteProcessMemory(self.h, ctypes.c_void_p(addr), data, len(data), ctypes.byref(n)))
    def u64(self, addr):
        r = self.rd(addr, 8); return struct.unpack('<Q', r)[0] if r else None
    def u32(self, addr):
        r = self.rd(addr, 4); return struct.unpack('<I', r)[0] if r else None
    def entity_array(self):
        return self.u64(self.u64(self.base + GLOBALS) + 0x18)
    def entity_table(self):
        return struct.unpack('<%dQ' % MAX_ID, self.rd(self.entity_array(), MAX_ID * 8))
    def entity(self, eid):
        return self.u64(self.entity_array() + eid * 8)
    def type_of(self, e):
        return self.u64(e + 8) - self.base      # descriptor, module-relative
    def camera(self):
        return struct.unpack('<3f', self.rd(self.base + CAM_POS, 12)), struct.unpack('<2f', self.rd(self.base + CAM_ANG, 8))

def attach(pid, write=False): return Proc(pid, write)
