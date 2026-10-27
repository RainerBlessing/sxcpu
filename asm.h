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
 *   samepage n continue at the start of the next page if the next n (hexadecimal)
 *              words would not fit on the rest of this page; for a routine that
 *              has to stay on one page
 *   autopage on|off  put "page n" in front of every jmp and call to a label on
 *              another page (off at the start). Code at an address is taken
 *              to run with the page of that address, so a callee on another
 *              page has to end with "retp". Code that runs on into the next
 *              page without a jump gets a "page" there as well (but not after
 *              an org: whoever jumps there sets the page).
 *
 * Macros (expanded before anything else, so a macro can contain every line of this list):
 *   name macro p1, p2   starts the definition of a macro, up to the line
 *   endm                the end of the definition
 *   local a, b          inside of a macro: labels that get a different name in every
 *                       expansion, so the macro can be used more than once
 *   name x, y           a call; "name(x, y)" works as well, and a label may stand in
 *                       front of it (it marks the first word of the expansion)
 * Every parameter in the body (as a whole word) is replaced by the text of the argument,
 * which can be a name, a number or any text without a comma, e.g. "#5" or "w". The number of
 * arguments must match. A macro must be defined before its first call, can call other macros
 * (up to 16 levels deep, no recursion) but not define one. Its name is case sensitive and
 * cannot be a mnemonic, a keyword of this list or w, m, option, wdt. An error in an
 * expansion is reported with the line of the call and, in brackets, the macro and the
 * line of the macro the mistake is in: "line 8: cannot assemble 'x' (in macro inner, line
 * 3, called from macro outer, line 6)".
 *
 * Expressions: wherever a number is allowed (operands, org, equ, samepage, dw) there can
 * be an expression with the operators of C: * / + - << >> & ^ | and the unary - + ~, and
 * parentheses. Its parts are numbers, names and $, the address of the line (of the
 * word of the instruction, of the value of a dw). Numbers are hexadecimal (1f, 0x1f),
 * d'31' is decimal and b'11111' is binary (0b1 is still the hexadecimal 0b1). The value
 * is at most 24 bits, a shift count 0 to 24; division by zero is an error. A value that
 * is negative after all is an error, except behind "#": "retw #-1" and "and w, #~8"
 * give ff and f7. The syntax of the instructions stays: # / ++ -- << >> in front of an
 * operand, -w behind it, !port, w, m and the bit number behind the dot in clrb, setb,
 * sb and snb ("setb flags+1.n+1"). An expression that uses a label or $ is an address
 * and is handled as one by jmp and call (low bits, autopage). Examples:
 *   mov w, #size*2-1      jmp loop+1      jmp $+2      and w, #~(1<<3)
 * A leading # / ++ -- << >> is always the prefix of the instruction; to use the operator
 * instead, start the operand with a parenthesis.
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
