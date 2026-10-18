# sxcpu

The core of the SX52 emulator that [xgsemu](https://github.com/RainerBlessing/xgsemu)
and [sxemu](https://github.com/RainerBlessing/sxemu) share. It has no
dependency on SDL, wxWidgets or curses.

- `Cpu` (`cpu.h`): the SX52 with ROM, RAM, SRAM, ports, timer and the joystick
  interface of the XGS.
- `VideoPort` and `SoundPort` (`cpu_ports.h`): what the CPU needs from the
  front end. Pass `NULL` as video for a front end without screen.
- `disassemble()` (`disasm.h`): opcode to text for a debugger.
- `assemble()` (`asm.h`): one line of assembler to an opcode, the reverse of `disassemble()`.
- `assembleSource()` (`asm.h`): a whole source text with labels, `org`, `equ` and `dw` into a ROM image.

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
