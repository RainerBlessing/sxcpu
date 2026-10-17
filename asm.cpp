#include "asm.h"
#include "disasm.h"
#include <algorithm>
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
	struct Symbol
	{
		int value;
		bool label;		// jmp and call keep only the low bits of a label
	};
	typedef map<string, Symbol> Symbols;

	struct SourceLine
	{
		int number;
		int address;
		bool data;		// dw: text holds the values, otherwise an instruction
		string text;
	};

	string trim(const string& text)
	{
		size_t first = text.find_first_not_of(" \t\r");
		if (first == string::npos)
			return "";
		return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
	}

	string lowerCase(string text)
	{
		for (size_t i = 0; i < text.size(); i++)
			text[i] = tolower((unsigned char) text[i]);
		return text;
	}

	// the first word of a line and what follows it
	string splitWord(const string& line, string* rest)
	{
		size_t end = line.find_first_of(" \t");
		*rest = end == string::npos ? "" : trim(line.substr(end));
		return line.substr(0, end);
	}

	bool isName(const string& name)
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

	// operands of the instructions that are no names of the user
	bool isReservedWord(const string& name)
	{
		return name == "w" || name == "m" || name == "option" || name == "wdt";
	}

	bool parseHex(const string& text, int* value)
	{
		string digits = plainNumber(text);
		if (digits.empty() || digits.size() > 4 || digits.find_first_not_of("0123456789abcdefABCDEF") != string::npos)
			return false;
		istringstream(digits) >> hex >> *value;
		return true;
	}

	// a name of the symbol table or a hexadecimal number
	bool parseValue(const string& text, const Symbols& symbols, int* value)
	{
		Symbols::const_iterator symbol = symbols.find(text);
		if (symbol != symbols.end())
		{
			*value = symbol->second.value;
			return true;
		}
		return parseHex(text, value);
	}

	string hexText(int value)
	{
		ostringstream out;
		out << hex << value;
		return out.str();
	}

	string lineError(int number, const string& message)
	{
		ostringstream out;
		out << "line " << number << ": " << message;
		return out.str();
	}

	// "jmp loop" -> "jmp 20", "mov counter, w" -> "mov 10, w": the names of
	// the operands become numbers. Names that are no symbols must be hex numbers.
	bool substituteNames(const string& text, const Symbols& symbols, string* resolved, string* problem)
	{
		string operands;
		string mnemonic = splitWord(trim(text), &operands);
		int mask = 0xfff;
		string lower = lowerCase(mnemonic);
		if (lower == "jmp")
			mask = 0x1ff;
		else if (lower == "call")
			mask = 0xff;

		string out;
		for (size_t i = 0; i < operands.size();)
		{
			if (!isalnum((unsigned char) operands[i]) && operands[i] != '_')
			{
				out += operands[i++];
				continue;
			}
			size_t end = i;
			while (end < operands.size() && (isalnum((unsigned char) operands[end]) || operands[end] == '_'))
				end++;
			string word = operands.substr(i, end - i);
			bool port = i > 0 && operands[i - 1] == '!';
			Symbols::const_iterator symbol = symbols.find(word);
			if (port || isReservedWord(lowerCase(word)) || isdigit((unsigned char) word[0]))
				out += word;
			else if (symbol != symbols.end() && symbol->second.label && lower == "call" && (symbol->second.value & 0x100))
			{
				*problem = "call target " + word + " is in the second half of a page";
				return false;
			}
			else if (symbol != symbols.end())
				out += hexText(symbol->second.label ? symbol->second.value & mask : symbol->second.value);
			else
			{
				int number;
				if (!parseHex(word, &number))
				{
					*problem = "unknown name " + word;
					return false;
				}
				out += word;
			}
			i = end;
		}
		*resolved = lower + (out.empty() ? "" : " " + out);
		return true;
	}

	// the values of a dw line, one ROM word each
	bool dataValues(const string& text, const Symbols& symbols, vector<int>* values, string* problem)
	{
		istringstream in(text);
		string item;
		while (getline(in, item, ','))
		{
			int value;
			item = trim(item);
			if (!parseValue(item, symbols, &value) || value > 0xfff)
			{
				*problem = "bad value '" + item + "' for dw";
				return false;
			}
			values->push_back(value);
		}
		if (values->empty())
		{
			*problem = "dw needs a value";
			return false;
		}
		return true;
	}

	Symbols registerNames()
	{
		static const char* names[] = { "rtcc", "pc", "status", "fsr", "ra", "rb", "rc", "rd", "re" };
		static const int numbers[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
		Symbols symbols;
		for (int i = 0; i < 9; i++)
		{
			Symbol symbol = { numbers[i], false };
			symbols[names[i]] = symbol;
		}
		return symbols;
	}

	bool define(const string& name, int value, bool label, Symbols* symbols, string* problem)
	{
		if (!isName(name) || isReservedWord(lowerCase(name)))
		{
			*problem = "bad name '" + name + "'";
			return false;
		}
		if (symbols->count(name))
		{
			*problem = name + " is defined twice";
			return false;
		}
		Symbol symbol = { value, label };
		(*symbols)[name] = symbol;
		return true;
	}
}

namespace
{
	enum Kind { NOTHING, ORG, EQU, DW, CODE, AUTOPAGE };

	struct Item
	{
		int number;		// line of the source
		Kind kind;
		string label;		// defined in front of the item, may be empty
		string name;		// EQU: the name
		string text;		// the rest of the line
		bool autopage;		// CODE: page instructions are inserted
		int address;		// where the item (or its inserted page) starts
	};

