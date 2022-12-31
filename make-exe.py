#!/usr/bin/env python3
# -*- coding: utf-8 -*-

__author__ = 'Markus Thilo'
__license__ = 'GPL-3'
__email__ = 'markus.thilo@gmail.com'

import PyInstaller.__main__
from pathlib import Path
from shutil import copy

__parent__ = Path(__file__).parent
__hdzero__ = __parent__/'dist\\hdzero'
__ctk__ = __parent__.parent.parent/'AppData\Local\Programs\Python\Python311\Lib\site-packages\customtkinter'
#PyInstaller.__main__.run([
print([
    'hdzero.py',
    '--onedir',
    '--windowed',
	'-i icon.ico',
	f'--add-data {__ctk__};customtkinter/'
])
for srcfile in 'zerod.exe', 'icon.png', 'hdzero.conf', 'logheader.txt':
	print(srcfile, __hdzero__/srcfile)


# C:\Users\THI\AppData\Local\Programs\Python\Python311\Lib\site-packages/customtkinter;customtkinter/