#!/usr/bin/env python3
"""Fails the build if the firmware image outgrows its budget.

The TICKEY has 8 MB of flash. C-Ink keeps the app image under 2 MB so two OTA
slots, the digest cache and plenty of free space all fit with room to spare.
"""
import os
import sys

BUDGET = 2 * 1024 * 1024
FLASH = 8 * 1024 * 1024

path = sys.argv[1] if len(sys.argv) > 1 else "firmware/build/c-ink.bin"
size = os.path.getsize(path)
print(f"app image: {size / 1024:.0f} KiB ({size / FLASH:.1%} of 8 MB flash, {size / BUDGET:.0%} of the 2 MB budget)")
summary = os.environ.get("GITHUB_STEP_SUMMARY")
if summary:
    with open(summary, "a") as f:
        f.write(f"**Firmware image:** {size / 1024:.0f} KiB, {size / FLASH:.1%} of the TICKEY's 8 MB flash "
                f"(budget {BUDGET // 1024} KiB)\n")
sys.exit(1 if size > BUDGET else 0)
