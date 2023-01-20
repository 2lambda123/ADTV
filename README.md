# ADTV

Active Directory Toplogy Visualizer

Joseph Ryan Ries - 2023

**Currently early alpha work in progress!**

This app is designed to discover the sites, domain controllers, replication connections, etc., in your 
Active Directory environment and display them in a visual way.

Registry Settings:

HKCU\SOFTWARE\ADTV =>
- LogLevel (DWORD) 0-4
If 0 or not present, nothing is logged. 1 = log errors only. 2 = log warnings and errors. 3 = log everything.
- ResolutionIndex (DWORD) 0-17
If 0 or not present, resolution will be detected automatically. All other values indicate other render resolutions.
- FontFace (String)
If not present, the default font of Consolas is used.
- DomainController (String)
If not present, a domain controller to use for discovery will be located automatically.
