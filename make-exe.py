#!/usr/bin/env python3
# -*- coding: utf-8 -*-

__author__ = 'Markus Thilo'
__license__ = 'GPL-3'
__email__ = 'markus.thilo@gmail.com'

import PyInstaller.__main__
from pathlib import Path
from shutil import copy, rmtree

__parent__ = Path(__file__).parent
__hdzero__ = __parent__/'dist\\hdzero'
__ctk__ = __parent__.parent.parent/'AppData\Local\Programs\Python\Python311\Lib\site-packages\customtkinter'
rmtree('dist')
PyInstaller.__main__.run([
    'hdzero.py',
    '--onedir',
    '--windowed',
	'--icon', 'icon.ico',
	'--add-data', f'{__ctk__};customtkinter/'
])
for srcfile in 'zerod.exe', 'icon.png', 'hdzero.conf', 'logheader.txt':
	copy(srcfile, __hdzero__/srcfile)
rmtree('build')
