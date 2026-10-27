# sxcpu

The core of the SX52 emulator that [xgsemu](https://github.com/RainerBlessing/xgsemu)
and [sxemu](https://github.com/RainerBlessing/sxemu) share. It has no
dependency on SDL, wxWidgets or curses.

- `Cpu` (`cpu.h`): the SX52 with ROM, RAM, ports and timer.
- `PortDevice` (`cpu_ports.h`): what hangs on the pins of the ports RA..RE (a screen, a
  sound chip, a joystick, a RAM). The CPU calls it when a port is written or has another
  value, a front end implements it. Pass `NULL` for a CPU without any device.
- `disassemble()` (`disasm.h`): opcode to text for a debugger.
- `assemble()` (`asm.h`): one line of assembler to an opcode, the reverse of `disassemble()`.
- `assembleSource()` (`asm.h`): a whole source text with labels, `org`, `equ`, `dw`, expressions (`jmp loop+1`, `#~(1<<3)`, `$`, `d'10'`) and macros (`name macro a, b` ... `endm`) into a ROM image.

## Use it

Add the repository as a git submodule and link it with CMake:

```cmake
add_subdirectory(sxcpu)
target_link_libraries(myprogram sxcpu)
```

## Tests

The tests of the CPU, the assembler and the disassembler are in `tests/` and need only
GoogleTest (CMake downloads it):

```bash
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

A project that uses sxcpu sets `SXCPU_BUILD_TESTS` to build them as well. The tests that need
the video, the sound chip or program files are part of xgsemu.

## License

GNU General Public License, version 2, see `COPYING`.
