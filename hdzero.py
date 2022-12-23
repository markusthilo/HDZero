#!/usr/bin/env python3
# -*- coding: utf-8 -*-

__author__ = 'Markus Thilo'
__version__ = '0.1_2022-12-20'
__license__ = 'GPL-3'
__email__ = 'markus.thilo@gmail.com'
__status__ = 'Under Construction'
__description__ = 'Wipe data'

from pathlib import Path
from configparser import ConfigParser
from wmi import WMI
from functools import partial
from subprocess import Popen, PIPE
from time import sleep
from customtkinter import CTk
from customtkinter import set_appearance_mode
from customtkinter import set_default_color_theme
from customtkinter import CTkFrame, CTkButton, CTkLabel
from customtkinter import CTkEntry, CTkRadioButton
from customtkinter import CTkCheckBox
from tkinter import StringVar, BooleanVar, PhotoImage
from tkinter.messagebox import askquestion
from tkinter.filedialog import askopenfilenames

class Config(ConfigParser):
	'Handle config file hdprepare.conf'

	def __init__(self, path):
		'Set path config file by giving app directory'
		self.path = path
		super().__init__()

	def read(self):
		'Open config file'
		super().read(self.path)

	def write(self):
		'Update config file'
		with open(self.path, 'w') as fh:
			super().write(fh)

class WinUtils:
	'Needed Windows functions'

	def __init__(self):
		'Generate Windows tools'
		self.conn = WMI()

	def list_drives(self):
		'Use DiskDrive'
		drives = { drive.Index: drive for drive in self.conn.Win32_DiskDrive() }
		for i in sorted(drives.keys()):
			yield drives[i]

	def get_drive(self, diskindex):
		for drive in self.conn.Win32_DiskDrive():
			if drive.index == diskindex:
				return drive

	def list_partitions(self, diskindex):
		print('########## DiskDrive: ##########')
		for drive in self.conn.Win32_DiskDrive():
			print(drive)
		print('########## Volume: ##########')
		for vol in self.conn.Win32_Volume():
			print(vol)
		print('########## LogicalDiskToPartition: ##########')
		for part in self.conn.Win32_LogicalDiskToPartition():
			print(part)
		print('########## DiskDriveToDiskPartition: ##########')
		for part in self.conn.Win32_DiskDriveToDiskPartition():
			print(part)
		print('########################################')
		for part in self.conn.Win32_LogicalDiskToPartition():
			if part.Antecedent.DiskIndex == diskindex:
				yield part

class ZeroD:
	'Use zerod.exe'

	def __init__(self, path, dummy=False):
		'Generate Object with the desired functions'
		self.exe_path = path
		self.dummy_write = dummy
		self.extra_wipe = False

	def launch_zproc(self, targetpath, targetsize=None, blocksize=None):
		'Set file or drive to write to'
		cmd = [self.exe_path, targetpath]
		if targetsize:
			cmd.append(str(targetsize))
		if blocksize:
			cmd.append(str(blocksize))
		if self.extra_wipe:
			cmd.append('/x')
		if self.dummy_write:
			cmd.append('/d')
		self.zproc = Popen(cmd, stdout=PIPE, bufsize=1, universal_newlines=True)	#, stderr=PIPE)

	def watch_zproc(self):
		'Communicate with running zerod.exe'
		for line in self.zproc.stdout:
			print(line, end='')
		#while self.zproc.poll() == None:
			#stdout, stderr = self.zproc.communicate()
			#print('running:', self.zproc.poll())
			#print('stdout:', stdout)
			sleep(0.5)
		
		
		# self.dec(stdout), self.dec(stderr)
		# log.write(proc.stdout.read())

