#!/usr/bin/python

import pathlib
import subprocess

module_directory = pathlib.Path(__file__).resolve().parent
its_file = module_directory / "wayfire-itstool.xml"
metadata_directory = module_directory.parent / "metadata"
locale_directory = module_directory.parent / "locale"

for xml_file in metadata_directory.glob("*.xml"):
    subprocess.run(["itstool", f"--its={its_file}", "-o", locale_directory / (xml_file.stem + ".pot"), xml_file])
