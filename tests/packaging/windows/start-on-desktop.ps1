# Runs a command line as SYSTEM on the console user's desktop, as UAC runs an elevated setup, and writes its exit code to -ExitFile.
# Elevated Winix jobs run in session 0 inside a job object that may not create processes in another session: start this through WMI.
param(
    [Parameter(Mandatory)][string]$EncodedCommandLine,
    [Parameter(Mandatory)][string]$ExitFile,
    [int]$TimeoutSeconds = 600
)
$ErrorActionPreference = 'Stop'
$commandLine = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($EncodedCommandLine))

Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
public static class HsDesktop {
    [DllImport("kernel32.dll")] static extern IntPtr GetCurrentProcess();
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll")] static extern int WTSGetActiveConsoleSessionId();
    [DllImport("kernel32.dll")] static extern uint WaitForSingleObject(IntPtr h, uint ms);
    [DllImport("kernel32.dll")] static extern bool GetExitCodeProcess(IntPtr h, out int code);
    [DllImport("advapi32.dll", SetLastError = true)] static extern bool OpenProcessToken(IntPtr process, uint access, out IntPtr token);
    [DllImport("advapi32.dll", SetLastError = true)] static extern bool DuplicateTokenEx(IntPtr token, uint access, IntPtr attributes, int level, int type, out IntPtr result);
    [DllImport("advapi32.dll", SetLastError = true)] static extern bool SetTokenInformation(IntPtr token, int cls, ref int info, int len);
    [DllImport("advapi32.dll", SetLastError = true, CharSet = CharSet.Unicode)] static extern bool LookupPrivilegeValueW(string system, string name, out long luid);
    [StructLayout(LayoutKind.Sequential, Pack = 4)] struct OnePrivilege { public int count; public long luid; public int attributes; }
    [DllImport("advapi32.dll", SetLastError = true)] static extern bool AdjustTokenPrivileges(IntPtr token, bool disableAll, ref OnePrivilege state, int len, IntPtr previous, IntPtr returned);
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct StartupInfo { public int cb; public string reserved, desktop, title; public int x, y, w, h, cx, cy, fill, flags; public short show, r2; public IntPtr r3, stdin, stdout, stderr; }
    [StructLayout(LayoutKind.Sequential)]
    struct ProcessInfo { public IntPtr process, thread; public int pid, tid; }
    [DllImport("advapi32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern bool CreateProcessAsUserW(IntPtr token, string app, System.Text.StringBuilder cmd, IntPtr pa, IntPtr ta, bool inherit, uint flags, IntPtr env, string dir, ref StartupInfo si, out ProcessInfo pi);

    static void Fail(string call) {
        int error = Marshal.GetLastWin32Error();
        throw new Win32Exception(error, call + ": " + new Win32Exception(error).Message);
    }
    static void Enable(IntPtr token, string name) {
        var p = new OnePrivilege { count = 1, attributes = 2 };
        if (!LookupPrivilegeValueW(null, name, out p.luid) || !AdjustTokenPrivileges(token, false, ref p, 0, IntPtr.Zero, IntPtr.Zero) || Marshal.GetLastWin32Error() != 0) Fail(name);
    }
    public static IntPtr Start(string commandLine) {
        IntPtr own, token;
        if (!OpenProcessToken(GetCurrentProcess(), 0x1 | 0x2 | 0x8 | 0x20 | 0x80 | 0x100, out own)) Fail("OpenProcessToken");
        try {
            foreach (var name in new[] { "SeTcbPrivilege", "SeAssignPrimaryTokenPrivilege", "SeIncreaseQuotaPrivilege" }) Enable(own, name);
            if (!DuplicateTokenEx(own, 0x02000000, IntPtr.Zero, 2, 1, out token)) Fail("DuplicateTokenEx");
        } finally { CloseHandle(own); }
        try {
            int session = WTSGetActiveConsoleSessionId();
            if (!SetTokenInformation(token, 12, ref session, 4)) Fail("SetTokenInformation");
            var si = new StartupInfo { cb = Marshal.SizeOf(typeof(StartupInfo)), desktop = "winsta0\\default" };
            ProcessInfo pi;
            if (!CreateProcessAsUserW(token, null, new System.Text.StringBuilder(commandLine), IntPtr.Zero, IntPtr.Zero, false, 0, IntPtr.Zero, null, ref si, out pi))
                Fail("CreateProcessAsUserW");
            CloseHandle(pi.thread);
            return pi.process;
        } finally { CloseHandle(token); }
    }
    public static int Wait(IntPtr process, uint ms) {
        if (WaitForSingleObject(process, ms) != 0) return -1;
        int code;
        GetExitCodeProcess(process, out code);
        return code;
    }
}
'@

try {
    $process = [HsDesktop]::Start($commandLine)
    $code = [HsDesktop]::Wait($process, [uint32]($TimeoutSeconds * 1000))
    Set-Content -LiteralPath $ExitFile -Value $(if ($code -lt 0) { 'timeout' } else { $code })
} catch {
    Set-Content -LiteralPath $ExitFile -Value "error: $($_.Exception.Message)"
}
