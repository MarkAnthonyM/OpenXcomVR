@echo off
cd /d "%~dp0"
rem the sky debug panel on the left wrist (the setting is saved: Play VR.bat keeps it on until vrDebugPanel is set to false in options.cfg)
start "" openxcom.exe -vrMode 1 -vrDebugPanel true
