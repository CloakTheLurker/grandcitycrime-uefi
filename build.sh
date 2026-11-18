clang --target=x86_64-unknown-windows -ffreestanding -fno-stack-protector -fshort-wchar -mno-red-zone -mno-stack-arg-probe -O2 -fno-unroll-loops -mllvm -inline-threshold=25 -fno-builtin -fno-math-errno -c main.c -o main.obj
lld-link -subsystem:efi_application -entry:efi_main -nodefaultlib -out:game.efi main.obj
