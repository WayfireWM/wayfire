#!/usr/bin/python

import pathlib
import subprocess

module_directory = pathlib.Path(__file__).resolve().parent
locale_directory = module_directory.parent / "locale"

for xml_file in locale_directory.glob("*/LC_MESSAGES/*.po"):
    subprocess.run(["msgcat", "--no-location", "-o", xml_file, xml_file])