	// Cuts the source into items.
	bool parseSource(const string& source, vector<Item>* items, string* error)
	{
		istringstream in(source);
		string raw;
		int number = 0;
		bool autopage = false;
		while (getline(in, raw))
		{
			number++;
			string text = trim(raw.substr(0, raw.find(';')));
			Item item = { number, NOTHING, "", "", "", autopage, 0 };

			size_t colon = text.find(':');
			if (colon != string::npos)
			{
				item.label = trim(text.substr(0, colon));
				if (!isName(item.label) || isReservedWord(lowerCase(item.label)))
				{
					*error = lineError(number, "bad name '" + item.label + "'");
					return false;
				}
				text = trim(text.substr(colon + 1));
			}

			string rest;
			string word = splitWord(text, &rest);
			string keyword = lowerCase(word);
			string rest2;
			string second = lowerCase(splitWord(rest, &rest2));
			if (text.empty())
				item.kind = NOTHING;
			else if (keyword == "org")
			{
				item.kind = ORG;
				item.text = rest;
			}
			else if (keyword == "autopage")
			{
				string mode = lowerCase(rest);
				if (mode != "on" && mode != "off")
				{
					*error = lineError(number, "autopage needs on or off");
					return false;
				}
				autopage = mode == "on";
				item.kind = AUTOPAGE;
			}
			else if (second == "equ")
			{
				item.kind = EQU;
				item.name = word;
				item.text = rest2;
			}
			else if (keyword == "dw")
			{
				item.kind = DW;
				item.text = rest;
			}
			else
			{
				item.kind = CODE;
				item.text = text;
			}
			items->push_back(item);
		}
		return true;
	}

	// Gives every item its address and every label its value. Items in
	// extraPage get a page instruction in front of them.
	bool layout(vector<Item>* items, const map<size_t, int>& extraPage, int romSize, Symbols* symbols, string* error)
	{
		*symbols = registerNames();
		int address = 0;
		string problem;
		for (size_t i = 0; i < items->size(); i++)
		{
			Item& item = (*items)[i];
			item.address = address;
			if (!item.label.empty() && !define(item.label, address, true, symbols, &problem))
			{
				*error = lineError(item.number, problem);
				return false;
			}
			int value;
			switch (item.kind)
			{
			case ORG:
				if (!parseValue(item.text, *symbols, &value) || value >= romSize)
				{
					*error = lineError(item.number, "bad org address");
					return false;
				}
				address = value;
				break;
			case EQU:
				if (!parseValue(item.text, *symbols, &value) || value > 0xfff)
				{
					*error = lineError(item.number, "bad value '" + item.text + "' for equ");
					return false;
				}
				if (!define(item.name, value, false, symbols, &problem))
				{
					*error = lineError(item.number, problem);
					return false;
				}
				break;
			case DW:
				address += 1 + count(item.text.begin(), item.text.end(), ',');
				break;
			case CODE:
				address += extraPage.count(i) ? 2 : 1;
				break;
			default:
				break;
			}
		}
		return true;
	}

	// The page a jmp or call to a label needs, if it is not the page the
	// jump itself is in. Code at an address is taken to run with the page of
	// that address.
	bool neededPage(const Item& item, const Symbols& symbols, int* page)
	{
		string operand;
		string mnemonic = lowerCase(splitWord(item.text, &operand));
		if (mnemonic != "jmp" && mnemonic != "call")
			return false;
		Symbols::const_iterator target = symbols.find(operand);
		if (target == symbols.end() || !target->second.label)
			return false;
		*page = target->second.value >> 9;
		return *page != item.address >> 9;
	}

	// "page 1" written by the programmer right in front of item i
	bool pageWrittenBefore(const vector<Item>& items, size_t i, int page)
	{
		if (i == 0 || !items[i].label.empty() || items[i - 1].kind != CODE)
			return false;
		return normalize(items[i - 1].text) == "page " + hexText(page);
	}
}

bool assembleSource(const string& source, unsigned short* rom, int romSize, string* error)
{
	for (int i = 0; i < romSize; i++)
		rom[i] = 0xfff;

	vector<Item> items;
	if (!parseSource(source, &items, error))
		return false;

	// A page instruction moves the addresses behind it, which can change the
	// page of a label. Insert them until nothing changes any more.
	Symbols symbols;
	map<size_t, int> extraPage;		// item -> page
	for (bool changed = true; changed;)
	{
		if (!layout(&items, extraPage, romSize, &symbols, error))
			return false;
		changed = false;
		for (size_t i = 0; i < items.size(); i++)
		{
			int page;
			if (items[i].kind == CODE && items[i].autopage && !extraPage.count(i) &&
					neededPage(items[i], symbols, &page) && !pageWrittenBefore(items, i, page))
			{
				extraPage[i] = page;
				changed = true;
			}
		}
	}

	// the words
	string problem;
	for (size_t i = 0; i < items.size(); i++)
	{
		const Item& item = items[i];
		vector<int> words;
		if (item.kind == DW)
		{
			if (!dataValues(item.text, symbols, &words, &problem))
			{
				*error = lineError(item.number, problem);
				return false;
			}
		}
		else if (item.kind == CODE)
		{
			if (extraPage.count(i))
				words.push_back(0x010 | extraPage[i]);
			string resolved;
			unsigned short opcode;
			if (!substituteNames(item.text, symbols, &resolved, &problem))
			{
				*error = lineError(item.number, problem);
				return false;
			}
			if (!assemble(resolved, &opcode))
			{
				*error = lineError(item.number, "cannot assemble '" + item.text + "'");
				return false;
			}
			words.push_back(opcode);
		}
		if (item.address + (int) words.size() > romSize)
		{
			*error = lineError(item.number, "address beyond the end of the ROM");
			return false;
		}
		for (size_t w = 0; w < words.size(); w++)
			rom[item.address + w] = words[w];
	}
	return true;
}
