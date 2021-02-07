from distutils.core import setup
import py2exe 

class Target:
    def __init__(self, **kw):
        self.__dict__.update(kw)
        self.version = "1.1.0.0"
        self.company_name = "Intelitrader"
        self.copyright = "Copyright (c) 2020 Intelitrader."
        self.name = "TioLogReplay"

target = Target(
    description = "Player of tiodb log transaction.",
    script = "tiologreplay.py",
    dest_base = "TioLogReplay")

setup(
	options = {'py2exe': {'bundle_files': 1, 'compressed': True}},
	console=[target]
	)
