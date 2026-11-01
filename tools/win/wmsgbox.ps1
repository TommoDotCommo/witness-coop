# List the top-level windows of one process (title, visible), and optionally close the ones with a given title (WM_CLOSE).
#   powershell -File wmsgbox.ps1 -ProcessId <pid> [-CloseTitle "The Witness Co-op"]
# Output: one "window|<title>|<visible>" line per window. Only the process you name is touched.
param([Parameter(Mandatory=$true)][uint32]$ProcessId, [string]$CloseTitle = '')
Add-Type -TypeDefinition @'
using System; using System.Text; using System.Collections.Generic; using System.Runtime.InteropServices;
public class WinList {
    public delegate bool CB(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] static extern bool EnumWindows(CB c, IntPtr l);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint p);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    public static List<string> Of(uint pid, string close) {
        var r = new List<string>();
        EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); if (p != pid) return true;
            var sb = new StringBuilder(256); GetWindowText(h, sb, 256);
            r.Add("window|" + sb.ToString() + "|" + (IsWindowVisible(h) ? "visible" : "hidden"));
            if (close.Length > 0 && sb.ToString() == close) PostMessage(h, 0x10, IntPtr.Zero, IntPtr.Zero);
            return true; }, IntPtr.Zero);
        return r;
    }
}
'@
[WinList]::Of($ProcessId, $CloseTitle) | ForEach-Object { $_ }
