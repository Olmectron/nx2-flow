"""The game loader's one piece of logic a computer can try: where each byte of a NUP file kept in parts is (runtime/parts.h).

  python tools/test_parts.py [--cc <C compiler>]       builds tests/parts_test.c with the computer's own compiler and runs it

The compiler is the one named, else HOSTCC, else the first of cc, gcc and clang on the path, else the one devkitPro's msys2 has
for the computer itself (ucrt64: `pacman -S mingw-w64-ucrt-x86_64-gcc`).
"""
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent  # (loader/)


def compiler():
    """The computer's own C compiler: a command, by its path or as a name the path has."""
    devkit = Path(os.environ.get('DEVKITPRO') or 'C:/devkitPro')
    in_devkit = sorted(devkit.glob('msys2/ucrt64/bin/gcc.exe')) + sorted(devkit.glob('msys2/mingw64/bin/gcc.exe'))
    for where in [os.environ.get('HOSTCC'), 'cc', 'gcc', 'clang', *map(str, in_devkit)]:
        if where and (os.path.isabs(where) or shutil.which(where)):
            return where
    sys.exit('No C compiler for this computer was found: name one with --cc (clang, or msys2\'s gcc).')


def main():
    args = sys.argv[1:]
    cc = args[args.index('--cc') + 1] if '--cc' in args else compiler()
    # (a compiler run from outside its own shell, as msys2's is here, finds its back end's and the program's own libraries by
    # its folder being on the path)
    env = dict(os.environ)
    if os.path.isabs(cc):
        env['PATH'] = os.path.dirname(cc) + os.pathsep + env.get('PATH', '')
    out = ROOT / 'build' / ('parts_test.exe' if os.name == 'nt' else 'parts_test')
    out.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(cc.split() + ['-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'runtime'), str(ROOT / 'tests' / 'parts_test.c'), '-o', str(out)],
                   check=True, env=env)
    sys.exit(subprocess.run([str(out)], env=env).returncode)


if __name__ == '__main__':
    main()
