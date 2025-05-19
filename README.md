# BitBlt HDR
Fixes overexposed hdr screenshot for softwares that using Bitblt api

Using DXGI Desktop Duplication API and DX11 Compute Shader, It requires GPU that supports DX11 to works properly

### Download
Go to [releases](https://github.com/GEEKiDoS/bitblt-hdr/releases)

### How to use
1. Close your screenshotter
2. Put `version.dll` next to the exe of your screenshotter
3. You are good to go

### Tested Screenshotters
1. Tencent QQ (9.9.12-26466, NT Build with screenshot code in `wrapper.node`)
2. Tencent QQ (9.7.23, old non-NT 32bit build)
    - In order to get it working, "Override high DPI scaling behavior" must set to "Application" in Compatibility setting for QQ.exe
3. Snipaste (2.10.6)

### Known Issue
1. Multi-monitor is borken
2. Screenshot under 1680x1050 is broken

### Result Image (using QQ Screenshot):
![result](./screenshot/result.jpg)

### Disclaimer
Use it with QQNT could somehow resulted in a banned account.
