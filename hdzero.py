#!/usr/bin/env python3
# -*- coding: utf-8 -*-

__author__ = 'Markus Thilo'
__version__ = '1.0.1-0001_2023-02-27'
__license__ = 'GPL-3'
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
from tkinter import Tk, Toplevel, StringVar, BooleanVar, PhotoImage, CENTER, NORMAL, DISABLED
from tkinter.ttk import Frame, Label, Button, Entry, Radiobutton, Checkbutton, Progressbar, OptionMenu
from tkinter.messagebox import askquestion, showwarning, showerror, showinfo
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

	WINCMD_TIMEOUT = 20
	WINCMD_RETRIES = 20
	WINCMD_DELAY = 1
	BLOCKSIZES = (
		'auto',
		'512',
		'1024',
		'2048',
		'4096',
		'8192',
		'16384',
		'32768',
		'65536',
		'131072',
		'262144',
		'524288'
	)

	def __init__(self, parentpath):
		'Generate Windows tools'
		self.conn = WMI()
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

	def zerod_launch(self, targetpath, blocksize=None, extra=False, writeff=False, verify=False, check=False):
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
		if writeff:
			cmd.append('/f')
		if check:
			cmd.append('/c')
		else:
			if extra:
				cmd.append('/x')
			if verify:
				cmd.append('/v')
		return self.cmd_launch(cmd)

	def zerod_get_size(self, targetpath):
		'Use zerod.exe toget file or disk size'
		proc = self.cmd_launch([self.zerod_path, targetpath, '/p'])
		proc.wait(timeout=self.WINCMD_TIMEOUT)
		try:
			return proc.stdout.read().split()[4]
		except:
			return

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
			sleep(self.WINCMD_DELAY)
		return stillmounted

	def run_diskpart(self, script):
		'Run diskpart script'
		self.tmpscriptpath.write_text(script)
		proc = self.cmd_launch(['diskpart', '/s', self.tmpscriptpath])
		proc.wait()
		try:
			self.tmpscriptpath.unlink()
		except:
			pass
		return

	def clean_table(self, driveid):
		'Clean partition table using diskpart'
		try:
			driveno = driveid[17:]
		except:
			return
		return self.run_diskpart(f'''select disk {driveno}
clean
'''
		)

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
			sleep(self.WINCMD_DELAY)

class Logging:
	'Log to file'
	
	def __init__(self, parentpath):
		'Generate logging'
		self.log_header_path = parentpath / 'logheader.txt'
	
	def start_log(self, info):
		'Open log file'
		self.log_string = info
		self.append_log(f'\n{self.timestamp()}\n')

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

