#!/usr/bin/env python3
# -*- coding: utf-8 -*-

__author__ = 'Markus Thilo'
__version__ = '1.0.0-0001_2023-01-11'
__license__ = 'GPL 3'
__email__ = 'markus.thilo@gmail.com'
__status__ = 'Release'
__description__ = 'Wipe HDDs'

from pathlib import Path
from configparser import ConfigParser
from wmi import WMI
from win32api import GetCurrentProcessId, GetLogicalDriveStrings
from win32com.shell.shell import IsUserAnAdmin
from functools import partial
from subprocess import Popen, PIPE, STDOUT, STARTUPINFO, STARTF_USESHOWWINDOW
from time import sleep
from datetime import datetime
from threading import Thread
from customtkinter import CTk, set_appearance_mode, set_default_color_theme
from customtkinter import CTkToplevel, CTkFrame, CTkLabel
from customtkinter import CTkButton, CTkEntry, CTkRadioButton
from customtkinter import CTkCheckBox, CTkProgressBar, CTkOptionMenu
from tkinter import StringVar, BooleanVar, PhotoImage, CENTER
from tkinter.messagebox import askquestion, showwarning, showerror
from tkinter.filedialog import askopenfilenames, asksaveasfilename

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

class WinUtils:
	'Needed Windows functions'

	WINCMD_TIMEOUT = 10
	WINCMD_RETRIES = 6

	def __init__(self, parentpath, dummy=False):
		'Generate Windows tools'
		self.conn = WMI()
		if dummy == True or dummy.lower() in ('true', 'yes', 'enabled'):
			self.dummy_mode = True
		else:
			self.dummy_mode = False
		self.cmd_startupinfo = STARTUPINFO()
		self.cmd_startupinfo.dwFlags |= STARTF_USESHOWWINDOW
		self.tmpscriptpath = parentpath/f'_diskpart_script_{GetCurrentProcessId()}.tmp'
		self.zerod_path = parentpath/'zerod.exe'
		self.this_drive = self.zerod_path.drive

	def cmd_launch(self, cmd):
		'Start command line subprocess without showing a terminal window'
		return Popen(
			cmd,
			startupinfo = self.cmd_startupinfo,
			stdout = PIPE,
			stderr = PIPE,
			universal_newlines = True
		)

	def zerod_launch(self, targetpath, blocksize=None, extra=False, writeff=False, verify=False):
		'Use zerod.exe to wipe file or drive'
		cmd = [self.zerod_path, targetpath]
		if blocksize:
			try:
				blocksize = int(blocksize)
			except ValueError:
				pass
			else:
				if blocksize % 512 == 0 and blocksize <= 1048576:
					cmd.append(str(blocksize))
		if extra:
			cmd.append('/x')
		if writeff:
			cmd.append('/f')
		if verify:
			cmd.append('/v')
		if self.dummy_mode:
			cmd.append('/d')
		return self.cmd_launch(cmd)

	def is_user_an_admin(self):
		'True if current User has Admin rights'
		return IsUserAnAdmin()

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
		for driveletter in driveletters:
			proc = self.cmd_launch(['mountvol', driveletter, '/p'])
			try:
				proc.wait(timeout=self.WINCMD_TIMEOUT)
			except:
				pass
		stillmounted = driveletters
		for cnt in range(self.WINCMD_RETRIES):
			for driveletter in stillmounted:
				if not Path(driveletter).exists():
					stillmounted.remove(driveletter)
			if stillmounted == list():
				return
			sleep(self.WINCMD_TIMEOUT)
		return stillmounted

	def run_diskpart(self, script):
		'Run diskpart script'
		self.tmpscriptpath.write_text(script)
		proc = self.cmd_launch(['diskpart', '/s', self.tmpscriptpath])
		try:
			ret = proc.wait(timeout=self.WINCMD_TIMEOUT)
		except:
			ret = None
		try:
			self.tmpscriptpath.unlink()
		except:
			return
		return ret

	def clean_table(self, driveid):
		'Clean partition table using diskpart'
		try:
			driveno = driveid[17:]
		except:
			return
		return self.run_diskpart(f'''select disk {driveno}
clean
list partition
'''
		)	# list partiton makes shure that the disk is free to write

	def create_partition(self, driveid, label, letter=None, table='gpt', fs='ntfs'):
		'Create partition using diskpart'
		try:
			driveno = driveid[17:]
		except:
			return
		if not letter:
			usedletters = GetLogicalDriveStrings().split(':\\\x00')
			for char in range(ord('D'),ord('Z')+1):
				if not chr(char) in usedletters:
					pure_letter = chr(char)
					break
			else:
				return
			letter = pure_letter + ':'
		else:
			pure_letter = letter.strip(':')
		ret = self.run_diskpart(f'''select disk {driveno}
clean
convert {table}
create partition primary
format quick fs={fs} label={label}
assign letter={pure_letter}
''')
		for cnt in range(self.WINCMD_RETRIES):
			if Path(letter).exists():
				return letter
			sleep(self.WINCMD_TIMEOUT)

