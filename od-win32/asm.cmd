nasm -f win32 fpux86_80.asm
nasm -f win64 fpux64_80.asm

nasm -w-orphan-labels -f win32 hq2x32.asm
nasm -w-orphan-labels -f win32 hq3x32.asm
nasm -w-orphan-labels -f win32 hq4x32.asm
nasm -w-orphan-labels -f win32 hq2x16.asm
nasm -w-orphan-labels -f win32 hq3x16.asm
nasm -w-orphan-labels -f win32 hq4x16.asm
