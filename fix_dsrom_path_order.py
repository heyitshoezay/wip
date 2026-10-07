#!/usr/bin/env python3
"""Run once from the repo root:   python3 fix_dsrom_path_order.py

Why: the ROM packer (ds-rom) places files in the order listed in base_dsrom/path_order.txt.  A folder that was not in your
original ROM (like 'waves', the streamed music) is not on that list, so the packer creates a table entry for it but never
writes its data (size 0).  This adds a few lines to pack() in scripts/dsrom_bridge.py that append any folder or file of
base/root that is missing from path_order.txt, right before the ROM is built.  Nothing else changes.
Safe to run twice (it checks for its own marker).
"""
import sys

PATH = "scripts/dsrom_bridge.py"
MARK = "# --- keep new files (waves/) in path_order.txt ---"
ANCHOR = "    shutil.copytree(filesys, dsrom_files)\n\n    overlays_yaml_path"

NEW = '''    shutil.copytree(filesys, dsrom_files)

    ''' + MARK + '''
    order_path = os.path.join(dsrom_dir, "path_order.txt")
    if os.path.isfile(order_path):
        with open(order_path, encoding="utf-8") as f:
            listed = [l.rstrip("\\r\\n") for l in f if l.strip()]
        added = []

        def covered(p):
            # a path is placed if it, or one of its parents, is listed; or it is a parent of something listed
            return any(p == l or p.startswith(l + "/") or l.startswith(p + "/") for l in listed + added)

        def visit(rel):
            p = "/" + rel if rel else ""
            if rel and not covered(p):
                added.append(p)
                return
            full = os.path.join(dsrom_files, rel) if rel else dsrom_files
            if os.path.isdir(full) and not (rel and any(p == l or p.startswith(l + "/") for l in listed + added)):
                for name in sorted(os.listdir(full)):
                    visit(rel + "/" + name if rel else name)

        visit("")
        if added:
            with open(order_path, "a", encoding="utf-8") as f:
                for p in added:
                    f.write(p + "\\n")
            print("path_order.txt: added", ", ".join(added))

    overlays_yaml_path'''

def main():
    s = open(PATH, encoding="utf-8").read()
    if MARK in s:
        print("already applied"); return
    if s.count(ANCHOR) != 1:
        sys.exit("could not find the place to insert in %s (is this the right repo / an edited file?)" % PATH)
    open(PATH, "w", encoding="utf-8").write(s.replace(ANCHOR, NEW, 1))
    print("done: %s patched" % PATH)

main()
