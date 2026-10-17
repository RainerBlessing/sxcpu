#include "asm.h"
#include "disasm.h"
#include <cctype>
#include <map>

using namespace std;

// "0x0A" and "0a" become "a", "0" stays "0".
static string plainNumber(string run)
{
	if (run.compare(0, 2, "0x") == 0)
		run.erase(0, 2);
	size_t zeros = run.find_first_not_of('0');
	if (zeros == string::npos)
		return run.empty() ? run : "0";
	return run.substr(zeros);
}

// One spelling for every way to write an instruction: lower case, the
// mnemonic, a blank and the operands without any blanks, numbers without
// 0x and leading zeros. "MOV  W , #0x0A ; comment" -> "mov w,#a".
static string normalize(const string& line)
{
	string text = line.substr(0, line.find(';'));
	size_t first = text.find_first_not_of(" \t");
	if (first == string::npos)
		return "";
	size_t end = text.find_first_of(" \t", first);
	string mnemonic = text.substr(first, end == string::npos ? end : end - first);
	string operands;
	for (size_t i = end; end != string::npos && i < text.size(); i++)
	{
		if (!isspace((unsigned char) text[i]))
			operands += tolower((unsigned char) text[i]);
	}

	string out;
	for (size_t i = 0; i < mnemonic.size(); i++)
		out += tolower((unsigned char) mnemonic[i]);
	if (operands.empty())
		return out;

	out += ' ';
	for (size_t i = 0; i < operands.size();)
	{
		if (!isalnum((unsigned char) operands[i]))
		{
			out += operands[i++];
			continue;
		}
		size_t run = i;
		while (run < operands.size() && isalnum((unsigned char) operands[run]))
			run++;
		string word = operands.substr(i, run - i);
		// words like "w", "ra" or "option" are no numbers
		bool number = isdigit((unsigned char) word[0]) != 0;
		out += number ? plainNumber(word) : word;
		i = run;
	}
	return out;
}

bool assemble(const string& line, unsigned short* opcode)
{
	// every valid opcode written the way disassemble() does it
	static map<string, unsigned short> byText;
	if (byText.empty())
	{
		for (unsigned short op = 0; op < 0x1000; op++)
		{
			string text = disassemble(op);
			if (text == "unused" || text.compare(0, 8, "not impl") == 0)
				continue;
			byText[normalize(text)] = op;
		}
	}
	map<string, unsigned short>::const_iterator found = byText.find(normalize(line));
	if (found == byText.end())
		return false;
	*opcode = found->second;
	return true;
}
