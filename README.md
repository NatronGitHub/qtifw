# Qt(4) Installer Framework

This is a fork of qtifw for use with Natron.

## Build for Windows x64 (on Linux/BSD/macOS)

```
git clone https://github.com/NatronGitHub/qtifw
cd qtifw
git clone https://github.com/mxe/mxe
cd mxe
make MXE_TARGETS='x86_64-w64-mingw32.static' qt
cd ..
export PATH=`pwd`/mxe/usr/bin:$PATH
x86_64-w64-mingw32.static-qmake-qt4
make -jX
ls bin
```

### Add manifest

We have to attach a manifest to be able to get administrator privileges for the installer.

**This has to be done on MSYS2 or through WINE.**

``installerbase.exe.manifest``:
```
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0"> 
  <assemblyIdentity version="1.0.0.0" processorArchitecture="amd64" name="installerbase.exe" type="win32"/>
  <trustInfo xmlns="urn:schemas-microsoft-com:asm.v2">
    <security>
      <requestedPrivileges>
        <requestedExecutionLevel level="requireAdministrator" uiAccess="true"/>
        </requestedPrivileges>
       </security>
  </trustInfo>
</assembly>
```

```
mt.exe -manifest installerbase.exe.manifest -outputresource:"installerbase.exe;1"
```
