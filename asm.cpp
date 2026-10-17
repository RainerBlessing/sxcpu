#include "asm.h"
#include "disasm.h"
#include <cctype>
#include <map>
#include <sstream>
#include <vector>

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

namespace
{
	struct SourceLine
	{
		int number;
		int address;
		string text;
	};

	string trim(const string& text)
	{
		size_t first = text.find_first_not_of(" \t\r");
		if (first == string::npos)
			return "";
		return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
	}

	bool isLabelName(const string& name)
	{
		if (name.empty() || isdigit((unsigned char) name[0]))
			return false;
		for (size_t i = 0; i < name.size(); i++)
		{
			if (!isalnum((unsigned char) name[i]) && name[i] != '_')
				return false;
		}
		return true;
	}

	bool parseHex(const string& text, int* value)
	{
		string digits = plainNumber(text);
		if (digits.empty() || digits.size() > 4 || digits.find_first_not_of("0123456789abcdefABCDEF") != string::npos)
			return false;
		istringstream(digits) >> hex >> *value;
		return true;
	}

	string lineError(int number, const string& message)
	{
		ostringstream out;
		out << "line " << number << ": " << message;
		return out.str();
	}

	// "jmp loop" -> "jmp 20" with the address of loop, if the operand is a label
	bool resolveLabel(const string& text, const map<string, int>& labels, string* resolved, string* problem)
	{
		*resolved = text;
		string line = trim(text);
		size_t blank = line.find_first_of(" \t");
		if (blank == string::npos)
			return true;
		string mnemonic;
		for (size_t i = 0; i < blank; i++)
			mnemonic += tolower((unsigned char) line[i]);
		if (mnemonic != "jmp" && mnemonic != "call")
			return true;

		string operand = trim(line.substr(blank));
		map<string, int>::const_iterator label = labels.find(operand);
		if (label == labels.end())
		{
			int number;
			if (!isLabelName(operand) || parseHex(operand, &number))
				return true;
			*problem = "unknown label " + operand;
			return false;
		}
		ostringstream out;
		out << mnemonic << " " << hex << (label->second & (mnemonic == "jmp" ? 0x1ff : 0xff));
		*resolved = out.str();
		return true;
	}
}

bool assembleSource(const string& source, unsigned short* rom, int romSize, string* error)
{
	for (int i = 0; i < romSize; i++)
		rom[i] = 0xfff;

	// first pass: addresses of the labels
	map<string, int> labels;
	vector<SourceLine> lines;
	int address = 0;
	int number = 0;
	istringstream in(source);
	string raw;
	while (getline(in, raw))
	{
		number++;
		string text = trim(raw.substr(0, raw.find(';')));

		size_t colon = text.find(':');
		if (colon != string::npos)
		{
			string name = trim(text.substr(0, colon));
			if (!isLabelName(name))
			{
				*error = lineError(number, "bad label name '" + name + "'");
				return false;
			}
			if (labels.count(name))
			{
				*error = lineError(number, "label " + name + " is defined twice");
				return false;
			}
			labels[name] = address;
			text = trim(text.substr(colon + 1));
		}
		if (text.empty())
			continue;

		if (normalize(text).compare(0, 4, "org ") == 0)
		{
			int target;
			if (!parseHex(normalize(text).substr(4), &target) || target >= romSize)
			{
				*error = lineError(number, "bad org address");
				return false;
			}
			address = target;
			continue;
		}

		SourceLine line = { number, address, text };
		lines.push_back(line);
		address++;
	}

	// second pass: the opcodes
	for (size_t i = 0; i < lines.size(); i++)
	{
		const SourceLine& line = lines[i];
		if (line.address >= romSize)
		{
			*error = lineError(line.number, "address beyond the end of the ROM");
			return false;
		}
		string resolved, problem;
		unsigned short opcode;
		if (!resolveLabel(line.text, labels, &resolved, &problem))
		{
			*error = lineError(line.number, problem);
			return false;
		}
		if (!assemble(resolved, &opcode))
		{
			*error = lineError(line.number, "cannot assemble '" + line.text + "'");
			return false;
		}
		rom[line.address] = opcode;
	}
	return true;
}
