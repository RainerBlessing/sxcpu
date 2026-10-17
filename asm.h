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

/**
 * Assembles a whole source text into a ROM image of romSize words.
 *
 * One instruction per line, in the syntax of assemble(). Besides that:
 *   name:      a label, alone or in front of an instruction
 *   org addr   continue at the (hexadecimal) address
 *   name equ v a name for a number, e.g. "counter equ 10"; no address is used
 *   dw v, ...  raw 12 bit words
 * A name can be used in place of a number in every instruction, also before
 * its definition. The registers have names: rtcc, pc, status, fsr, ra to re.
 * w, m, option and wdt are reserved. Names are case sensitive.
 * jmp and call may use a label instead of an address; jmp keeps the low
 * nine bits of its address, call the low eight. The page the address belongs
 * to is not chosen here, use "page" for that. A label wins over a number of
 * the same spelling.
 *
 * Words that nothing was assembled into get 0xFFF (unused).
 * Returns false and sets *error to "line N: ..." for the first mistake.
 */
bool assembleSource(const std::string& source, unsigned short* rom, int romSize, std::string* error);

#endif /*ASM_H_*/
