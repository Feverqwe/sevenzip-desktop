#!/usr/bin/env python3
"""Generate the viewer index from an installed notices directory."""
import argparse
import sys
sys.dont_write_bytecode = True
from lib.package_notices import write_notice_index

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory")
    args = parser.parse_args()
    write_notice_index(args.directory)
