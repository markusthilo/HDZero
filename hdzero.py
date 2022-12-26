#!/usr/bin/env python3
# -*- coding: utf-8 -*-

__author__ = 'Markus Thilo'
__version__ = '0.1_2022-12-26'
__license__ = 'GPL-3'
__email__ = 'markus.thilo@gmail.com'
__status__ = 'Under Construction'
__description__ = 'Wipe data'

from pathlib import Path
from configparser import ConfigParser
from wmi import WMI
from functools import partial
from subprocess import Popen, PIPE, STDOUT
from time import sleep
from datetime import datetime
from threading import Thread
from customtkinter import CTk, CTkToplevel
from customtkinter import set_appearance_mode
from customtkinter import set_default_color_theme
from customtkinter import CTkFrame, CTkButton, CTkLabel
from customtkinter import CTkEntry, CTkRadioButton
from customtkinter import CTkCheckBox
from customtkinter import CTkProgressBar
from tkinter import StringVar, BooleanVar, PhotoImage
from tkinter import CENTER
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

class LogFile:
	'Log to file'
	
	def __init__(self, filepath):
		'Open log file'
		self.filehandler = open(filepath, 'w+')
		self.write_timestamp()

	def close(self):
		'Close log file'
		self.write_timestamp()
		self.filehandler.close()

	def write_timestamp(self):
		'Write timestamp to log file'
		print(self.timestamp(), file=self.filehandler)

	def timestamp(self):
		'Give timestamp for now'
		return datetime.now().strftime('%Y-%m-%d %H:%M:%S.%f')

class ZeroD:
	'Use zerod.exe'

	def __init__(self, path, dummy=False):
		'Generate Object with the desired functions'
		self.path = path
		self.dummy = dummy
		self.extra = False

	def launch(self, targetpath, targetsize=None, blocksize=None):
		'Set file or drive to write to'
		cmd = [self.path, targetpath]
		if targetsize:
			cmd.append(str(targetsize))
		if blocksize:
			cmd.append(str(blocksize))
		if self.extra:
			cmd.append('/x')
		if self.dummy:
			cmd.append('/d')
		return Popen(cmd, stdout=PIPE, bufsize=0, universal_newlines=True)
		#	cmd,
		#	stdout = PIPE,
		#	stderr = STDOUT,
		#	bufsize=1,
		#	shell = True,
		#	encoding = 'utf-8',
		#	universal_newlines = True,
		#	errors = 'replace'
		#)

class Gui(CTk, WinUtils):
	'GUI look and feel'

	CONFIG = 'hdzero.conf'
	ZEROD = 'zerod.exe'
	APPICON = 'icon.png'
	PAD = 10
	SLIMPAD = 4
	BARWIDTH = 200
	BARHEIGHT = 20
	
	SIZEBASE = (
		{ 'PiB': 2**50, 'TiB': 2**40, 'GiB': 2**30, 'MiB': 2**20, 'kiB': 2**10 },
		{ 'PB': 10**15, 'TB': 10**12, 'GB': 10**9, 'MB': 10**6, 'kB': 10**3 }
	)

	def __init__(self):
		'Base Configuration'
		CTk.__init__(self)
		WinUtils.__init__(self)
		parentpath = Path(__file__).parent
		self.conf = Config(parentpath/self.CONFIG)
		self.conf.read()
		self.zerod = ZeroD(parentpath/self.ZEROD, dummy = self.conf['DEFAULT']['extra'])
		self.settings = dict()
		set_appearance_mode(self.conf['APPEARANCE']['mode'])
		set_default_color_theme(self.conf['APPEARANCE']['color_theme'])
		self.title(self.conf['TEXT']['title'])
		self.app_icon = PhotoImage(file=parentpath/self.APPICON)
		self.iconphoto(False, self.app_icon)
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
		CTkButton(frame, text=self.conf['TEXT']['wipefile'], command=self.wipe_files).pack(
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

	def workframe(self):
		'Define Work Frame to show Progress'
		self.work_frame = CTkToplevel(self)
		self.work_frame.title(self.conf['TEXT']['wipefile'])
		self.work_frame.iconphoto(False, self.app_icon)
		self.withdraw()
		frame = CTkFrame(self.work_frame)
		frame.pack(padx=self.PAD, pady=self.PAD, fill='both', expand=True)
		self.head_info = StringVar()
		CTkLabel(frame, textvariable=self.head_info).pack(padx=self.PAD, pady=self.PAD)
		self.main_info = StringVar()
		CTkLabel(frame, textvariable=self.main_info).pack(padx=self.PAD, pady=self.PAD)
		self.progressbar = CTkProgressBar(
			master = frame,
			width = self.BARWIDTH,
			height = self.BARHEIGHT,
			border_width = self.SLIMPAD
		)
		self.progressbar.set(0)
		self.progressbar.pack(padx=self.PAD, pady=self.PAD, fill='both', expand=True)
		CTkButton(frame, text=self.conf['TEXT']['abort'], command=self.abort_work).pack(
			padx=self.PAD, pady=self.PAD, side='right')

	def abort_work(self):
		'Write config an quit'
		self.work_frame.destroy()
		self.deiconify()
		self.update()

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

	def wipe_files(self):
		'Wipe selected file or files'
		self.decode_settings()
		self.work_target = askopenfilenames(
			title=self.conf['TEXT']['filestowipe'],
			initialdir=self.conf['DEFAULT']['initialdir']
		)
		if len(self.work_target) > 0:
			question = self.conf['TEXT']['filewarning']
			for file in self.work_target:
				question += f'\n{file}'
			if self.confirm(question):
				self.workframe()
				self.zerod.wipe = self.options['extra']
				self.work_files_thread = Thread(target=self.work_files)
				self.work_files_thread.start()
				return
		self.refresh()

	def work_files(self):
		'Do the work with zerod'
		self.main_info.set('Main')
		for file in self.work_target:
			self.head_info.set(file)
			proc = self.zerod.launch(file)
			for msg_raw in proc.stdout:
				msg_split = msg_raw.split()
				msg = msg_raw.strip()
				print(msg)
				if msg_split[0] == '...':
					self.main_info.set(msg)
					self.progressbar.set(float(msg_split[1]) / float(msg_split[3]))
					

		self.deiconify()
		self.update()
		self.work_frame.destroy()
		
		#self.work_frame.destroy()

		#self.quit_app()

		#self.logpath = Path(self.conf['DEFAULT']['log'])
		#self.zlog = LogFile(self.logpath)
				
		#with open (self.logpath, 'r') as follow:
		#	for file in files:
		#		self.zerod.launch(Path(file), log=self.zlog)
		#		thread
		#	self.read_zproc()
		#	for stdout in self.read_zproc():
		#		self.main_info.set(stdout)
		#		stdout_split = stdout.split()
		#		if stdout_split[0] == '...':
		#			self.progressbar.set(int(stdout_split[1])/int(stdout_split[3]))
		#self.zlog.close()

		#while True:
		#	line = follow.readline()
		#	if line:
		#		print(line)
		#		continue
		#	sleep(.1)


if __name__ == '__main__':  # start here
	Gui().mainloop()
