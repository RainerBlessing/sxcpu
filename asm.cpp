#include "asm.h"
#include "disasm.h"
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
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

	bool isNameChar(char c)
	{
		return isalnum((unsigned char) c) || c == '_';
	}

	struct Value
	{
		long long number;
		bool label;		// a label or $ is part of the value: jmp and call keep only its low bits
	};

	const long long EXPRESSION_LIMIT = 0xffffff;
	const int EXPRESSION_DEPTH = 64;

	// A value of the assembler: numbers (hexadecimal as 1f or 0x1f, decimal d'31', binary
	// b'11111'), names, $ (the address of the line), ( ), the unary operators - + ~ and
	// the binary operators * / + - << >> & ^ | with the precedence of C.
	class Expression
	{
	public:
		Expression(const string& text, const Symbols& symbols, int here) :
			text(text), symbols(symbols), here(here), position(0), depth(0), problem(NULL) {}

		bool evaluate(Value* result, string* problemText)
		{
			problem = problemText;
			Value value = { 0, false };
			if (!binary(0, &value))
				return false;
			skipBlanks();
			if (position < text.size())
				return fail(string("unexpected '") + text[position] + "'");
			*result = value;
			return true;
		}

	private:
		string text;
		const Symbols& symbols;
		int here;
		size_t position;
		int depth;
		string* problem;

		bool fail(const string& message)
		{
			*problem = message;
			return false;
		}

		void skipBlanks()
		{
			while (position < text.size() && (text[position] == ' ' || text[position] == '\t'))
				position++;
		}

		// the operator of a precedence level (0 is the weakest) at the position, or ""
		string operatorAt(int level) const
		{
			if (position >= text.size())
				return "";
			char c = text[position];
			char next = position + 1 < text.size() ? text[position + 1] : 0;
			switch (level)
			{
			case 0:
				return c == '|' ? "|" : "";
			case 1:
				return c == '^' ? "^" : "";
			case 2:
				return c == '&' ? "&" : "";
			case 3:
				return c == '<' && next == '<' ? "<<" : c == '>' && next == '>' ? ">>" : "";
			case 4:
				return c == '+' ? "+" : c == '-' ? "-" : "";
			default:
				return c == '*' ? "*" : c == '/' ? "/" : "";
			}
		}

		bool binary(int level, Value* result)
		{
			if (level > 5)
				return unary(result);
			if (!binary(level + 1, result))
				return false;
			for (;;)
			{
				skipBlanks();
				string op = operatorAt(level);
				if (op.empty())
					return true;
				position += op.size();
				Value right;
				if (!binary(level + 1, &right) || !apply(op, right, result))
					return false;
			}
		}

		bool apply(const string& op, const Value& right, Value* left)
		{
			long long a = left->number;
			long long b = right.number;
			long long r = 0;
			if (op == "|")
				r = a | b;
			else if (op == "^")
				r = a ^ b;
			else if (op == "&")
				r = a & b;
			else if (op == "+")
				r = a + b;
			else if (op == "-")
				r = a - b;
			else if (op == "*")
				r = a * b;
			else if (op == "/" || op == "<<" || op == ">>")
			{
				if (op == "/" && b == 0)
					return fail("division by zero");
				if (op != "/" && (b < 0 || b > 24))
					return fail("the shift count must be from 0 to 24");
				r = op == "/" ? a / b : op == "<<" ? a * (1LL << b) : a >> b;
			}
			if (r > EXPRESSION_LIMIT || r < -EXPRESSION_LIMIT)
				return fail("the value is out of range");
			left->number = r;
			left->label = left->label || right.label;
			return true;
		}

		bool unary(Value* result)
		{
			if (depth >= EXPRESSION_DEPTH)
				return fail("the expression is nested too deeply");
			depth++;
			bool ok = unaryOperator(result);
			depth--;
			return ok;
		}

		bool unaryOperator(Value* result)
		{
			skipBlanks();
			if (position < text.size() && (text[position] == '-' || text[position] == '+' || text[position] == '~'))
			{
				char op = text[position++];
				if (!unary(result))
					return false;
				if (op == '-')
					result->number = -result->number;
				else if (op == '~')
					result->number = ~result->number;
				return true;
			}
			return primary(result);
		}

		bool primary(Value* result)
		{
			skipBlanks();
			if (position >= text.size())
				return fail("a value is missing");
			char c = text[position];
			result->label = false;
			if (c == '(')
			{
				position++;
				if (!binary(0, result))
					return false;
				skipBlanks();
				if (position >= text.size() || text[position] != ')')
					return fail("missing )");
				position++;
				return true;
			}
			if (c == '$')
			{
				position++;
				result->number = here;
				result->label = true;
				return true;
			}
			if (!isNameChar(c))
				return fail(string("unexpected '") + c + "'");
			size_t end = position;
			while (end < text.size() && isNameChar(text[end]))
				end++;
			string word = text.substr(position, end - position);
			position = end;
			char kind = word.size() == 1 ? (char) tolower((unsigned char) word[0]) : 0;
			if ((kind == 'd' || kind == 'b') && position < text.size() && text[position] == '\'')
				return quoted(kind == 'd' ? 10 : 2, result);
			return name(word, result);
		}

		// d'123' and b'101': the position is at the opening quote
		bool quoted(int base, Value* result)
		{
			size_t close = text.find('\'', position + 1);
			if (close == string::npos)
				return fail("missing ' in a number");
			string digits = text.substr(position + 1, close - position - 1);
			position = close + 1;
			if (digits.empty() || digits.size() > (base == 10 ? 7u : 24u))
				return fail("bad number '" + digits + "'");
			long long number = 0;
			for (size_t i = 0; i < digits.size(); i++)
			{
				int digit = digits[i] - '0';
				if (digit < 0 || digit >= base)
					return fail("bad digit '" + string(1, digits[i]) + "' in a " + (base == 10 ? "decimal" : "binary") + " number");
				number = number * base + digit;
			}
			result->number = number;
			result->label = false;
			return true;
		}

		// a symbol, or a name that is a hexadecimal number; a symbol wins
		bool name(const string& word, Value* result)
		{
			if (isReservedWord(lowerCase(word)))
				return fail(word + " cannot be used in an expression");
			Symbols::const_iterator symbol = symbols.find(word);
			if (symbol != symbols.end())
			{
				if (symbol->second.label && symbol->second.value > 0xfff)
					return fail("label " + word + " is beyond the end of the ROM");
				result->number = symbol->second.value;
				result->label = symbol->second.label;
				return true;
			}
			int number;
			if (!parseHex(word, &number))
				return fail(isdigit((unsigned char) word[0]) ? "bad number " + word : "unknown name " + word);
			result->number = number;
			return true;
		}
	};

	// the value of a text that must not be negative, for org, equ, dw ...; `here` is $
	bool evaluateNumber(const string& text, const Symbols& symbols, int here, int* number, string* problem)
	{
		Value value;
		if (!Expression(text, symbols, here).evaluate(&value, problem))
			return false;
		if (value.number < 0)
		{
			*problem = "the value is negative";
			return false;
		}
		*number = (int) value.number;
		return true;
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

	// " (text)", or nothing for an empty text
	string parenthesized(const string& text)
	{
		return text.empty() ? "" : " (" + text + ")";
	}

	// The value of an operand (or of the bit number of "clrb a.3") as a hexadecimal number. A
	// label or $ makes the value the address of code: jmp keeps the low 9 bits, call the low 8.
	bool resolveValue(const string& mnemonic, const string& text, bool immediate, const Symbols& symbols, int here,
		string* out, string* problem)
	{
		Value value;
		if (!Expression(text, symbols, here).evaluate(&value, problem))
			return false;
		long long number = value.number;
		if (value.label && mnemonic == "call" && (number & 0x100))
		{
			*problem = "call target " + text + " is in the second half of a page";
			return false;
		}
		if (value.label)
			number &= mnemonic == "jmp" ? 0x1ff : mnemonic == "call" ? 0xff : 0xfff;
		else if (number < 0 && immediate)
			number &= 0xff;
		if (number < 0)
		{
			*problem = "the value of " + text + " is negative";
			return false;
		}
		*out = hexText((int) number);
		return true;
	}

	// One operand of an instruction. What belongs to the syntax of the instruction stays as it
	// is (the prefixes # / ++ -- << >>, the suffix -w, the !port, the registers w and m); the
	// rest is an expression and becomes a number.
	bool resolveOperand(const string& mnemonic, bool bitInstruction, const string& operand, const Symbols& symbols,
		int here, string* out, string* problem)
	{
		string core = trim(operand);
		if (!core.empty() && core[0] == '!')
		{
			*out = core;
			return true;
		}

		static const char* const prefixes[] = { "++", "--", "<<", ">>", "#", "/" };
		string prefix;
		for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]) && prefix.empty(); i++)
		{
			string candidate = prefixes[i];
			if (core.compare(0, candidate.size(), candidate) == 0)
			{
				prefix = candidate;
				core = trim(core.substr(candidate.size()));
			}
		}
		string suffix;
		size_t last = core.find_last_not_of(" \t");
		if (last != string::npos && last > 0 && tolower((unsigned char) core[last]) == 'w')
		{
			size_t dash = core.find_last_not_of(" \t", last - 1);
			if (dash != string::npos && core[dash] == '-')
			{
				suffix = "-w";
				core = trim(core.substr(0, dash));
			}
		}
		if (core.empty() || isReservedWord(lowerCase(core)))
		{
			*out = prefix + core + suffix;
			return true;
		}

		string first = core;
		string bit;
		size_t dot = core.rfind('.');
		if (bitInstruction && dot != string::npos)
		{
			first = trim(core.substr(0, dot));
			bit = trim(core.substr(dot + 1));
		}
		string value;
		if (!resolveValue(mnemonic, first, prefix == "#", symbols, here, &value, problem))
			return false;
		if (first != core)
		{
			string number;
			if (!resolveValue("", bit, false, symbols, here, &number, problem))
				return false;
			value += "." + number;
		}
		*out = prefix + value + suffix;
		return true;
	}

	// "jmp loop+1" -> "jmp 21", "mov counter, w" -> "mov 10, w": the values in the operands
	// become numbers. `here` is the address of the instruction ($).
	bool substituteNames(const string& text, const Symbols& symbols, int here, string* resolved, string* problem)
	{
		string operands;
		string lower = lowerCase(splitWord(trim(text), &operands));
		bool bits = lower == "clrb" || lower == "setb" || lower == "sb" || lower == "snb";
		string out;
		for (size_t from = 0; !operands.empty();)
		{
			size_t comma = operands.find(',', from);
			string operand;
			if (!resolveOperand(lower, bits, operands.substr(from, comma == string::npos ? string::npos : comma - from),
				symbols, here, &operand, problem))
				return false;
			out += (out.empty() ? "" : ",") + operand;
			if (comma == string::npos)
				break;
			from = comma + 1;
		}
		*resolved = lower + (out.empty() ? "" : " " + out);
		return true;
	}

	// the values of a dw line, one ROM word each; `here` is the address of the first
	bool dataValues(const string& text, const Symbols& symbols, int here, vector<int>* values, string* problem)
	{
		istringstream in(text);
		string item;
		while (getline(in, item, ','))
		{
			int value;
			string detail;
			item = trim(item);
			if (!evaluateNumber(item, symbols, here + (int) values->size(), &value, &detail) || value > 0xfff)
			{
				*problem = "bad value '" + item + "' for dw" + parenthesized(detail);
				return false;
			}
			values->push_back(value);
		}
		string trimmed = trim(text);
		if (!trimmed.empty() && trimmed[trimmed.size() - 1] == ',')
		{
			*problem = "dw ends with a comma";
			return false;
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
		static const char* names[] = { "indf", "rtcc", "pc", "status", "fsr", "ra", "rb", "rc", "rd", "re" };
		static const int numbers[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
		Symbols symbols;
		for (int i = 0; i < 10; i++)
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
	const int PAGE_WORDS = 512;
	const int PAGE_HALF = 256;

	enum Kind { NOTHING, ORG, FIRSTHALF, SAMEPAGE, EQU, DW, CODE, AUTOPAGE };

	struct Item
	{
		int number;		// line of the source
		Kind kind;
		string label;		// defined in front of the item, may be empty
		string name;		// EQU: the name
		string text;		// the rest of the line
		bool autopage;		// CODE: page instructions are inserted
		int address;		// where the item (or its inserted pages) starts
		bool boundaryPage;	// CODE: a page instruction in front, the code runs into a new page
		string origin;		// where a line from a macro comes from, e.g. "macro wait, line 3"; empty otherwise
	};

	string decimalText(int value)
	{
		ostringstream out;
		out << value;
		return out.str();
	}

	// "line N: message (in macro name, line M)": origin tells where in a macro the line is
	string positionError(int number, const string& origin, const string& message)
	{
		return lineError(number, message + parenthesized(origin.empty() ? "" : "in " + origin));
	}

	string itemError(const Item& item, const string& message)
	{
		return positionError(item.number, item.origin, message);
	}

	struct Line
	{
		int number;		// line of the source; for the lines of a macro the line of its call
		string text;		// without comment and without blanks around it
		string origin;		// like Item::origin
	};

	struct Macro
	{
		int number;		// line of the definition
		vector<string> params;
		vector<string> locals;	// names that are different in every expansion
		vector<string> body;
		vector<int> bodyLines;	// the line of the source of every line of the body
	};
	typedef map<string, Macro> Macros;

	const int MAX_MACRO_DEPTH = 16;
	const size_t MAX_EXPANDED_LINES = 100000;

	// a word that starts a line of the assembler and so cannot name a macro
	bool isDirective(const string& word)
	{
		return word == "org" || word == "firsthalf" || word == "samepage" || word == "autopage" || word == "dw" ||
			word == "equ" || word == "macro" || word == "endm" || word == "local";
	}

	bool isMnemonic(const string& word)
	{
		static set<string> mnemonics;
		if (mnemonics.empty())
		{
			for (unsigned op = 0; op < 0x1000; op++)
			{
				string text = disassemble((unsigned short) op);
				if (text == "unused" || text.compare(0, 8, "not impl") == 0)
					continue;
				string rest;
				mnemonics.insert(splitWord(normalize(text), &rest));
			}
		}
		return mnemonics.count(word) != 0;
	}

	// "a, b, c" -> names; each must be a name that is in neither list so far
	bool addNames(const string& text, vector<string>* names, const vector<string>& others, string* problem)
	{
		if (text.empty())
			return true;
		for (size_t from = 0;;)
		{
			size_t comma = text.find(',', from);
			string name = trim(text.substr(from, comma == string::npos ? string::npos : comma - from));
			if (!isName(name) || isReservedWord(lowerCase(name)))
			{
				*problem = "bad name '" + name + "'";
				return false;
			}
			if (find(names->begin(), names->end(), name) != names->end() || find(others.begin(), others.end(), name) != others.end())
			{
				*problem = name + " is used twice";
				return false;
			}
			names->push_back(name);
			if (comma == string::npos)
				return true;
			from = comma + 1;
		}
	}

	// the arguments of a macro call: "a, b" or "(a, b)"
	bool splitArguments(const string& text, vector<string>* args, string* problem)
	{
		string inner = text;
		if (!inner.empty() && inner[0] == '(')
		{
			// the parentheses around all arguments, not around the first one only: "(a+1), b"
			size_t close = string::npos;
			int open = 0;
			for (size_t i = 0; i < inner.size() && close == string::npos; i++)
			{
				if (inner[i] == '(')
					open++;
				else if (inner[i] == ')' && --open == 0)
					close = i;
			}
			if (close == string::npos)
			{
				*problem = "missing ) after the arguments";
				return false;
			}
			if (close == inner.size() - 1)
				inner = trim(inner.substr(1, inner.size() - 2));
		}
		if (inner.empty())
			return true;
		for (size_t from = 0;;)
		{
			size_t comma = inner.find(',', from);
			string arg = trim(inner.substr(from, comma == string::npos ? string::npos : comma - from));
			if (arg.empty())
			{
				*problem = "empty argument";
				return false;
			}
			args->push_back(arg);
			if (comma == string::npos)
				return true;
			from = comma + 1;
		}
	}

	// the names in the text that are keys of values are replaced, in one pass
	string substitute(const string& text, const map<string, string>& values)
	{
		string out;
		for (size_t i = 0; i < text.size();)
		{
			if (!isNameChar(text[i]))
			{
				out += text[i++];
				continue;
			}
			size_t end = i;
			while (end < text.size() && isNameChar(text[end]))
				end++;
			string word = text.substr(i, end - i);
			map<string, string>::const_iterator found = values.find(word);
			out += found == values.end() ? word : found->second;
			i = end;
		}
		return out;
	}

	// Appends the line to `out`; the call of a macro is replaced by its body (which can call
	// macros again). A label in front of a call is kept in front of the first line. origin is
	// where `text` comes from if it is a line of a macro.
	bool expandLine(const string& text, int number, int depth, const Macros& macros, int* counter, const string& origin,
		vector<Line>* out, string* error)
	{
		if (out->size() > MAX_EXPANDED_LINES)
		{
			*error = positionError(number, origin, "the macros expand to too many lines");
			return false;
		}
		string label;
		string rest = text;
		size_t colon = text.find(':');
		if (colon != string::npos)
		{
			label = trim(text.substr(0, colon));
			rest = trim(text.substr(colon + 1));
		}
		size_t end = rest.find_first_of(" \t(");
		string name = rest.substr(0, end);
		Macros::const_iterator found = macros.find(name);
		if (found == macros.end())
		{
			Line line = { number, text, origin };
			out->push_back(line);
			return true;
		}

		const Macro& macro = found->second;
		if (depth >= MAX_MACRO_DEPTH)
		{
			*error = positionError(number, origin, "macro " + name + " is nested too deeply (does it call itself?)");
			return false;
		}
		vector<string> args;
		string problem;
		if (!splitArguments(end == string::npos ? "" : trim(rest.substr(end)), &args, &problem))
		{
			*error = positionError(number, origin, problem + " in the call of macro " + name);
			return false;
		}
		if (args.size() != macro.params.size())
		{
			ostringstream message;
			message << "macro " << name << " takes " << macro.params.size() << " arguments, not " << args.size();
			*error = positionError(number, origin, message.str());
			return false;
		}

		map<string, string> values;
		for (size_t i = 0; i < args.size(); i++)
			values[macro.params[i]] = args[i];
		(*counter)++;
		for (size_t i = 0; i < macro.locals.size(); i++)
		{
			ostringstream unique;
			unique << macro.locals[i] << "__" << *counter;
			values[macro.locals[i]] = unique.str();
		}

		if (!label.empty())
		{
			Line line = { number, label + ":", origin };
			out->push_back(line);
		}
		for (size_t i = 0; i < macro.body.size(); i++)
		{
			string where = "macro " + name + ", line " + decimalText(macro.bodyLines[i]);
			if (!origin.empty())
				where += ", called from " + origin;
			if (!expandLine(substitute(macro.body[i], values), number, depth + 1, macros, counter, where, out, error))
				return false;
		}
		return true;
	}

	// Cuts the source into lines, takes the macro definitions out and expands the calls.
	bool expandSource(const string& source, vector<Line>* lines, string* error)
	{
		istringstream in(source);
		string raw;
		int number = 0;
		Macros macros;
		int counter = 0;
		string defining;	// the name of the macro whose body is being read
		Macro macro = Macro();
		while (getline(in, raw))
		{
			number++;
			string text = trim(raw.substr(0, raw.find(';')));
			string rest;
			string keyword = lowerCase(splitWord(text, &rest));
			string rest2;
			string second = lowerCase(splitWord(rest, &rest2));
			string problem;

			if (!defining.empty())
			{
				if (keyword == "endm")
				{
					if (!rest.empty())
					{
						*error = lineError(number, "endm takes no operands");
						return false;
					}
					macros[defining] = macro;
					defining.clear();
				}
				else if (keyword == "macro" || second == "macro")
				{
					*error = lineError(number, "a macro cannot be defined inside of a macro");
					return false;
				}
				else if (keyword == "local")
				{
					if (rest.empty() || !addNames(rest, &macro.locals, macro.params, &problem))
					{
						*error = lineError(number, rest.empty() ? "local needs a name" : problem);
						return false;
					}
				}
				else if (!text.empty())
				{
					macro.body.push_back(text);
					macro.bodyLines.push_back(number);
				}
			}
			else if (second == "macro")
			{
				string name = text.substr(0, text.find_first_of(" \t"));
				if (!isName(name) || isReservedWord(lowerCase(name)) || isMnemonic(lowerCase(name)) || isDirective(lowerCase(name)))
				{
					*error = lineError(number, "bad macro name '" + name + "'");
					return false;
				}
				if (macros.count(name))
				{
					*error = lineError(number, "macro " + name + " is defined twice");
					return false;
				}
				macro = Macro();
				macro.number = number;
				if (!addNames(rest2, &macro.params, vector<string>(), &problem))
				{
					*error = lineError(number, problem + " in the parameters of macro " + name);
					return false;
				}
				defining = name;
			}
			else if (keyword == "macro" || keyword == "endm" || keyword == "local")
			{
				*error = lineError(number, keyword == "macro" ? "macro needs a name in front of it" : keyword + " outside of a macro");
				return false;
			}
			else if (!text.empty() && !expandLine(text, number, 0, macros, &counter, "", lines, error))
				return false;
		}
		if (!defining.empty())
		{
			*error = lineError(macro.number, "macro " + defining + " has no endm");
			return false;
		}
		return true;
	}

	// Cuts the source into items.
	bool parseSource(const vector<Line>& lines, vector<Item>* items, string* error)
	{
		bool autopage = false;
		for (size_t index = 0; index < lines.size(); index++)
		{
			int number = lines[index].number;
			string text = lines[index].text;
			Item item = { number, NOTHING, "", "", "", autopage, 0, false, lines[index].origin };

			size_t colon = text.find(':');
			if (colon != string::npos)
			{
				item.label = trim(text.substr(0, colon));
				if (!isName(item.label) || isReservedWord(lowerCase(item.label)))
				{
					*error = positionError(number, item.origin, "bad name '" + item.label + "'");
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
			else if (keyword == "firsthalf")
				item.kind = FIRSTHALF;
			else if (keyword == "samepage")
			{
				item.kind = SAMEPAGE;
				item.text = rest;
			}
			else if (keyword == "autopage")
			{
				string mode = lowerCase(rest);
				if (mode != "on" && mode != "off")
				{
					*error = positionError(number, item.origin, "autopage needs on or off");
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
		bool sequential = false;	// reached from the item before, not by org or firsthalf
		string problem;
		for (size_t i = 0; i < items->size(); i++)
		{
			Item& item = (*items)[i];
			// code that runs on into the next page has to set the page there
			item.boundaryPage = item.kind == CODE && item.autopage && sequential && address % PAGE_WORDS == 0;
			item.address = address;
			// org, firsthalf and samepage move the address: their label stands where the code goes on
			bool movesAddress = item.kind == ORG || item.kind == FIRSTHALF || item.kind == SAMEPAGE;
			if (!movesAddress && !item.label.empty() && !define(item.label, address, true, symbols, &problem))
			{
				*error = itemError(item, problem);
				return false;
			}
			int value;
			switch (item.kind)
			{
			case ORG:
				problem.clear();
				if (!evaluateNumber(item.text, *symbols, address, &value, &problem) || value >= romSize)
				{
					*error = itemError(item, "bad org address" + parenthesized(problem));
					return false;
				}
				address = value;
				sequential = false;
				break;
			case FIRSTHALF:
				if (address & PAGE_HALF)
				{
					address = (address | (PAGE_WORDS - 1)) + 1;
					if (address >= romSize)
					{
						*error = itemError(item, "firsthalf moves beyond the end of the ROM");
						return false;
					}
					sequential = false;
				}
				break;
			case SAMEPAGE:
			{
				int size;
				problem.clear();
				if (!evaluateNumber(item.text, *symbols, address, &size, &problem) || size < 1 || size > PAGE_WORDS)
				{
					*error = itemError(item, "samepage needs a number of words from 1 to 200" + parenthesized(problem));
					return false;
				}
				if ((address % PAGE_WORDS) + size > PAGE_WORDS)
				{
					address = (address | (PAGE_WORDS - 1)) + 1;
					if (address >= romSize)
					{
						*error = itemError(item, "samepage moves beyond the end of the ROM");
						return false;
					}
					sequential = false;
				}
				break;
			}
			case EQU:
				problem.clear();
				if (!evaluateNumber(item.text, *symbols, address, &value, &problem) || value > 0xfff)
				{
					*error = itemError(item, "bad value '" + item.text + "' for equ" + parenthesized(problem));
					return false;
				}
				if (!define(item.name, value, false, symbols, &problem))
				{
					*error = itemError(item, problem);
					return false;
				}
				break;
			case DW:
				address += 1 + count(item.text.begin(), item.text.end(), ',');
				sequential = true;
				break;
			case CODE:
				address += (item.boundaryPage ? 1 : 0) + (extraPage.count(i) ? 1 : 0) + 1;
				sequential = true;
				break;
			default:
				break;
			}
			if (movesAddress)
			{
				item.address = address;
				if (!item.label.empty() && !define(item.label, address, true, symbols, &problem))
				{
					*error = itemError(item, problem);
					return false;
				}
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
		Value target;
		string problem;
		if (!Expression(operand, symbols, item.address).evaluate(&target, &problem) || !target.label || target.number < 0)
			return false;
		*page = (int) (target.number >> 9);
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

bool assembleSource(const string& source, unsigned short* rom, int romSize, string* error, map<string, int>* labels)
{
	for (int i = 0; i < romSize; i++)
		rom[i] = 0xfff;

	vector<Line> lines;
	if (!expandSource(source, &lines, error))
		return false;
	vector<Item> items;
	if (!parseSource(lines, &items, error))
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
			if (items[i].kind != CODE || !items[i].autopage)
				continue;
			if (extraPage.count(i))
			{
				// the label may have moved to another page since: the page instruction
				// stays (it cannot be taken back), but sets the right page
				int right = neededPage(items[i], symbols, &page) ? page : items[i].address >> 9;
				if (extraPage[i] != right)
				{
					extraPage[i] = right;
					changed = true;
				}
			}
			else if (neededPage(items[i], symbols, &page) && !pageWrittenBefore(items, i, page))
			{
				extraPage[i] = page;
				changed = true;
			}
		}
	}

	// the words
	string problem;
	vector<bool> written(romSize, false);
	for (size_t i = 0; i < items.size(); i++)
	{
		const Item& item = items[i];
		vector<int> words;
		if (item.kind == DW)
		{
			if (!dataValues(item.text, symbols, item.address, &words, &problem))
			{
				*error = itemError(item, problem);
				return false;
			}
		}
		else if (item.kind == CODE)
		{
			if (item.boundaryPage)
				words.push_back(0x010 | (item.address / PAGE_WORDS));
			if (extraPage.count(i))
				words.push_back(0x010 | extraPage[i]);
			string resolved;
			unsigned short opcode;
			if (!substituteNames(item.text, symbols, item.address + (int) words.size(), &resolved, &problem))
			{
				*error = itemError(item, problem);
				return false;
			}
			if (!assemble(resolved, &opcode))
			{
				*error = itemError(item, "cannot assemble '" + item.text + "'");
				return false;
			}
			words.push_back(opcode);
		}
		if (item.address + (int) words.size() > romSize)
		{
			*error = itemError(item, "address beyond the end of the ROM");
			return false;
		}
		for (size_t w = 0; w < words.size(); w++)
		{
			int target = item.address + (int) w;
			if (written[target])
			{
				*error = itemError(item, "address " + hexText(target) + " is used twice");
				return false;
			}
			written[target] = true;
			rom[target] = words[w];
		}
	}
	if (labels != NULL)
	{
		for (Symbols::const_iterator symbol = symbols.begin(); symbol != symbols.end(); ++symbol)
		{
			if (symbol->second.label)
				(*labels)[symbol->first] = symbol->second.value;
		}
	}
	return true;
}
