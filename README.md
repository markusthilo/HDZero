# HDZero
### --- Deprecated project - transitioned into FallbackImager, module: HdZero (hdzero.py, zerod.c) ---
## Application with a simple GUI to securely erase disks
In 1-pass mode, zeros are written to an entire disk (also partition table, slacks, etc.). The 2-pass option adds a first pass, writing a random block over the existing data (for the paranoid...). A verify option allows you to verify that each byte is zero after erasing. A new partition table and a new primary partition can be created automatically. It is also possible to have HDZero write a log to the new partition. This feature is aimed at professional environments where data protection requirements must be met.

The overwriting of files with the tool serves as a kind of test function. This will not erase any traces left anywhere in the file system.

To maximize performance, the best block size for writing data to the drive is tested (with block size set to >>auto<<).

HDZero is written for Windows 10 and 11. It might also work on other Windows versions. There is no port to Linux or MacOS (I might write something for Linux based systems but as an independent project).
### How to
Gui and main components are written in Python (3.11) using CTk. The core command line tool zerod is written in C for MinGW. An independent package with an executable hdzero.exe can be built with make-exe.py. The executable file must be started with administrator rights.

In the future I will publish a ready-to-use version here on GitHub.
### Disclaimer
I am not responsible for any loss of data caused by HDZero. Obviously, the tool is dangerous as its purpose is to delete data.

Respect the GPL 3 license.

Markus Thilo

markus.thilo@gmail.com
https://github.com/markusthilo