class Gui(Tk, WinUtils, Logging):
	'GUI look and feel'

	PAD = 4
	BARLENGTH = 400
	WARNING_FG = 'red'
	WARNING_BG = 'white'
	
	SIZEBASE = (
		{ 'PiB': 2**50, 'TiB': 2**40, 'GiB': 2**30, 'MiB': 2**20, 'kiB': 2**10 },
		{ 'PB': 10**15, 'TB': 10**12, 'GB': 10**9, 'MB': 10**6, 'kB': 10**3 }
	)

	def __init__(self):
		'Base Configuration'
		self.__file_parentpath__ = Path(__file__).parent
		self.conf = Config(self.__file_parentpath__/'hdzero.conf')
		self.conf.read()
		WinUtils.__init__(self, self.__file_parentpath__)
		self.i_am_admin = self.is_user_an_admin()
		Logging.__init__(self, self.__file_parentpath__)
		self.app_info_str = self.conf['TEXT']['title'] + f' v{__version__}'
		Tk.__init__(self)
		self.title(self.app_info_str)
		self.app_icon = PhotoImage(file=self.__file_parentpath__/'icon.png')
		self.iconphoto(False, self.app_icon)
		self.resizable(False, False)
		self.mainframe_user_opts = dict()
		self.gen_main_frame()

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

	def clear_frame(self, frame):
		'Destroy all widgets in frame'
		for child in frame.winfo_children():
			child.destroy()

	def disable_frame(self, frame):
		'Disable all widgets that have this option in frame'
		for child in frame.winfo_children():
			try:
				child.configure(state=DISABLED)
			except:
				pass

	def gen_main_frame(self):
		'Define Main Frame'
		try:
			self.main_frame.destroy()
		except AttributeError:
			pass
		self.main_frame = Frame(self)
		self.main_frame.pack()
		### HEAD ###
		frame = Frame(self.main_frame)
		frame.pack(fill='both', expand=True)
		Label(
			frame,
			text = self.conf['TEXT']['head'],
			padding = self.PAD
		).pack(fill='both', expand=True, side='left')
		if not self.i_am_admin:
			frame = Frame(self.main_frame)
			frame.pack(fill='both', expand=True)
			Label(
				frame,
				text = self.conf['TEXT']['notadmin'],
				foreground = self.WARNING_FG,
				background = self.WARNING_BG,
				padding=self.PAD
			).pack(fill='both', expand=True, side='left')
		### DRIVES ###
		self.drives_frame = Frame(self.main_frame, padding=self.PAD)
		self.drives_frame.pack(fill='both', expand=True)
		self.selected_target = StringVar()
		self.fill_drives_frame()
		### FILE(S) ###
		frame = Frame(self.main_frame)
		frame.pack(fill='both', expand=True)
		Radiobutton(
				frame,
				text = self.conf['TEXT']['files'],
				command = self.enable_start_button,
				variable = self.selected_target,
				value = 'files',
				padding = (self.PAD*2, 0)
		).pack(side='left')
		self.mainframe_user_opts['deletefiles'] = BooleanVar(value=self.conf['DEFAULT']['deletefiles'])
		Checkbutton(
			frame,
			text = self.conf['TEXT']['deletefiles'],
			variable = self.mainframe_user_opts['deletefiles'],
			onvalue = True,
			offvalue = False,
			padding = (self.PAD*2, 0)
		).pack(side='right')
		### DISK OPTIONS FRAME ###
		frame = Frame(self.main_frame, padding=self.PAD)
		frame.pack(fill='both', expand=True)
		Button(frame, text=self.conf['TEXT']['refresh'],
			command=self.refresh_drives_frame).grid(row=0, column=0, sticky='w')
		self.mainframe_user_opts['parttable'] = StringVar(value=self.conf['DEFAULT']['parttable'])
		Radiobutton(frame, variable=self.mainframe_user_opts['parttable'], value='None',
			text=self.conf['TEXT']['no_diskpart'], padding=(self.PAD*4, 0)).grid(row=0, column=1, sticky='w')
		Radiobutton(frame, variable=self.mainframe_user_opts['parttable'],
			value='gpt',
			text='GPT', padding=(self.PAD*4, 0)).grid(row=1, column=1, sticky='w')
		Radiobutton(frame, variable=self.mainframe_user_opts['parttable'],
			value='mbr',
			text='MBR', padding=(self.PAD*4, 0)).grid(row=2, column=1, sticky='w')
		self.mainframe_user_opts['fs'] = StringVar(value=self.conf['DEFAULT']['fs'])
		Radiobutton(frame, variable=self.mainframe_user_opts['fs'],
			value='ntfs', text='NTFS', padding=(self.PAD*2, 0)).grid(row=0, column=2, sticky='w')
		Radiobutton(frame, variable=self.mainframe_user_opts['fs'],
			value='exfat', text='exFAT', padding=(self.PAD*2, 0)).grid(row=1, column=2, sticky='w')
		Radiobutton(frame, variable=self.mainframe_user_opts['fs'],
			value='fat32', text='FAT32', padding=(self.PAD*2, 0)).grid(row=2, column=2, sticky='w')
		Label(frame, text=self.conf['TEXT']['volname']+':', padding=(self.PAD, 0)
			).grid(row=0, column=3, sticky='e')
		self.mainframe_user_opts['volname'] = StringVar(value=self.conf['DEFAULT']['volname'])
		Entry(frame, textvariable=self.mainframe_user_opts['volname']).grid(row=0, column=4, sticky='w')
		self.mainframe_user_opts['writelog'] = BooleanVar(value=self.conf['DEFAULT']['writelog'])
		Checkbutton(frame, text=self.conf['TEXT']['writelog'], variable=self.mainframe_user_opts['writelog'],
			onvalue=True, offvalue=False, padding=(self.PAD, 0)).grid(row=2, column=3, sticky='w')
		Button(frame, text=self.conf['TEXT']['editlog'],
			command=self.edit_log_header).grid(row=2, column=4, sticky='w')
		### OPTIONS FRAME ###
		frame = Frame(self.main_frame, padding=self.PAD)
		frame.pack(fill='both', expand=True)
		self.mainframe_user_opts['extra'] = BooleanVar(value=self.conf['DEFAULT']['extra'])
		Checkbutton(
			frame,
			text = self.conf['TEXT']['extra'],
			variable = self.mainframe_user_opts['extra'],
			onvalue = True,
			offvalue = False,
			padding = (self.PAD, 0)
		).grid(row=0, column=0, sticky='w')
		self.mainframe_user_opts['ff'] = BooleanVar(value=self.conf['DEFAULT']['ff'])
		Checkbutton(
			frame,
			text = self.conf['TEXT']['ff'],
			variable = self.mainframe_user_opts['ff'],
			onvalue = True,
			offvalue = False,
			padding = (self.PAD, 0)
		).grid(row=0, column=1, sticky='w')
		self.mainframe_user_opts['full_verify'] = BooleanVar(value=self.conf['DEFAULT']['full_verify'])
		Checkbutton(
			frame,
			text = self.conf['TEXT']['full_verify'],
			variable = self.mainframe_user_opts['full_verify'],
			onvalue = True,
			offvalue = False,
			padding = (self.PAD, 0)
		).grid(row=0, column=3, sticky='w')
		Label(frame, text=self.conf['TEXT']['blocksize']+':', padding = (self.PAD, 0)
			).grid(row=0, column=4, sticky='w')
		self.mainframe_user_opts['blocksize'] = StringVar(value=self.conf['DEFAULT']['blocksize'])
		OptionMenu(
			frame,
			self.mainframe_user_opts['blocksize'],
			self.conf['DEFAULT']['blocksize'],
			*self.BLOCKSIZES
		).grid(row=0, column=5, sticky='w')
		self.mainframe_user_opts['check'] = BooleanVar(value=self.conf['DEFAULT']['check'])
		Checkbutton(
			frame,
			text = self.conf['TEXT']['check'],
			variable = self.mainframe_user_opts['check'],
			onvalue = True,
			offvalue = False,
			padding = (self.PAD, 0)
		).grid(row=1, column=0, sticky='w')
		### ASK MORE ###
		frame = Frame(self.main_frame, padding=self.PAD)
		frame.pack(fill='both', expand=True)
		self.mainframe_user_opts['askmore'] = BooleanVar(value=self.conf['DEFAULT']['askmore'])
		Checkbutton(
			frame,
			text = self.conf['TEXT']['askmore'],
			variable = self.mainframe_user_opts['askmore'],
			onvalue = True, offvalue = False
		).pack(side='right')
		### BOTTOM ###
		frame = Frame(self.main_frame, padding=self.PAD)
		frame.pack(fill='both', expand=True)
		self.start_button = Button(
			frame,
			text = self.conf['TEXT']['start'],
			command = self.start_work,
			state = DISABLED
		)
		self.start_button.pack(side='left')
		Button(frame, text=self.conf['TEXT']['quit'], command=self.quit_app).pack(side='right')

	def fill_drives_frame(self):
		'Drive section'
		Label(self.drives_frame, text=self.conf['TEXT']['drive']).grid(row=0, column=0, sticky='w')
		Label(self.drives_frame, text=self.conf['TEXT']['mounted']).grid(row=0, column=1, sticky='w')
		Label(self.drives_frame, text=self.conf['TEXT']['details']).grid(row=0, column=2, sticky='w')
		row = 1
		for drive in self.list_drives():
			button = Radiobutton(
				self.drives_frame,
				text = f'{drive.index}',
				command = self.enable_start_button,
				variable = self.selected_target,
				value = drive.index,
				padding=(self.PAD, 0)
			)
			button.grid(row=row, column=0, sticky='w')
			partitions = [ part.Dependent.DeviceID for part in self.get_partitions(drive.index) ]
			p_label = Label(self.drives_frame, text=', '.join(partitions))
			p_label.grid(row=row, column=1, sticky='w')
			Label(
				self.drives_frame,
				text = f'{drive.Caption}, {drive.MediaType} ({self.readable(self.zerod_get_size(drive.DeviceID))})'
			).grid(row=row, column=2, sticky='w')
			if self.i_am_admin:
				if self.this_drive in partitions or 'C:' in partitions:
					p_label.configure(foreground=self.WARNING_FG, background=self.WARNING_BG)
			else:
				button.configure(state=DISABLED)
			row += 1

	def enable_start_button(self):
		'When target has been selected'
		self.start_button.configure(state=NORMAL)

	def decode_settings(self):
		'Decode settings and write as default to config file'
		self.options = { setting: tkvalue.get() for setting, tkvalue in self.mainframe_user_opts.items() } 
		for option, value in self.options.items():
			self.conf['DEFAULT'][option] = str(value)
		self.conf.write()

	def refresh_drives_frame(self):
		self.decode_settings()
		self.clear_frame(self.drives_frame)
		self.start_button.configure(state=DISABLED)
		self.selected_target.set(None)
		self.fill_drives_frame()

	def edit_log_header(self):
		'Edit log header'
		self.withdraw()
		self.notepad_log_header()
		self.deiconify()
		self.refresh_drives_frame()

	def start_work(self):
		'Star work process'
		target = self.selected_target.get()
		if target:
			self.decode_settings()
			if target == 'files':
				self.files_init()
			else:
				self.drive_init(int(target))

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

	def open_work_frame(self):
		'Open frame to show progress and iconify main frame'
		self.withdraw()
		self.work_frame = Toplevel(self)
		self.work_frame.iconphoto(False, self.app_icon)
		self.work_frame.title(self.conf['TEXT']['title'])
		self.work_frame.resizable(False, False)
		frame = Frame(self.work_frame)
		frame.pack(fill='both', expand=True)
		self.head_info = StringVar()
		Label(frame, textvariable=self.head_info).pack(padx=self.PAD, pady=self.PAD)
		self.main_info = StringVar()
		Label(frame, textvariable=self.main_info).pack(padx=self.PAD, pady=self.PAD)
		self.progressbar = Progressbar(frame, mode='indeterminate', length=self.BARLENGTH)
		self.progressbar.pack(padx=self.PAD, pady=self.PAD, fill='both', expand=True)
		self.progressbar.start()
		self.progress_info = StringVar()
		Label(frame, textvariable=self.progress_info).pack(padx=self.PAD, pady=self.PAD)
		self.quit_button = Button(
			frame,
			text = self.conf['TEXT']['quit'],
			command = self.set_quit_work
		)
		self.quit_button.pack(padx=self.PAD, pady=self.PAD, side='right')
		self.quit_work = False

	def close_work_frame(self):
		'Close work frame and show main'
		self.deiconify()
		self.refresh_drives_frame()
		self.work_frame.destroy()

	def set_quit_work(self):
		'Write config an quit'
		if self.confirm(self.conf['TEXT']['abort']):
			self.quit_work = True
			self.close_work_frame()

	def watch_zerod(self, files_of_str = ''):
		'Handle output of zerod'
		of_str = self.conf['TEXT']['of']
		bytes_str = self.conf['TEXT']['bytes']
		are_str = self.conf['TEXT']['are']
		pass_of_str = ''
		for msg_raw in self.zerod_proc.stdout:
			msg_split = msg_raw.split()
			msg = msg_raw.strip()
			####################################################################################
			print("DEBUG zerod:", msg)
			####################################################################################
			info = None
			if msg_split[0] == '...':
				progress_str = files_of_str + pass_of_str + ' '
				progress_str += f'{msg_split[1]} {of_str} {msg_split[3]} {bytes_str}'
				self.progress_info.set(progress_str)
				self.progressbar['value'] = 100 * float(msg_split[1]) / float(msg_split[3])
			elif msg_split[0] == 'Calculating':
				continue
			elif msg_split[0] == 'Pass':
				if self.options['extra']:
					pass_of_str = self.conf['TEXT']['pass'] + f' {msg_split[1]} {of_str} {msg_split[3]}'
			elif msg_split[0] == 'Testing':
				self.main_info.set(self.conf['TEXT']['testing_blocksize'] + f' {msg_split[3]} {bytes_str}')
			elif msg_split[0] == 'Using':
				self.blocksize = msg_split[4]
				self.main_info.set(self.conf['TEXT']['using_blocksize'] + f' {self.blocksize} {bytes_str}')
			elif msg_split[0] == 'Verifying':
				pass_of_str = ''
				info = self.conf['TEXT']['verifying']
				self.main_info.set(info)
			elif msg_split[0] == 'Verified':
				info = self.conf['TEXT']['verified'] + f' {msg_split[1]} {bytes_str}'
				self.main_info.set(info)
			elif msg_split[0] == 'All':
				info = f'{msg_split[2]} {bytes_str} {are_str} {msg_split[5]}'
				self.main_info.set(info)
			elif msg_split[0] == 'Warning:':
				info = msg
				self.main_info.set(info)
			elif msg_split[0] == 'Retrying':
				info = msg
				self.progress_info.set(info)
			else:
				info = msg
			if info and self.options['writelog']:
				self.append_log(info)
			if self.quit_work:
				return

	def drive_init(self, diskindex):
		'Wipe selected disk - launch thread'
		drive = self.get_drive(diskindex)
		driveletters = [ part.Dependent.DeviceID for part in self.get_partitions(diskindex) ]
		if not self.options['check']:
			question = self.conf['TEXT']['drivewarning']
			question += f'\n\n{drive.DeviceID}\n{drive.Caption}, {drive.MediaType}\n'
			question += self.readable(drive.Size) + '\n'
			mounted = ''
			for driveletter in driveletters:
				mounted += f'\n{driveletter}'
				if driveletter == self.this_drive or driveletter == 'C:':
					mounted += ' - ' + self.conf['TEXT']['danger']
			if mounted != '':
				question += self.conf['TEXT']['mounted'] + mounted
			else:
				question += self.conf['TEXT']['nomounted']
			if not self.confirm(question):
				self.mainframe()
				return
			stillmounted = self.dismount_drives(driveletters)
			if stillmounted:
				warning = self.conf['TEXT']['not_dismount'] + ' '
				warning += ', '.join(stillmounted)
				warning += '\n\n' + self.conf['TEXT']['dismount_manually']
				showwarning(title=self.conf['TEXT']['warning_title'], message=warning)
				self.mainframe()
				return
		if driveletters != list():
			self.driveletter = driveletters[0]
		else:
			self.driveletter = None
		if self.options['writelog']:
			self.start_log(
				f'{self.app_info_str}\n{drive.Caption}, {drive.MediaType}, {self.readable(drive.Size)}'
			)
		self.work_target = drive.DeviceID
		self.open_work_frame()
		Thread(target=self.drive_worker).start()

	def drive_worker(self):
		'Worker for drive'
		self.head_info.set(self.work_target)
		if not self.options['check']:
			self.main_info.set(self.conf['TEXT']['cleaning_table'])
			self.clean_table(self.work_target)
		if self.quit_work:
			self.close_work_frame()
			return
		self.progressbar.stop()
		self.progressbar.configure(mode='determinate')
		self.progressbar['value'] = 0
		self.zerod_proc = self.zerod_launch(
			self.work_target,
			blocksize = self.options['blocksize'],
			extra = self.options['extra'],
			writeff = self.options['ff'],
			verify = self.options['full_verify'],
			check = self.options['check']
		)
		self.watch_zerod()
		if self.quit_work:
			self.zerod_proc.terminate()
			self.close_work_frame()
			return
		if self.zerod_proc.wait() != 0:
			showerror(
				self.conf['TEXT']['error'],
				self.conf['TEXT']['errorwhile'] + f' {self.work_target} \n\n {self.zerod_proc.stderr.read()}'
			)
			self.close_work_frame()
			return
		self.progress_info.set('')
		self.quit_button.configure(state=DISABLED)
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
			self.progressbar.stop()
			self.progressbar.configure(mode='determinate')
		self.progressbar['value'] = 100
		self.main_info.set('')
		self.progress_info.set('')
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
		showinfo(message=self.conf['TEXT']['all_done'])
		self.close_work_frame()

	def files_init(self):
		'Wipe selected file or files - launch thread'
		if self.options['check']:
			self.work_target = askopenfilenames(
				title=self.conf['TEXT']['filestocheck'],
				initialdir=self.conf['DEFAULT']['initialdir']
			)
		else:
			self.work_target = askopenfilenames(
				title=self.conf['TEXT']['filestowipe'],
				initialdir=self.conf['DEFAULT']['initialdir']
			)
		if len(self.work_target) < 1:
			self.refresh_main()
			return
		if not self.options['check']:
			question = self.conf['TEXT']['filewarning']
			question += self.list_to_string(self.work_target)
			if not self.confirm(question):
				self.refresh_main()
				return
		self.conf['DEFAULT']['initialdir'] = str(Path(self.work_target[0]).parent)
		self.decode_settings()
		self.options['writelog'] = False
		self.open_work_frame()
		Thread(target=self.files_worker).start()

	def files_worker(self):
		'Worker for files'
		of_str = self.conf['TEXT']['of']
		files_of_str = ''
		qt_files = len(self.work_target)
		file_cnt = 0
		errors = list()
		self.blocksize = self.options['blocksize']
		for file in self.work_target:
		
			print('DEBUG:', file, type(file))
		
			self.head_info.set(file)
			return
			self.zerod_proc = self.zerod_launch(
				file,
				blocksize = self.blocksize,
				extra = self.options['extra'],
				writeff = self.options['ff'],
				verify = self.options['full_verify'],
				check = self.options['check']
			)
			if qt_files > 1:
				file_cnt += 1
				files_of_str = self.conf['TEXT']['file'] + f' {file_cnt} {of_str} {qt_files}, '
			self.watch_zerod(files_of_str=files_of_str)
			if not self.working:
				return
			if self.zerod_proc.wait() == 0:
				if self.options['deletefiles'] and not self.options['check']:
					self.main_info.set(self.conf['TEXT']['deleting_file'])
					try:
						Path(file).unlink()
					except:
						errors.append(file)
			else:
				errors.append(file)
		if qt_files > 1:
			self.head_info.set(f'{qt_files} ' + self.conf['TEXT']['files'])
		else:
			self.head_info.set('1 ' + self.conf['TEXT']['file'])
		self.main_info.set('')
		self.progress_info.set('')
		if errors == list():
			showinfo(message=self.conf['TEXT']['all_done'])
		else:
			self.main_info.set(self.conf['TEXT']['errorwhile'])
			showerror(
				self.conf['TEXT']['error'],
				self.conf['TEXT']['errorwhile'] + self.list_to_string(errors)
			)
		#self.main_info.set(self.conf['TEXT']['all_done'])
		#showinfo(message=self.conf['TEXT']['all_done'])
		self.close_work_frame()

if __name__ == '__main__':  # start here
	Gui().mainloop()
