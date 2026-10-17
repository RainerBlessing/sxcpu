#ifndef ASM_H_
#define ASM_H_
#include <map>
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
 *   firsthalf  continue at the start of the next page if the address is in the
 *              second half of a page; a call reaches only the first half
 *   autopage on|off  put "page n" in front of every jmp and call to a label on
 *              another page (off at the start). Code at an address is taken
 *              to run with the page of that address, so a callee on another
 *              page has to end with "retp". Code that runs on into the next
 *              page without a jump gets a "page" there as well (but not after
 *              an org: whoever jumps there sets the page).
 * A name can be used in place of a number in every instruction, also before
 * its definition. The registers have names: indf, rtcc, pc, status, fsr, ra to re.
 * w, m, option and wdt are reserved. Names are case sensitive.
 * jmp and call may use a label instead of an address; jmp keeps the low
 * nine bits of its address, call the low eight. A call to a label in the
 * second half of a page (bit 8 set) cannot be coded and is an error. The page
 * the address belongs to is chosen by "page" or autopage. A label wins over a
 * number of the same spelling.
 *
 * Words that nothing was assembled into get 0xFFF (unused). Two pieces of code
 * must not share a word, that is an error.
 * Returns false and sets *error to "line N: ..." for the first mistake. If `labels` is
 * given it gets the address of every label (not of the names from equ and not of the
 * registers) when the source is assembled.
 */
bool assembleSource(const std::string& source, unsigned short* rom, int romSize, std::string* error,
                    std::map<std::string, int>* labels = 0);

#endif /*ASM_H_*/
