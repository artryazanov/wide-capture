import sys
from ctypes import *
import psutil

# Windows API constants
PROCESS_ALL_ACCESS = 0x1F0FFF
MEM_COMMIT = 0x1000
MEM_RESERVE = 0x2000
PAGE_READWRITE = 0x04

def inject(pid, dll_path):
    kernel32 = windll.kernel32
    
    # Define return types for 64-bit compatibility
    kernel32.VirtualAllocEx.argtypes = [c_void_p, c_void_p, c_size_t, c_uint32, c_uint32]
    kernel32.VirtualAllocEx.restype = c_void_p
    
    kernel32.GetModuleHandleW.argtypes = [c_wchar_p]
    kernel32.GetModuleHandleW.restype = c_void_p
    
    kernel32.GetProcAddress.argtypes = [c_void_p, c_char_p]
    kernel32.GetProcAddress.restype = c_void_p
    
    kernel32.CreateRemoteThread.argtypes = [c_void_p, c_void_p, c_size_t, c_void_p, c_void_p, c_uint32, POINTER(c_ulong)]
    kernel32.CreateRemoteThread.restype = c_void_p

    kernel32.OpenProcess.argtypes = [c_uint32, c_bool, c_uint32]
    kernel32.OpenProcess.restype = c_void_p

    kernel32.WriteProcessMemory.argtypes = [c_void_p, c_void_p, c_void_p, c_size_t, POINTER(c_size_t)]
    kernel32.WriteProcessMemory.restype = c_bool

    # Open target process
    h_process = kernel32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)
    if not h_process:
        print(f"Failed to open process {pid}")
        return False

    # Allocate memory for DLL path
    dll_path_bytes = dll_path.encode('utf-8')
    arg_address = kernel32.VirtualAllocEx(h_process, 0, len(dll_path_bytes) + 1, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
    
    # Write DLL path
    written = c_size_t(0)
    if not kernel32.WriteProcessMemory(h_process, arg_address, dll_path_bytes, len(dll_path_bytes), byref(written)):
        print("Failed to write memory")
        return False

    # Create remote thread to load library
    h_kernel32 = kernel32.GetModuleHandleW(u"kernel32.dll")
    h_loadlib = kernel32.GetProcAddress(h_kernel32, b"LoadLibraryA")
    
    thread_id = c_ulong(0)
    if not kernel32.CreateRemoteThread(h_process, None, 0, h_loadlib, arg_address, 0, byref(thread_id)):
        print("Failed to create remote thread")
        return False

    print(f"Injection successful! Thread ID: {thread_id.value}")
    return True

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python inject.py <process_name> <path_to_dll>")
        sys.exit(1)

    target_name = sys.argv[1]
    dll_path = sys.argv[2]
    
    pid = None
    for proc in psutil.process_iter():
        if proc.name() == target_name:
            pid = proc.pid
            break
            
    if pid:
        print(f"Found {target_name} at PID {pid}")
        inject(pid, dll_path)
    else:
        print(f"Process {target_name} not found")
