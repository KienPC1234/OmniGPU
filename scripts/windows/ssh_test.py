"""
Deploy guest DLL to VM and test via SSH.
Usage:
    python scripts\windows\ssh_test.py
Requires: paramiko (pip install paramiko)
"""

import paramiko
import time
import os

VM_IP = "192.168.1.113"
VM_USER = "test"
VM_PASS = "Htt@123456"
GUEST_DLL_SRC = r"C:\Users\kien\Documents\repos\OmniGPU\build\release\bin\omnigpu_guest.dll"
GUEST_DLL_DST = r"C:\Program Files\OmniGPU\x64\omnigpu_guest.dll"


def main():
    s = paramiko.SSHClient()
    s.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    s.connect(VM_IP, username=VM_USER, password=VM_PASS, timeout=15, allow_agent=False, look_for_keys=False)

    # Kill any running test apps
    s.exec_command("taskkill /F /IM vkcube.exe 2>&1")
    time.sleep(1)

    # Upload guest DLL
    sftp = s.open_sftp()
    sftp.put(GUEST_DLL_SRC, GUEST_DLL_DST)
    sftp.close()
    print(f"Uploaded {GUEST_DLL_SRC} -> {GUEST_DLL_DST}")

    # Start vkcube
    s.exec_command(
        r'start /B "" "C:\VulkanSDK\1.4.350.0\Bin\vkcube.exe"',
        timeout=10,
    )
    time.sleep(5)

    # Check status
    stdin, out, err = s.exec_command(
        'tasklist /FI "IMAGENAME eq vkcube.exe" /FO CSV /NH', timeout=10
    )
    output = out.read().decode().strip()
    print("vkcube:", "RUNNING" if output else "NOT RUNNING")
    s.close()


if __name__ == "__main__":
    main()
