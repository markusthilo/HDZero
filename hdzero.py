#!/usr/bin/env python3
# -*- coding: utf-8 -*-

__author__ = 'Markus Thilo'
__version__ = '0.1_2022-12-28'
__license__ = 'GPL-3'
__email__ = 'markus.thilo@gmail.com'
__status__ = 'Under Construction'
__description__ = 'Wipe data'

from pathlib import Path
from configparser import ConfigParser
from wmi import WMI
from win32api import GetCurrentProcessId, GetLogicalDriveStrings
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
from tkinter.messagebox import askquestion, showwarning, showerror
from tkinter.filedialog import askopenfilenames

class Config(ConfigParser):
	'Handle config file'

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

class ZeroD:
	'Use zerod.exe'

	def __init__(self, path, dummy=False):
		'Generate Object with the desired functions'
		self.path = path
		self.dummy = dummy

	def launch(self, targetpath, targetsize=None, blocksize=None, extra=False):
		'Set file or drive to write to'
		cmd = [self.path, targetpath]
		if targetsize:
			cmd.append(str(targetsize))
		if blocksize:
			cmd.append(str(blocksize))
		if extra:
			cmd.append('/x')
		if self.dummy:
			cmd.append('/d')
		return Popen(cmd, stdout=PIPE, bufsize=0, universal_newlines=True)

class WinUtils:
	'Needed Windows functions'

	def __init__(self, parentpath):
		'Generate Windows tools'
		self.conn = WMI()
		self.tmpscriptpath = parentpath / f'_diskpart_script_{GetCurrentProcessId()}.tmp'

	def list_drives(self):
		'Use DiskDrive'
		drives = { drive.Index: drive for drive in self.conn.Win32_DiskDrive() }
		for i in sorted(drives.keys()):
			yield drives[i]

	def get_drive(self, diskindex):
		'Get DiskDrive to given DiskIndex'
		for drive in self.conn.Win32_DiskDrive():
			if drive.index == diskindex:
				return drive

	def get_partitions(self, diskindex):
		'Get Partition to given DiskIndex'
		for part in self.conn.Win32_LogicalDiskToPartition():
			if part.Antecedent.DiskIndex == diskindex:
				yield part

	def dismount_drives(self, driveletters):
		'Dismount Drives'
		notdismounted = list()
		for driveletter in driveletters:
			proc = Popen(['mountvol', driveletter, '/p'], stdout=PIPE, stderr=PIPE)
			if proc.wait() != 0:
				notdismounted.append(driveletter)
		return notdismounted

	def create_partition(self, driveid, label, letter=None, table='gpt', fs='ntfs'):
		try:
			driveno = int(driveid[17:])
		except ValueError:
			return
		if not letter:
			usedletters = GetLogicalDriveStrings().split(':\\\x00')
			for char in range(ord('D'),ord('Z')+1):
				if not chr(char) in usedletters:
					letter = chr(char)
					break
			else:
				return
		else:
			letter = letter.strip(':')
		with open(self.tmpscriptpath, 'w') as fh:
			fh.write(f'''select disk {driveno}
clean
convert {table}
create partition primary
format quick fs={fs} label={label}
assign letter={letter}
'''
			)

		return letter + ':'

		#proc = Popen(['diskpart', '/s', self.tmpscriptpath], stdout=PIPE, stderr=PIPE)
		#print(self.tmpscriptpath.unlink())
		#if proc.wait() == 0:
		#	res = letter + ':'
		#else:
		#	res = None
		#self.tmpscriptpath.unlink()
		#return res

class Logging:
	'Log to file'
	
	def __init__(self, parentpath):
		'Generate logging'
		self.log_header_path = parentpath / 'logheader.txt'
	
	def start_log(self):
		'Open log file'
		with open(self.log_header_path, 'r') as fh:
			self.log_string = fh.read()
		self.append(self.timestamp())

	def append_log(self, info):
		'Append to log'
		self.log_string += f'{info}/n'

	def write_log(self, driveletter):
		'Write log file'
		with open(Path(driveletter + '\\')/'hdzero-log.txt', 'w') as fh:
			fh.write(self.log_string)

	def timestamp(self):
		'Give timestamp for now'
		return datetime.now().strftime('%Y-%m-%d %H:%M:%S.%f')

	def notepad_log_header(self):
		'Edit log header file with Notepad'
		proc = Popen(['notepad', self.log_header_path], stdout=PIPE, stderr=PIPE)
		proc.wait()

