# List every active render endpoint and its audio sessions (pid, state, volume, mute, name).
Add-Type -TypeDefinition @"
using System; using System.Runtime.InteropServices;
[ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")] class MMDeviceEnumeratorCom {}
[ComImport, Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMMDeviceEnumerator { int EnumAudioEndpoints(int f, int s, out IMMDeviceCollection c); int GetDefaultAudioEndpoint(int flow, int role, out IMMDevice d); }
[ComImport, Guid("0BD7A1BE-7A1A-44DB-8397-CC5392387B5E"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMMDeviceCollection { int GetCount(out int n); int Item(int i, out IMMDevice d); }
[ComImport, Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMMDevice { int Activate(ref Guid iid, int ctx, IntPtr p, [MarshalAs(UnmanagedType.IUnknown)] out object o); int OpenPropertyStore(int a, out IPropertyStore s); int GetId([MarshalAs(UnmanagedType.LPWStr)] out string id); }
[StructLayout(LayoutKind.Sequential)] struct PROPERTYKEY { public Guid fmtid; public int pid; }
[StructLayout(LayoutKind.Explicit)] struct PROPVARIANT { [FieldOffset(0)] public short vt; [FieldOffset(8)] public IntPtr p; }
[ComImport, Guid("886D8EEB-8CF2-4446-8D02-CDBA1DBDCF99"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IPropertyStore { int GetCount(out int n); int GetAt(int i, out PROPERTYKEY k); int GetValue(ref PROPERTYKEY k, out PROPVARIANT v); }
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
public static class Sess {
  static string Name(IMMDevice d) { IPropertyStore ps; d.OpenPropertyStore(0, out ps); var k = new PROPERTYKEY { fmtid = new Guid("a45c254e-df1c-4efd-8020-67d146a850e0"), pid = 14 }; PROPVARIANT v; ps.GetValue(ref k, out v); return v.vt == 31 ? Marshal.PtrToStringUni(v.p) : "?"; }
  public static string List() {
    var sb = new System.Text.StringBuilder(); var en = (IMMDeviceEnumerator)new MMDeviceEnumeratorCom();
    IMMDevice def; en.GetDefaultAudioEndpoint(0, 1, out def); string defid; def.GetId(out defid);
    IMMDeviceCollection col; en.EnumAudioEndpoints(0, 1, out col); int n; col.GetCount(out n);
    for (int i = 0; i < n; i++) { IMMDevice d; col.Item(i, out d); string id; d.GetId(out id);
      sb.AppendLine((id == defid ? "* " : "  ") + Name(d));
      Guid iid = typeof(IAudioSessionManager2).GUID; object o; d.Activate(ref iid, 23, IntPtr.Zero, out o); var mgr = (IAudioSessionManager2)o;
      IAudioSessionEnumerator se; mgr.GetSessionEnumerator(out se); int m; se.GetCount(out m);
      for (int j = 0; j < m; j++) { IAudioSessionControl2 s; se.GetSession(j, out s); uint pid; s.GetProcessId(out pid); int st; s.GetState(out st); string nm; s.GetDisplayName(out nm);
        var v = (ISimpleAudioVolume)s; float vol; v.GetMasterVolume(out vol); bool mu; v.GetMute(out mu);
        string pn = "?"; try { pn = System.Diagnostics.Process.GetProcessById((int)pid).ProcessName; } catch {}
        sb.AppendLine(string.Format("      pid {0,6} {1,-14} state {2} vol {3:F3} mute {4} {5}", pid, pn, st == 1 ? "ACTIVE  " : st == 0 ? "inactive" : "expired ", vol, mu, nm)); }
    }
    return sb.ToString(); }
}
"@
[Sess]::List()