class Gui(CTk, WinUtils, ZeroD):
	'GUI look and feel'

	CONFIG = 'hdzero.conf'
	ZEROD = 'zerod.exe'
	APPICON = 'icon.png'
	PAD = 10
	SLIMPAD = 4
	SIZEBASE = (
		{ 'PiB': 2**50, 'TiB': 2**40, 'GiB': 2**30, 'MiB': 2**20, 'kiB': 2**10 },
		{ 'PB': 10**15, 'TB': 10**12, 'GB': 10**9, 'MB': 10**6, 'kB': 10**3 }
	)

	def __init__(self):
		'Base Configuration'
		parentpath = Path(__file__).parent
		self.conf = Config(parentpath/self.CONFIG)
		self.conf.read()
		ZeroD.__init__(self, parentpath/self.ZEROD, dummy = self.conf['DEFAULT']['extra'])
		WinUtils.__init__(self)
		self.settings = dict()
		CTk.__init__(self)
		set_appearance_mode(self.conf['APPEARANCE']['mode'])
		set_default_color_theme(self.conf['APPEARANCE']['color_theme'])
		self.title(self.conf['TEXT']['title'])
		self.iconphoto(False, PhotoImage(file=parentpath/self.APPICON))
		self.mainframe()

	def readable(self, size):
		'Genereate readable size string'
		try:
			size = int(size)
		except TypeError:
			return self.conf['TEXT']['undetected']
		outstr = f'{size} B'
		for base in self.SIZEBASE:
			for apx, b in base.items():
				res = size/b
				rnd = round(res, 2)
				if rnd >= 1:
					outstr += ', '
					if res != rnd:
						outstr += self.conf['TEXT']['approx'] + ' '
					outstr += f'{rnd} {apx}'
					break
		return outstr

	def mainframe(self):
		'Define Main Frame'
		self.main_frame = CTkFrame(self)
		self.main_frame.pack()
		### WIPE DRIVE ###
		self.drive_frame = CTkFrame(self.main_frame)
		self.drive_frame.pack(padx=self.PAD, pady=self.PAD)
		opt_frame = CTkFrame(self.drive_frame)
		opt_frame.pack(padx=self.PAD, pady=self.PAD, fill='both', expand=True)
		labeltext = self.conf['TEXT']['volname']
		CTkLabel(opt_frame, text=f'{labeltext}:').pack(
			padx=self.PAD, pady=self.PAD, side='left')
		self.settings['volname'] = StringVar(value=self.conf['DEFAULT']['volname'])
		CTkEntry(opt_frame, textvariable=self.settings['volname']).pack(
			padx=self.PAD, pady=self.PAD, side='left')
		self.settings['fs'] = StringVar(value=self.conf['DEFAULT']['fs'])
		CTkRadioButton(master=opt_frame, variable=self.settings['fs'], value='NTFS', text='NTFS').pack(
			padx=self.PAD, pady=self.PAD, side='left')
		CTkRadioButton(master=opt_frame, variable=self.settings['fs'], value='exFAT', text='exFAT').pack(
			padx=self.PAD, pady=self.PAD, side='left')
		CTkRadioButton(master=opt_frame, variable=self.settings['fs'], value='FAT', text='FAT').pack(
			padx=self.PAD, pady=self.PAD, side='left')
		self.settings['writelog'] = BooleanVar(value=self.conf['DEFAULT']['writelog'])
		CTkCheckBox(master=opt_frame, text=self.conf['TEXT']['writelog'], variable=self.settings['writelog'],
			onvalue=True, offvalue=False).pack(padx=self.PAD, pady=self.PAD, side='left')
		CTkButton(opt_frame, text=self.conf['TEXT']['refresh'], command=self.refresh).pack(
			padx=self.PAD, pady=self.PAD, side='left')
		labeltext = self.conf['TEXT']['wipedrive']
		for drive in self.list_drives():
			frame = CTkFrame(self.drive_frame)
			frame.pack(padx=self.PAD, pady=(0, self.PAD), fill='both', expand=True)
			CTkButton(frame, text=f'{labeltext} {drive.Index}', command=partial(self.wipe_disk, drive.Index)).pack(
				padx=self.PAD, pady=self.SLIMPAD, side='left')
			CTkLabel(frame, text=f'{drive.Caption}, {drive.MediaType} ({self.readable(drive.Size)})').pack(
				padx=self.PAD, pady=self.SLIMPAD, anchor='w')
		### WIPE FILE(S) ###
		self.file_frame = CTkFrame(self.main_frame)
		self.file_frame.pack(padx=self.PAD, pady=self.PAD, fill='both', expand=True)
		frame = CTkFrame(self.file_frame)
		frame.pack(padx=self.PAD, pady=self.PAD, fill='both', expand=True)
		CTkButton(frame, text=self.conf['TEXT']['wipefile'], command=self.wipe_file).pack(
			padx=self.PAD, pady=self.PAD, side='left')
		self.settings['deletefile'] = BooleanVar(value=self.conf['DEFAULT']['deletefile'])
		CTkCheckBox(master=frame, text=self.conf['TEXT']['deletefile'], variable=self.settings['deletefile'],
			onvalue=True, offvalue=False).pack(padx=self.PAD, pady=self.PAD, side='left')
		### BOTTOM ###
		frame = CTkFrame(self.main_frame)
		frame.pack(padx=self.PAD, pady=self.PAD, fill='both', expand=True)
		frame = CTkFrame(frame)
		frame.pack(padx=self.PAD, pady=self.PAD, fill='both', expand=True)
		self.settings['extra'] = BooleanVar(value=self.conf['DEFAULT']['extra'])
		CTkCheckBox(master=frame, text=self.conf['TEXT']['extra'], variable=self.settings['extra'],
			onvalue=True, offvalue=False).pack(padx=self.PAD, pady=self.PAD, side='left')
		self.settings['askmore'] = BooleanVar(value=self.conf['DEFAULT']['askmore'])
		CTkCheckBox(master=frame, text=self.conf['TEXT']['askmore'], variable=self.settings['askmore'],
			onvalue=True, offvalue=False).pack(padx=self.PAD, pady=self.PAD, side='left')
		CTkButton(frame, text=self.conf['TEXT']['quit'], command=self.quit_app).pack(
			padx=self.PAD, pady=self.PAD, side='right')

	def decode_settings(self):
		'Decode settings and write as default to config file'
		self.options = { setting: tkvalue.get() for setting, tkvalue in self.settings.items() } 
		for option, value in self.options.items():
			self.conf['DEFAULT'][option] = str(value)
		self.conf.write()

	def refresh(self):
		self.decode_settings()
		self.main_frame.destroy()
		self.mainframe()

	def quit_app(self):
		'Write config an quit'
		self.decode_settings()
		self.destroy()

	def confirm(self, question):
		'Additional Confirmations'
		question += '\n\n\n'
		question += self.conf['TEXT']['areyoushure']
		if not askquestion(self.conf['TEXT']['title'], question) == 'yes':
			return False
		if self.options['askmore'] and not (
			askquestion(self.conf['TEXT']['title'], self.conf['TEXT']['areyoureallyshure']) == 'yes'
			and askquestion(self.conf['TEXT']['title'], self.conf['TEXT']['areyoufngshure']) == 'yes'
		):
			return False
		return True

	def wipe_disk(self, diskindex):
		'Wipe selected disk'
		self.decode_settings()
		drive = self.get_drive(diskindex)
		question = self.conf['TEXT']['drivewarning']
		question += f'\n\n{drive.DeviceId}\n{drive.Caption}, {drive.MediaType}\n'
		question += self.readable(drive.Size) + '\n\n'
		mounted = ''
		for part in self.list_partitions(diskindex):
			mounted += f'\n\n{part.Dependent.DeviceID}\n{part.Antecedent.DeviceID}\n{part.Dependent.Description}\n'
			mounted += self.readable(part.Dependent.Size)
		if mounted != '':
			question += self.conf['TEXT']['mounted'] + mounted
		else:
			question += self.conf['TEXT']['nomounted']
		if self.confirm(question):
			print('Work to do')
			######
		self.refresh()

	def wipe_file(self):
		'Wipe selected file or files'
		self.decode_settings()
		files = askopenfilenames(title=self.conf['TEXT']['filestowipe'], initialdir=self.conf['DEFAULT']['initialdir'])
		if len(files) > 0:
			question = self.conf['TEXT']['filewarning']
			for file in files:
				question += f'\n{file}'
			if self.confirm(question):
				self.extra_wipe = self.options['extra']
				for file in files:
					self.launch_zproc(Path(file))
					self.watch_zproc()
					
				######
		
		self.refresh()

if __name__ == '__main__':  # start here
	Gui().mainloop()
