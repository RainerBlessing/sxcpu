#ifndef ASM_H_
#define ASM_H_
#include <string>

/**
 * Turns one line of SX52 assembler into an opcode, e.g. "mov w, #a" -> 0xC0A.
 * The syntax is the one disassemble() writes: numbers are hexadecimal (a
 * leading 0x is allowed), case and blanks do not matter, ';' starts a comment.
 * Returns false if the line is no valid instruction; opcode is not changed then.
 */
bool assemble(const std::string& line, unsigned short* opcode);

#endif /*ASM_H_*/
