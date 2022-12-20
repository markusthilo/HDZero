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
from customtkinter import CTk
from customtkinter import set_appearance_mode
from customtkinter import set_default_color_theme
from customtkinter import CTkFrame, CTkButton, CTkLabel
from customtkinter import CTkEntry, CTkRadioButton
from tkinter import StringVar
from tkinter.messagebox import askquestion

class Config(ConfigParser):
	'Handle config file hdprepare.conf'

	def read(self):
		'Open config file'
		path = Path(__file__)
		self.path = (path.parent / path.stem).with_suffix('.conf')
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
		for drive in self.conn.Win32_DiskDrive():
			yield drive

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

class Gui(CTk, WinUtils):
	'GUI look and feel'

	PAD = 10
	BIGPAD = 20

	def __init__(self, config):
		'Base GUI'
		self.conf = config
		set_appearance_mode(self.conf['APPEARANCE']['mode'])
		set_default_color_theme(self.conf['APPEARANCE']['color_theme'])
		CTk.__init__(self)
		WinUtils.__init__(self)
		self.title(self.conf['TEXT']['title'])
		self.main()

	def main(self):
		self.main_frame = CTkFrame(self)
		self.main_frame.pack()
		drivetext = self.conf['TEXT']['drive']
		for drive in self.list_drives():
			print(drive)
			frame = CTkFrame(self.main_frame)
			frame.pack(padx=self.PAD, pady=self.PAD, fill='both', expand=True)
			CTkButton(frame, text=f'{drivetext} {drive.Index}', command=partial(self.prepdisk, drive.Index)).pack(
				padx=self.PAD, pady=self.PAD, side='left')
			CTkLabel(frame, text=f'{drive.Caption}, {drive.MediaType} ({self.readable(drive.Size)})').pack(
				padx=self.PAD, pady=self.PAD, anchor='w')
		opt_frame = CTkFrame(self.main_frame)
		opt_frame.pack(padx=self.PAD, pady=self.BIGPAD, fill='both', expand=True)
		CTkButton(opt_frame, text=self.conf['TEXT']['refresh'], command=self.refresh).pack(
			padx=self.PAD, pady=self.PAD, side='left')
		self.fs = StringVar(value=self.conf['DEFAULT']['fs'])
		CTkRadioButton(master=opt_frame, variable=self.fs, value='NTFS', text='NTFS').pack(
			padx=self.PAD, pady=self.PAD, side='left')
		CTkRadioButton(master=opt_frame, variable=self.fs, value='exFAT', text='exFAT').pack(
			padx=self.PAD, pady=self.PAD, side='left')
		volname = self.conf['TEXT']['volname']
		CTkLabel(opt_frame, text=f'{volname}:').pack(
			padx=self.PAD, pady=self.PAD, side='left')
		self.volname = StringVar(value=self.conf['DEFAULT']['volname'])
		CTkEntry(opt_frame, textvariable=self.volname).pack(
			padx=self.PAD, pady=self.PAD, side='left')
		CTkButton(self.main_frame, text=self.conf['TEXT']['quit'], command=self.destroy).pack(
			padx=self.PAD, pady=self.PAD, side='right')

	def prepdisk(self, diskindex):
		'Prepare selected disk'
		self.fs = self.fs.get()
		self.volname = self.volname.get()
		self.conf['DEFAULT']['fs'] = self.fs
		self.conf['DEFAULT']['volname'] = self.volname
		self.conf.write()
		drive = self.get_drive(diskindex)
		question = f'{drive.DeviceId}\n{drive.Caption}, {drive.MediaType}\n'
		question += self.readable(drive.Size)
		question += '\n\n'
		question += self.conf['TEXT']['partitions']

					#part.Dependent.DeviceID,
					#part.Antecedent.DeviceID,
					#part.Dependent.Description,
					#part.Dependent.Size
		
		for part in self.list_partitions(diskindex):
			question += f'\n\n{part.Dependent.DeviceID}\n{part.Antecedent.DeviceID}\n{part.Dependent.Description}\n'
			question += self.readable(part.Dependent.Size)
		question += '\n\n\n'
		question += self.conf['TEXT']['areyoushure']
		if askquestion(self.conf['TEXT']['title'], question) == 'yes':
			print('Work to do')
		else:
			print('Abort')
		self.refresh()
		return

	def refresh(self):
		self.main_frame.destroy()
		self.main()
		
	def readable(self, size):
		'Genereate readable size string'
		try:
			size = int(size)
		except TypeError:
			return self.conf['TEXT']['undetected']
		outstr = f'{size} B'
		for base in (
				{
					'PiB': 1125899906842620,
					'TiB': 1099511627776,
					'GiB': 1073741824,
					'MiB': 1048576,
					'kiB': 1024,
				}, 
				{
					'PB': 1000000000000000,
					'TB': 1000000000000,
					'GB': 1000000000,
					'MB': 1000000,
					'kB': 1000,
				}
			):
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


if __name__ == '__main__':  # start here
	config = Config()
	config.read()
	Gui(config).mainloop()