class Gui(CTk, WinUtils, Logging):
	'GUI look and feel'

	CONFIG = 'hdzero.conf'
	ZEROD = 'zerod.exe'
	APPICON = 'icon.png'
	PAD = 10
	SLIMPAD = 4
	LABELWIDTH = 400
	BARWIDTH = 200
	BARHEIGHT = 20
	
	SIZEBASE = (
		{ 'PiB': 2**50, 'TiB': 2**40, 'GiB': 2**30, 'MiB': 2**20, 'kiB': 2**10 },
		{ 'PB': 10**15, 'TB': 10**12, 'GB': 10**9, 'MB': 10**6, 'kB': 10**3 }
	)

	def __init__(self):
		'Base Configuration'
		parentpath = Path(__file__).parent
		self.conf = Config(parentpath/self.CONFIG)
		self.conf.read()
		self.zerod = ZeroD(parentpath/self.ZEROD, dummy = self.conf['DEBUG']['dummy'])
		self.settings = dict()
		CTk.__init__(self)
		WinUtils.__init__(self, parentpath)
		Logging.__init__(self, parentpath)
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
		bytes_str = self.conf['TEXT']['bytes']
		outstr = f'{size} {bytes_str}'
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
		### OPTION FRAME ###
		CTkButton(opt_frame, text=self.conf['TEXT']['refresh'],
			command=self.refresh).grid(padx=self.PAD, pady=(self.PAD,0), row=0, column=0, sticky='w')
		self.settings['parttable'] = StringVar(value=self.conf['DEFAULT']['parttable'])
		CTkRadioButton(master=opt_frame, variable=self.settings['parttable'],
			value=None, text=self.conf['TEXT']['no_diskpart']).grid(
			padx=self.PAD, pady=(self.PAD, 0), row=0, column=1, sticky='w')
		CTkRadioButton(master=opt_frame, variable=self.settings['parttable'],
			value='gpt', text='GPT').grid(padx=self.PAD, row=1, column=1, sticky='w')
		CTkRadioButton(master=opt_frame, variable=self.settings['parttable'],
			value='mbr', text='MBR').grid(padx=self.PAD, pady=(0, self.PAD), row=2, column=1, sticky='w')
		self.settings['fs'] = StringVar(value=self.conf['DEFAULT']['fs'])
		CTkRadioButton(master=opt_frame, variable=self.settings['fs'],
			value='ntfs', text='NTFS').grid(padx=self.PAD, pady=(self.PAD, 0), row=0, column=2, sticky='w')
		CTkRadioButton(master=opt_frame, variable=self.settings['fs'],
			value='exfat', text='exFAT').grid(padx=self.PAD, row=1, column=2, sticky='w')
		CTkRadioButton(master=opt_frame, variable=self.settings['fs'],
			value='fat32', text='FAT32').grid(padx=self.PAD, pady=(0, self.PAD), row=2, column=2, sticky='w')
		labeltext = self.conf['TEXT']['volname']
		CTkLabel(opt_frame, text=f'{labeltext}:').grid(padx=self.PAD, pady=(
			self.PAD, 0), row=0, column=3, sticky='w')
		self.settings['volname'] = StringVar(value=self.conf['DEFAULT']['volname'])
		CTkEntry(opt_frame, textvariable=self.settings['volname']).grid(
			padx=self.PAD, pady=(self.PAD, 0), row=0, column=4, sticky='w')
		self.settings['writelog'] = BooleanVar(value=self.conf['DEFAULT']['writelog'])
		CTkCheckBox(master=opt_frame, text=self.conf['TEXT']['writelog'], variable=self.settings['writelog'],
			onvalue=True, offvalue=False).grid(padx=self.PAD, pady=(0, self.PAD), row=2, column=4, sticky='w')
		CTkButton(opt_frame, text=self.conf['TEXT']['editlog'],
			command=self.edit_log_header).grid(padx=self.PAD, pady=(0, self.PAD), row=2, column=3, sticky='w')	
		### DRIVES ###
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

	def edit_log_header(self):
		'Edit log header'
		self.withdraw()
		self.notepad_log_header()
		self.deiconify()
		self.update()

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
		if not askquestion(self.conf['TEXT']['warning_title'], question) == 'yes':
			return False
		if self.options['askmore'] and not (
			askquestion(self.conf['TEXT']['warning_title'], self.conf['TEXT']['areyoureallyshure']) == 'yes'
			and askquestion(self.conf['TEXT']['warning_title'], self.conf['TEXT']['areyoufngshure']) == 'yes'
		):
			return False
		return True

	def workframe(self):
		'Define Work Frame to show Progress'
		self.working = True
		self.work_frame = CTkToplevel(self)
		self.work_frame.title(self.conf['TEXT']['wipefile'])
		self.work_frame.iconphoto(False, self.app_icon)
		self.withdraw()
		frame = CTkFrame(self.work_frame)
		frame.pack(padx=self.PAD, pady=self.PAD, fill='both', expand=True)
		self.head_info = StringVar()
		CTkLabel(frame, textvariable=self.head_info, width=self.LABELWIDTH).pack(
			padx=self.PAD, pady=self.PAD)
		self.main_info = StringVar()
		CTkLabel(frame, textvariable=self.main_info).pack(padx=self.PAD, pady=self.PAD)
		self.progress_info = StringVar()
		CTkLabel(frame, textvariable=self.progress_info).pack(padx=self.PAD, pady=self.PAD)
		self.progressbar = CTkProgressBar(
			master = frame,
			width = self.BARWIDTH,
			height = self.BARHEIGHT,
			border_width = self.SLIMPAD
		)
		self.progressbar.set(0)
		self.progressbar.pack(padx=self.PAD, pady=self.PAD, fill='both', expand=True)
		CTkButton(frame, text=self.conf['TEXT']['quit'], command=self.quit_work).pack(
			padx=self.PAD, pady=self.PAD, side='right')

	def quit_work(self):
		'Write config an quit'
		if self.working:
			if self.confirm(self.conf['TEXT']['abort']):
				self.working = False
				self.zerod_proc.terminate()
		self.work_frame.destroy()
		self.deiconify()
		self.update()

	def watch_zerod(self, files_of_str = ''):
		'Handle output of zerod'
		of_str = self.conf['TEXT']['of']
		pass_str = self.conf['TEXT']['pass']
		bytes_str = self.conf['TEXT']['bytes']
		testing_blocksize_str = self.conf['TEXT']['testing_blocksize']
		using_blocksize_str = self.conf['TEXT']['using_blocksize']
		pass_of_str = ''
		if self.options['writelog']:
				self.start_log()
		debug = self.conf['DEBUG']['print']
		for msg_raw in self.zerod_proc.stdout:
			msg_split = msg_raw.split()
			msg = msg_raw.strip()
			info = None
			if debug:
				print(msg)
			if msg_split[0] == '...':
				progress_str = files_of_str + pass_of_str + ' '
				progress_str += f'{msg_split[1]} {of_str} {msg_split[3]} {bytes_str}'
				self.progress_info.set(progress_str)
				self.progressbar.set(float(msg_split[1]) / float(msg_split[3]))
			elif msg_split[0] == 'Pass':
				if self.options['extra']:
					pass_of_str = f'{pass_str} {msg_split[1]} {of_str} {msg_split[3]}'
			elif msg_split[0] == 'Testing':
				info = f'{testing_blocksize_str} {msg_split[3]} {bytes_str}'
				self.main_info.set(info)
			elif msg_split[0] == 'Using':
				self.blocksize = msg_split[3]
				info = f'{using_blocksize_str} {self.blocksize} {bytes_str}'
				self.main_info.set(info)
			else:
				self.main_info.set(msg)
			if info and self.options['writelog']:
				self.append_log(info)

	def wipe_disk(self, diskindex):
		'Wipe selected disk - launch thread'
		self.decode_settings()
		drive = self.get_drive(diskindex)
		question = self.conf['TEXT']['drivewarning']
		question += f'\n\n{drive.DeviceID}\n{drive.Caption}, {drive.MediaType}\n'
		question += self.readable(drive.Size) + '\n\n'
		mounted = ''
		driveletters = list()
		for part in self.get_partitions(diskindex):
			driveletters.append(part.Dependent.DeviceID)
			mounted += f'\n\n{part.Dependent.DeviceID}\n'
			mounted += f'{part.Antecedent.DeviceID}\n{part.Dependent.Description}\n'
			mounted += self.readable(part.Dependent.Size)
		if mounted != '':
			question += self.conf['TEXT']['mounted'] + mounted
		else:
			question += self.conf['TEXT']['nomounted']
		if self.confirm(question):
			notdismounted = self.dismount_drives(driveletters)
			if notdismounted != list():
				warning = self.conf['TEXT']['not_dismount'] + ' '
				warning += ', '.join(notdismounted)
				warning += '\n\n' + self.conf['TEXT']['dismount_manually']
				showwarning(title=self.conf['TEXT']['warning_title'], message=warning)
			else:
				if driveletters != list():
					self.driveletter = driveletters[0]
				else:
					self.driveletter = None
				self.work_target = drive.DeviceID
				self.work_targetsize = drive.Size
				self.work_files_thread = Thread(target=self.work_drive)
				self.work_files_thread.start()
				return
		self.refresh()

	def work_drive(self):
		'Do the work with zerod, target is a drve'
		self.workframe()
		self.head_info.set(self.work_target)
		self.zerod_proc = self.zerod.launch(
			self.work_target,
			targetsize = self.work_targetsize,
			extra = self.options['extra']
		)
		self.watch_zerod()
		self.progress_info.set(f'100%, {self.readable(self.work_targetsize)}')
		if self.options['parttable']:
			self.main_info.set(self.conf['TEXT']['creatingpartition'])
			mounted = self.create_partition(
				self.work_target,
				self.options['volname'],
				table = self.options['parttable'],
				fs = self.options['fs'],
				letter = self.driveletter
			)
			if mounted:
				self.main_info.set(self.conf['TEXT']['newpartition'] + f' {mounted}')
				if self.options['writelog']:
					self.write_log(mounted)
			else:
				showwarning(
					title = self.conf['TEXT']['warning_title'],
					message = self.conf['TEXT']['couldnotcreate']
				)
		else:
			self.main_info.set(self.conf['TEXT']['all_done'])
		self.working = False

	def wipe_files(self):
		'Wipe selected file or files - launch thread'
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
				self.options['writelog'] = False
				self.work_files_thread = Thread(target=self.work_files)
				self.work_files_thread.start()
				return
		self.refresh()

	def work_files(self):
		'Do the work with zerod on targetrd files'
		self.workframe()
		of_str = self.conf['TEXT']['of']
		file_str = self.conf['TEXT']['file']
		files_of_str = ''
		qt_files = len(self.work_target)
		file_cnt = 0
		self.blocksize = None
		for file in self.work_target:
			self.head_info.set(file)
			self.zerod_proc = self.zerod.launch(
				file,
				blocksize = self.blocksize,
				extra = self.options['extra']
			)
			if qt_files > 1:
				file_cnt += 1
				files_of_str = f'{file_str} {file_cnt} {of_str} {qt_files}, '
			self.watch_zerod(files_of_str=files_of_str)
			if not self.working:
				return
			if self.zerod_proc.wait() == 0:
				self.main_info.set(self.conf['TEXT']['deleting_file'])
				if not self.zerod.dummy:
					Path(file).unlink()
		self.head_info.set('')
		self.main_info.set(self.conf['TEXT']['all_done'])
		self.progress_info.set('100 %')
		self.working = False

if __name__ == '__main__':  # start here
	Gui().mainloop()
