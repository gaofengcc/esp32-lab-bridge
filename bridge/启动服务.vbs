' 网络共享路径 (\\192.168.x.x\...) 请双击本文件启动串口桥
Option Explicit

Dim shell, fso, scriptDir, launcher

Set shell = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")
scriptDir = fso.GetParentFolderName(WScript.ScriptFullName)
launcher = fso.BuildPath(scriptDir, "start_win.vbs")

If Not fso.FileExists(launcher) Then
    MsgBox "未找到 start_win.vbs: " & launcher, vbCritical, "ESP32 Lab Bridge"
    WScript.Quit 1
End If

shell.Run "wscript.exe """ & launcher & """", 1, False
