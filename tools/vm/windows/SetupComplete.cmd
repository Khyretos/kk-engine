@echo off
rem Windows runs this once as SYSTEM when installing is done (autounattend.xml).
if not exist C:\kke mkdir C:\kke
powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\kke\setup\kke-setup.ps1 > C:\kke\setup-log.txt 2>&1