class Logging:
	'Log to file'
	
	def __init__(self, parentpath):
		'Generate logging'
		self.log_header_path = parentpath / 'logheader.txt'
	
	def start_log(self, info):
		'Open log file'
		self.log_string = info
		self.append_log(self.timestamp())

	def append_log(self, info):
		'Append to log'
		self.log_string += f'{info}\n'

	def write_log(self, logpath):
		'Write log file'
		if logpath:
			with open(logpath, 'w') as lf:
				lf.write(self.log_header_path.read_text() + self.log_string)

	def timestamp(self):
		'Give timestamp for now'
		return datetime.now().strftime('%Y-%m-%d %H:%M:%S.%f')

	def log_timestamp(self):
		'Append timestamp to log'
		self.append_log(self.timestamp())

	def notepad_log_header(self):
		'Edit log header file with Notepad'
		proc = Popen(['notepad', self.log_header_path])
		proc.wait()

class Gui(CTk, WinUtils, Logging):
	'GUI look and feel'

	PAD = 8
	SLIMPAD = 4
	LABELWIDTH = 400
	BARWIDTH = 200
	BARHEIGHT = 20
	LETTERWIDTH = 28
	CORNER_RADIUS = 6
	LETTER_COL = "green"
	WARNING_COL = "red"
	
	SIZEBASE = (
		{ 'PiB': 2**50, 'TiB': 2**40, 'GiB': 2**30, 'MiB': 2**20, 'kiB': 2**10 },
		{ 'PB': 10**15, 'TB': 10**12, 'GB': 10**9, 'MB': 10**6, 'kB': 10**3 }
	)

	def __init__(self):
		'Base Configuration'
		self.__file_parentpath__ = Path(__file__).parent
		self.conf = Config(self.__file_parentpath__/'hdzero.conf')
		self.conf.read()
		WinUtils.__init__(self, self.__file_parentpath__, dummy=self.conf['DEBUG']['dummy'])
		self.i_am_admin = self.is_user_an_admin()
		Logging.__init__(self, self.__file_parentpath__)
		CTk.__init__(self)
		set_appearance_mode(self.conf['APPEARANCE']['mode'])
		set_default_color_theme(self.conf['APPEARANCE']['color_theme'])
		self.title(self.conf['TEXT']['title'] + f' {__version__}')
		self.app_icon = PhotoImage(file=self.__file_parentpath__/'icon.png')
		self.iconphoto(False, self.app_icon)

		if not self.i_am_admin and askquestion(
			self.conf['TEXT']['warning_title'],
			self.conf['TEXT']['notadmin']
			) == 'yes':
			self.abort = True
		else:
			self.abort = False
			if self.dummy_mode:
				showwarning(title = self.conf['TEXT']['warning_title'], message = 'DUMMY MODE!!!')
			self.mainframe_user_opts = dict()
			self.mainframe()

	def readable(self, size):
		'Genereate readable size string'
		try:
			size = int(size)
		except (TypeError, ValueError):
			return self.conf['TEXT']['undetected']
		strings = list()
		for base in self.SIZEBASE:
			for u, b in base.items():
				rnd = round(size/b, 2)
				if rnd >= 1:
					break
			if rnd >= 10:
				rnd = round(rnd,1)
			if rnd >= 100:
				rnd = round(rnd)
			strings.append(f'{rnd} {u}')
		return ' / '.join(strings)

	def list_to_string(self, strings):
		'One per line'
		if len(strings) <= 20:
			return ':\n' + '\n '.join(strings)
		else:
			return ':\n' + '\n '.join(strings[:20]) + f'\n... {len(strings)-20} ' + self.conf['TEXT']['more']

	def mainframe(self):
		'Define Main Frame'
		self.main_frame = CTkFrame(self)
		self.main_frame.pack()
		if self.i_am_admin:	# no disk access without admin rights
			### WIPE DRIVE ###
			self.drive_frame = CTkFrame(self.main_frame)
			self.drive_frame.pack(padx=self.PAD, pady=self.PAD, fill='both', expand=True)
			head_frame = CTkFrame(self.drive_frame)
			head_frame.pack(padx=self.PAD, pady=self.PAD, fill='both', expand=True)
			CTkLabel(head_frame, text=self.conf['TEXT']['disklabel']).pack(
				padx=2*self.PAD, pady=self.PAD, side='left')
			opt_frame = CTkFrame(self.drive_frame)
			opt_frame.pack(padx=self.PAD, pady=self.PAD, fill='both', expand=True)
			### DISK OPTIONS FRAME ###
			CTkButton(opt_frame, text=self.conf['TEXT']['refresh'],
				command=self.refresh).grid(padx=self.PAD, pady=(self.PAD,0), row=0, column=0, sticky='w')
			self.mainframe_user_opts['parttable'] = StringVar(value=self.conf['DEFAULT']['parttable'])
			CTkRadioButton(master=opt_frame, variable=self.mainframe_user_opts['parttable'],
				value='None', text=self.conf['TEXT']['no_diskpart']).grid(
				padx=self.PAD, pady=(self.PAD, 0), row=0, column=1, sticky='w')
			CTkRadioButton(master=opt_frame, variable=self.mainframe_user_opts['parttable'],
				value='gpt', text='GPT').grid(padx=self.PAD, row=1, column=1, sticky='w')
			CTkRadioButton(master=opt_frame, variable=self.mainframe_user_opts['parttable'],
				value='mbr', text='MBR').grid(padx=self.PAD, pady=(0, self.PAD), row=2, column=1, sticky='w')
			self.mainframe_user_opts['fs'] = StringVar(value=self.conf['DEFAULT']['fs'])
			CTkRadioButton(master=opt_frame, variable=self.mainframe_user_opts['fs'],
				value='ntfs', text='NTFS').grid(padx=self.PAD, pady=(self.PAD, 0), row=0, column=2, sticky='w')
			CTkRadioButton(master=opt_frame, variable=self.mainframe_user_opts['fs'],
				value='exfat', text='exFAT').grid(padx=self.PAD, row=1, column=2, sticky='w')
			CTkRadioButton(master=opt_frame, variable=self.mainframe_user_opts['fs'],
				value='fat32', text='FAT32').grid(padx=self.PAD, pady=(0, self.PAD), row=2, column=2, sticky='w')
			labeltext = self.conf['TEXT']['volname']
			CTkLabel(opt_frame, text=f'{labeltext}:').grid(padx=self.PAD, pady=(
				self.PAD, 0), row=0, column=3, sticky='e')
			self.mainframe_user_opts['volname'] = StringVar(value=self.conf['DEFAULT']['volname'])
			CTkEntry(opt_frame, textvariable=self.mainframe_user_opts['volname']).grid(
				padx=self.PAD, pady=(self.PAD, 0), row=0, column=4, sticky='w')
			self.mainframe_user_opts['writelog'] = BooleanVar(value=self.conf['DEFAULT']['writelog'])
			CTkCheckBox(master=opt_frame, text=self.conf['TEXT']['writelog'], variable=self.mainframe_user_opts['writelog'],
				onvalue=True, offvalue=False).grid(padx=self.PAD, pady=(0, self.PAD), row=2, column=4, sticky='w')
			CTkButton(opt_frame, text=self.conf['TEXT']['editlog'],
				command=self.edit_log_header).grid(padx=self.PAD, pady=(0, self.PAD), row=2, column=3, sticky='w')	
			### DRIVES ###
			labeltext = self.conf['TEXT']['wipedrive']
			for drive in self.list_drives():
				frame = CTkFrame(self.drive_frame)
				frame.pack(padx=self.PAD, pady=(0, self.PAD), fill='both', expand=True)
				partitions = [ part.Dependent.DeviceID for part in self.get_partitions(drive.index) ]
				if self.this_drive in partitions or 'C:' in partitions:
					hover_color = self.WARNING_COL
					warning = True
				else:
					hover_color = self.LETTER_COL
					warning = False
				if partitions == list():
					letter = ''
				else:
					letter = partitions[0]
				CTkButton(frame, text=f'{labeltext} {drive.Index}', hover_color=hover_color,
					command=partial(self.wipe_disk, drive.Index)).pack(
					padx=self.PAD, pady=self.SLIMPAD, side='left')
				if letter == '':
					label = CTkLabel(frame, text='', width=self.LETTERWIDTH)
				else:
					if warning:
						label = CTkLabel(frame, text=letter, fg_color="red", text_color="white",
						width=self.LETTERWIDTH, corner_radius=self.CORNER_RADIUS)
					else:
						label = CTkLabel(frame, text=letter, width=self.LETTERWIDTH)
				label.pack(padx=self.PAD, pady=self.SLIMPAD, side='left')
				CTkLabel(frame, text=f'{drive.Caption}, {drive.MediaType} ({self.readable(drive.Size)})').pack(
					padx=self.PAD, pady=self.SLIMPAD, anchor='w')
		### WIPE FILE(S) ###
		self.file_frame = CTkFrame(self.main_frame)
		self.file_frame.pack(padx=self.PAD, pady=self.PAD, fill='both', expand=True)
		if not self.i_am_admin:
			notadmin_frame = CTkFrame(self.file_frame)
			notadmin_frame.pack(padx=self.PAD, pady=self.PAD, fill='both', expand=True)
			CTkLabel(notadmin_frame, text=self.conf['TEXT']['notadmin']).pack(
				padx=2*self.PAD, pady=self.PAD, side='left')
		frame = CTkFrame(self.file_frame)
		frame.pack(padx=self.PAD, pady=self.PAD, fill='both', expand=True)
		CTkButton(frame, text=self.conf['TEXT']['wipefile'], command=self.wipe_files).pack(
			padx=self.PAD, pady=self.PAD, side='left')
		CTkLabel(frame, text=self.conf['TEXT']['filelabel']).pack(padx=self.PAD, pady=self.PAD, side='left')
		self.mainframe_user_opts['deletefile'] = BooleanVar(value=self.conf['DEFAULT']['deletefile'])
		CTkCheckBox(master=frame, text=self.conf['TEXT']['deletefile'], variable=self.mainframe_user_opts['deletefile'],
			onvalue=True, offvalue=False).pack(padx=self.PAD, pady=self.PAD, side='left')
		### GENERAL OPTIONS FRAME ###
		frame = CTkFrame(self.main_frame)
		frame.pack(padx=self.PAD, pady=self.PAD, fill='both', expand=True)
		frame = CTkFrame(frame)
		frame.pack(padx=self.PAD, pady=self.PAD, fill='both', expand=True)
		self.mainframe_user_opts['extra'] = BooleanVar(value=self.conf['DEFAULT']['extra'])

		CTkCheckBox(master=frame, text=self.conf['TEXT']['extra'], variable=self.mainframe_user_opts['extra'],
			onvalue=True, offvalue=False).pack(padx=self.PAD, pady=self.PAD, side='left')

		self.mainframe_user_opts['writeff'] = BooleanVar(value=self.conf['DEFAULT']['writeff'])
		CTkCheckBox(master=frame, text=self.conf['TEXT']['writeff'], variable=self.mainframe_user_opts['writeff'],
			onvalue=True, offvalue=False).pack(padx=self.PAD, pady=self.PAD, side='left')
		self.mainframe_user_opts['full_verify'] = BooleanVar(value=self.conf['DEFAULT']['full_verify'])
		CTkCheckBox(master=frame, text=self.conf['TEXT']['full_verify'],
			variable=self.mainframe_user_opts['full_verify'], onvalue=True, offvalue=False).pack(
			padx=self.PAD, pady=self.PAD, side='left')
		CTkLabel(frame, text=self.conf['TEXT']['blocksize']).pack(padx=self.PAD, pady=self.PAD, side='left')
		self.mainframe_user_opts['blocksize'] = StringVar(value=self.conf['DEFAULT']['blocksize'])
		blocksizes = ['auto'] + [ str(2**p) for p in range(9, 20) ]
		blocksizes.remove(self.conf['DEFAULT']['blocksize'])
		blocksizes = [self.conf['DEFAULT']['blocksize']] + blocksizes
		drop = CTkOptionMenu(master=frame, variable=self.mainframe_user_opts['blocksize'],
			dynamic_resizing=False, values=blocksizes)
		drop.pack(padx=self.PAD, pady=self.PAD, side='left')
		### BOTTOM ###
		frame = CTkFrame(self.main_frame)
		frame.pack(padx=self.PAD, pady=self.PAD, fill='both', expand=True)
		frame = CTkFrame(frame)
		frame.pack(padx=self.PAD, pady=self.PAD, fill='both', expand=True)
		self.mainframe_user_opts['askmore'] = BooleanVar(value=self.conf['DEFAULT']['askmore'])
		CTkCheckBox(master=frame, text=self.conf['TEXT']['askmore'], variable=self.mainframe_user_opts['askmore'],
			onvalue=True, offvalue=False).pack(padx=self.PAD, pady=self.PAD, side='left')
		CTkButton(frame, text=self.conf['TEXT']['quit'], command=self.quit_app).pack(
			padx=self.PAD, pady=self.PAD, side='right')

	def decode_settings(self):
		'Decode settings and write as default to config file'
		self.options = { setting: tkvalue.get() for setting, tkvalue in self.mainframe_user_opts.items() } 
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
		question += '\n\n'
		question += self.conf['TEXT']['areyoushure']
		if not askquestion(self.conf['TEXT']['warning_title'], question) == 'yes':
			return False
		if self.options['askmore'] and not (
			askquestion(self.conf['TEXT']['warning_title'], self.conf['TEXT']['areyoureallyshure']) == 'yes'
			and askquestion(self.conf['TEXT']['warning_title'], self.conf['TEXT']['areyoufngshure']) == 'yes'
		):
			return False
		return True

	def workframe(self, title):
		'Define Work Frame to show Progress'
		self.working = True
		self.work_frame = CTkToplevel(self)
		self.work_frame.title(title)
		self.work_frame.iconphoto(False, self.app_icon)
		self.withdraw()
		frame = CTkFrame(self.work_frame)
		frame.pack(padx=self.PAD, pady=self.PAD, fill='both', expand=True)
		self.head_info = StringVar()
		CTkLabel(frame, textvariable=self.head_info, width=self.LABELWIDTH).pack(
			padx=self.PAD, pady=self.PAD)
		self.main_info = StringVar()
		CTkLabel(frame, textvariable=self.main_info).pack(padx=self.PAD, pady=self.PAD)
		self.progress_info = StringVar(value='0 %')
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
		self.refresh()

	def watch_zerod(self, files_of_str = ''):
		'Handle output of zerod'
		of_str = self.conf['TEXT']['of']
		pass_str = self.conf['TEXT']['pass']
		bytes_str = self.conf['TEXT']['bytes']
		testing_blocksize_str = self.conf['TEXT']['testing_blocksize']
		using_blocksize_str = self.conf['TEXT']['using_blocksize']
		verifying_str = self.conf['TEXT']['verifying']
		verified_str = self.conf['TEXT']['verified']
		were_wiped_str = self.conf['TEXT']['were_wiped']
		pass_of_str = ''
		for msg_raw in self.zerod_proc.stdout:
			msg_split = msg_raw.split()
			msg = msg_raw.strip()
			info = None
			if msg_split[0] == '...':
				progress_str = files_of_str + pass_of_str + ' '
				progress_str += f'{msg_split[1]} {of_str} {msg_split[3]} {bytes_str}'
				self.progress_info.set(progress_str)
				self.progressbar.set(float(msg_split[1]) / float(msg_split[3]))
			elif msg_split[0] == 'Calculating':
				continue
			elif msg_split[0] == 'Pass':
				if self.options['extra']:
					pass_of_str = f'{pass_str} {msg_split[1]} {of_str} {msg_split[3]}'
			elif msg_split[0] == 'Testing':
				self.main_info.set(f'{testing_blocksize_str} {msg_split[3]} {bytes_str}')
			elif msg_split[0] == 'Using':
				self.blocksize = msg_split[4]
				self.main_info.set(f'{using_blocksize_str} {self.blocksize} {bytes_str}')
			elif msg_split[0] == 'Verifying':
				pass_of_str = ''
				info = f'{verifying_str} {msg_split[1]}'
				self.main_info.set(info)
			elif msg_split[0] == 'Verified':
				info = f'{verified_str} {msg_split[1]} {bytes_str}'
				self.main_info.set(info)
			elif msg_split[0] == 'All':
				info = f'{msg_split[2]} {bytes_str} {were_wiped_str}'
				self.main_info.set(info)
			else:
				info = msg
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
			if part.Dependent.DeviceID == self.this_drive or part.Dependent.DeviceID == 'C:':
				mounted += f'\n\n{part.Dependent.DeviceID} --- ' + self.conf['TEXT']['danger'] + ' ---\n'
			else:
				mounted += f'\n\n{part.Dependent.DeviceID}\n'
			mounted += f'{part.Antecedent.DeviceID}\n{part.Dependent.Description}\n'
			mounted += self.readable(part.Dependent.Size)
		if mounted != '':
			question += self.conf['TEXT']['mounted'] + mounted
		else:
			question += self.conf['TEXT']['nomounted']
		if self.confirm(question):
			self.work_target = drive.DeviceID
			stillmounted = self.dismount_drives(driveletters)
			if stillmounted:
				warning = self.conf['TEXT']['not_dismount'] + ' '
				warning += ', '.join(stillmounted)
				warning += '\n\n' + self.conf['TEXT']['dismount_manually']
				showwarning(title=self.conf['TEXT']['warning_title'], message=warning)
			else:
				if driveletters != list():
					self.driveletter = driveletters[0]
				else:
					self.driveletter = None
				if self.options['writelog']:
					self.start_log(f'''
{drive.Caption}
{drive.MediaType}
{self.readable(drive.Size)}

'''
					)
				self.work_files_thread = Thread(target=self.work_drive)
				self.work_files_thread.start()
				return
		self.refresh()

	def work_drive(self):
		'Do the work with zerod, target is a drve'
		self.workframe(self.conf['TEXT']['wipedrive'])
		self.head_info.set(self.work_target) 
		self.main_info.set(self.conf['TEXT']['cleaning_table'])
		if self.clean_table(self.work_target) != 0:
			showwarning(title=self.conf['TEXT']['warning_title'],
				message=self.conf['TEXT']['not_clean_table'] + f' {self.work_target}')
			self.working = False
			self.quit_work()
			return
		self.zerod_proc = self.zerod_launch(
			self.work_target,
			blocksize = self.options['blocksize'],
			extra = self.options['extra'],
			writeff = self.options['writeff'],
			verify = self.options['full_verify']
		)
		self.watch_zerod()
		if not self.working:
			return
		if self.zerod_proc.wait() != 0 and not self.dummy_mode:
			showerror(
				self.conf['TEXT']['error'],
				self.conf['TEXT']['errorwhile'] + f' {self.work_target} \n\n {self.zerod_proc.stderr.read()}'
			)
			self.working = False
			self.quit_work()
			return
		self.progress_info.set('')
		mounted = None
		if self.options['parttable'] != 'None':
			self.main_info.set(self.conf['TEXT']['creatingpartition'])
			self.progressbar.configure(mode="indeterminate")
			self.progressbar.start()
			mounted = self.create_partition(
				self.work_target,
				self.options['volname'],
				table = self.options['parttable'],
				fs = self.options['fs'],
				letter = self.driveletter
			)
			if mounted:
				info = self.conf['TEXT']['newpartition'] + f' {mounted}'
				self.main_info.set(info)
			else:
				showwarning(
					title = self.conf['TEXT']['warning_title'],
					message = self.conf['TEXT']['couldnotcreate']
				)
		self.working = False
		if self.options['writelog']:
			self.log_timestamp()
			logpath = None
			if mounted:
				logpath = Path(mounted)/'hdzero-log.txt'
			else:
				filename = asksaveasfilename(title=self.conf['TEXT']['write_log'], defaultextension='.txt')
				if filename:
					logpath = Path(filename)
			self.write_log(logpath)
		self.main_info.set(self.conf['TEXT']['all_done'])
		self.progress_info.set('100 %')
		self.progressbar.stop()
		self.progressbar.configure(mode='determinate')
		self.progressbar.set(1)

	def wipe_files(self):
		'Wipe selected file or files - launch thread'
		self.decode_settings()
		self.work_target = askopenfilenames(
			title=self.conf['TEXT']['filestowipe'],
			initialdir=self.conf['DEFAULT']['initialdir']
		)
		if len(self.work_target) > 0:
			question = self.conf['TEXT']['filewarning']
			question += self.list_to_string(self.work_target)
			if self.confirm(question):
				self.conf['DEFAULT']['initialdir'] = str(Path(self.work_target[0]).parent)
				self.decode_settings()
				self.options['writelog'] = False
				self.work_files_thread = Thread(target=self.work_files)
				self.work_files_thread.start()
				return
		self.refresh()

	def work_files(self):
		'Do the work with zerod on targetrd files'
		self.workframe(self.conf['TEXT']['wipefile'])
		of_str = self.conf['TEXT']['of']
		file_str = self.conf['TEXT']['file']
		files_of_str = ''
		qt_files = len(self.work_target)
		file_cnt = 0
		errors = list()
		self.blocksize = self.options['blocksize']
		for file in self.work_target:
			self.head_info.set(file)
			self.zerod_proc = self.zerod_launch(
				file,
				blocksize = self.blocksize,
				extra = self.options['extra'],
				writeff = self.options['writeff'],
				verify = self.options['full_verify']
			)
			if qt_files > 1:
				file_cnt += 1
				files_of_str = f'{file_str} {file_cnt} {of_str} {qt_files}, '
			self.watch_zerod(files_of_str=files_of_str)
			if not self.working:
				return
			if self.zerod_proc.wait() == 0:
				self.main_info.set(self.conf['TEXT']['deleting_file'])
				try:
					Path(file).unlink()
				except:
					errors.append(file)
			else:
				errors.append(file)
		self.head_info.set('')
		self.main_info.set(self.conf['TEXT']['all_done'])
		self.progress_info.set('100 %')
		self.working = False
		if errors != list():
			showerror(
				self.conf['TEXT']['error'],
				self.conf['TEXT']['errorwhile'] + self.list_to_string(errors)
			)
			self.quit_work()

if __name__ == '__main__':  # start here
	gui = Gui()
	if not gui.abort:
		gui.mainloop()
