param([int]$ProcId = 0, [string]$ProcessName = 'xemu', [int]$Mute = 1, [int]$RetrySeconds = 0, [double]$Volume = -1)
# Mute (or unmute) one process's Windows audio session on the default output device, so a
# background oracle run (xemu) is silent for the user while QEMU's own wavcapture still records it.
# Only that process's session is touched; nothing persists beyond the session's lifetime.
Add-Type -TypeDefinition @"
using System; using System.Runtime.InteropServices;
[ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")] class MMDeviceEnumeratorCom {}
[ComImport, Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMMDeviceEnumerator { int EnumAudioEndpoints(int f, int s, out IntPtr c); int GetDefaultAudioEndpoint(int flow, int role, out IMMDevice d); }
[ComImport, Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMMDevice { int Activate(ref Guid iid, int ctx, IntPtr p, [MarshalAs(UnmanagedType.IUnknown)] out object o); }
[ComImport, Guid("77AA99A0-1BD6-484F-8BC7-2C654C9A9B6F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IAudioSessionManager2 { int GetAudioSessionControl(IntPtr g, int s, out IntPtr c); int GetSimpleAudioVolume(IntPtr g, int s, out IntPtr v); int GetSessionEnumerator(out IAudioSessionEnumerator e); }
[ComImport, Guid("E2F5BB11-0570-40CA-ACDD-3AA01277DEE8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IAudioSessionEnumerator { int GetCount(out int n); int GetSession(int i, out IAudioSessionControl2 s); }
[ComImport, Guid("BFB7FF88-7239-4FC9-8FA2-07C950BE9C6D"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IAudioSessionControl2 {
  int GetState(out int s); int GetDisplayName([MarshalAs(UnmanagedType.LPWStr)] out string n); int SetDisplayName(string n, IntPtr g);
  int GetIconPath([MarshalAs(UnmanagedType.LPWStr)] out string p); int SetIconPath(string p, IntPtr g); int GetGroupingParam(out Guid g); int SetGroupingParam(ref Guid g, IntPtr c);
  int RegisterAudioSessionNotification(IntPtr n); int UnregisterAudioSessionNotification(IntPtr n);
  int GetSessionIdentifier([MarshalAs(UnmanagedType.LPWStr)] out string s); int GetSessionInstanceIdentifier([MarshalAs(UnmanagedType.LPWStr)] out string s);
  int GetProcessId(out uint pid); int IsSystemSoundsSession(); int SetDuckingPreference(bool b); }
[ComImport, Guid("87CE5498-68D6-44E5-9215-6DA47EF883D8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface ISimpleAudioVolume { int SetMasterVolume(float v, IntPtr g); int GetMasterVolume(out float v); int SetMute(bool m, IntPtr g); int GetMute(out bool m); }
public static class AppMute {
  public static int Apply(uint[] pids, bool mute, float volume) {
    var en = (IMMDeviceEnumerator)new MMDeviceEnumeratorCom(); IMMDevice dev; en.GetDefaultAudioEndpoint(0, 1, out dev);
    Guid iid = typeof(IAudioSessionManager2).GUID; object o; dev.Activate(ref iid, 23, IntPtr.Zero, out o);
    var mgr = (IAudioSessionManager2)o; IAudioSessionEnumerator se; mgr.GetSessionEnumerator(out se); int n; se.GetCount(out n); int done = 0;
    for (int i = 0; i < n; i++) { IAudioSessionControl2 s; se.GetSession(i, out s); uint pid; s.GetProcessId(out pid);
      if (Array.IndexOf(pids, pid) < 0) continue; var v = (ISimpleAudioVolume)s; v.SetMute(mute, IntPtr.Zero); if (volume >= 0) v.SetMasterVolume(volume, IntPtr.Zero); done++; }
    return done; }
}
"@
$deadline = (Get-Date).AddSeconds($RetrySeconds)
do {
    $pids = if ($ProcId) { @([uint32]$ProcId) } else { @(Get-Process $ProcessName -ErrorAction SilentlyContinue | ForEach-Object { [uint32]$_.Id }) }
    $n = if ($pids.Count) { [AppMute]::Apply([uint32[]]$pids, [bool]$Mute, [float]$Volume) } else { 0 }
    if ($n) { "$(Get-Date -Format HH:mm:ss) mute=$Mute volume=$Volume applied to $n session(s) of pids $($pids -join ',')"; break }
    Start-Sleep -Milliseconds 150
} while ((Get-Date) -lt $deadline)
if (-not $n) { "no audio session found for $ProcessName/$ProcId" }
