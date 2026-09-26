' UNC 网络路径下请双击本文件启动, 不要直接双击 .cmd
Option Explicit

Dim shell, fso, scriptDir, ps1Path, cmdLine

Set shell = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")
scriptDir = fso.GetParentFolderName(WScript.ScriptFullName)
ps1Path = fso.BuildPath(scriptDir, "start_win.ps1")

If Not fso.FileExists(ps1Path) Then
    MsgBox "未找到 start_win.ps1: " & ps1Path, vbCritical, "ESP32 Lab Bridge"
    WScript.Quit 1
End If

cmdLine = "powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File """ & ps1Path & """"
shell.Run cmdLine, 1, False
