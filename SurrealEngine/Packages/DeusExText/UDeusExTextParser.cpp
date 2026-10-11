
#include "Precomp.h"
#include "UDeusExTextParser.h"
#include "Packages/Extension/UExtString.h"
#include "Package/Package.h"
#include "Package/PackageManager.h"
#include "Engine.h"
#include "Utils/Logger.h"

/*
	Parser for the pseudo HTML text of datacubes, books, bulletins and the email and file lists of computers:

	<DC=255,255,255>
	<P>We'll make this one easy for you.  To open the door, use the code:
	<P>
	<P><JC><B>0012</B>
	<P>
	<P>Got it?
	<P>
	<P>Jaime

	The rules are the rules of DDeusExTextParser in DeusExText.dll.
*/

namespace
{
	// Tag names in the order of EDeusExTextTags. The first name that is a prefix of the tag text gives the tag.
	const std::string TagNames[] =
	{
		"TEXT", "FILE", "EMAIL", "NOTE", "/NOTE", "GOAL", "/GOAL", "COMMENT", "/COMMENT",
		"PLAYERNAME", "PLAYERFIRSTNAME", "NP", "JC", "JL", "JR", "DC", "C", "/C", "P",
		"B", "/B", "U", "/U", "I", "/I", "G", "F", "L", "/<", "/>"
	};
}

bool UDXTextParser::OpenText(NameString textName, std::string textPackage)
{
	CloseText();
	bParagraphStarted() = false;

	if (textName.IsNone())
		return false;
	if (textPackage.empty())
		textPackage = "DeusExText";

	UDXExtString* textObject = UObject::Cast<UDXExtString>(engine->packages->GetPackage(textPackage)->GetUObject("ExtString", textName));
	if (!textObject)
		return false;

	// The parser works on its own copy of the text, which ends at the first null character
	text = textObject->Text();
	size_t end = text.find('\0');
	if (end != std::string::npos)
		text.resize(end);

	Text() = 1;
	TextPos() = 0;
	return true;
}

void UDXTextParser::CloseText()
{
	text.clear();
	textEnded = false;
	Text() = 0;
	TextPos() = 0;
	TagEndPos() = 0;
}

bool UDXTextParser::ProcessText()
{
	LastText() = ParseTextBlock();
	return !IsEOF();
}

bool UDXTextParser::IsEOF()
{
	return Text() == 0 || textEnded;
}

// Gets the next tag or the next run of plain text
std::string UDXTextParser::ParseTextBlock()
{
	std::string result;
	bool textFound = false;

	LastTag() = DeusExTextTags::TT_None;
	if (IsEOF())
		return result;

	if ((size_t)TextPos() >= text.size())
	{
		textEnded = true;
		return result;
	}

	while ((size_t)TextPos() < text.size())
	{
		char c = text[(size_t)TextPos()];
		if (c == '<')
		{
			if (textFound)
				break;

			result += ParseTag();

			// The first paragraph tag of a text only starts the text. All other tags go to the caller.
			if (LastTag() != DeusExTextTags::TT_NewParagraph || bParagraphStarted())
				break;
			bParagraphStarted() = true;
		}
		else
		{
			// Characters before the first paragraph tag are not part of the text
			if (bParagraphStarted())
			{
				if (c == '\r' || c == '\n')
					c = ' ';
				result += c;
				LastTag() = DeusExTextTags::TT_Text;
				textFound = true;
			}
			TextPos()++;
		}
	}

	return result;
}

// Reads the tag at the text position and returns the text that the tag stands for
std::string UDXTextParser::ParseTag()
{
	std::string result;
	if (IsEOF())
		return result;

	size_t start = (size_t)TextPos() + 1;
	size_t end = text.find('>', (size_t)TextPos());
	if (end == std::string::npos)
		end = text.size();

	TagEndPos() = (int)end;
	TextPos() = (int)std::min(end + 1, text.size());

	std::string tag = start < end ? text.substr(start, end - start) : std::string();
	for (size_t i = 0; i < std::size(TagNames); i++)
	{
		if (tag.compare(0, TagNames[i].size(), TagNames[i]) == 0)
		{
			LastTag() = static_cast<DeusExTextTags>(i);
			result = ParseToken(LastTag(), tag.substr(TagNames[i].size()));
			break;
		}
	}

	TagEndPos() = 0;
	return result;
}

std::string UDXTextParser::ParseToken(DeusExTextTags tag, const std::string& args)
{
	switch (tag)
	{
	case DeusExTextTags::TT_File:
		ParseFile(args);
		break;
	case DeusExTextTags::TT_Email:
		ParseEmail(args);
		break;
	case DeusExTextTags::TT_Note:
		FindEndTag(DeusExTextTags::TT_EndNote);
		break;
	case DeusExTextTags::TT_Goal:
		ParseName(args);
		FindEndTag(DeusExTextTags::TT_EndGoal);
		break;
	case DeusExTextTags::TT_Comment:
		FindEndTag(DeusExTextTags::TT_EndComment);
		break;
	case DeusExTextTags::TT_PlayerName:
		return PlayerName();
	case DeusExTextTags::TT_PlayerFirstName:
		return PlayerFirstName();
	case DeusExTextTags::TT_DefaultColor:
	case DeusExTextTags::TT_TextColor:
	case DeusExTextTags::TT_RevertColor:
		ParseColor(args);
		break;
	case DeusExTextTags::TT_Graphic:
	case DeusExTextTags::TT_Font:
		ParseName(args);
		break;
	case DeusExTextTags::TT_OpenBracket:
		return "<";
	case DeusExTextTags::TT_CloseBracket:
		return ">";
	default:
		break;
	}
	return {};
}

// Gets the next part of the tag arguments. A comma or the end of the tag ends a part.
std::string UDXTextParser::ParseSubToken(const std::string& args, size_t& pos)
{
	if (pos >= args.size())
		return {};

	size_t start = pos;
	size_t end = args.find(',', start);
	if (end == std::string::npos)
	{
		pos = args.size() + 1;
		return args.substr(start);
	}

	pos = end + 1;
	return args.substr(start, end - start);
}

std::string UDXTextParser::TrimSpaces(const std::string& value)
{
	size_t start = 0;
	size_t end = value.size();
	while (start < end && value[start] == ' ')
		start++;
	while (end > start && value[end - 1] == ' ')
		end--;
	return value.substr(start, end - start);
}

void UDXTextParser::ParseFile(const std::string& args)
{
	size_t pos = args.find('=');
	if (pos == std::string::npos)
		return;
	pos++;

	LastFileName() = TrimSpaces(ParseSubToken(args, pos));
	LastFileDescription() = TrimSpaces(ParseSubToken(args, pos));
}

void UDXTextParser::ParseEmail(const std::string& args)
{
	size_t pos = args.find('=');
	if (pos == std::string::npos)
		return;
	pos++;

	LastEmailName() = TrimSpaces(ParseSubToken(args, pos));
	LastEmailSubject() = TrimSpaces(ParseSubToken(args, pos));
	LastEmailFrom() = TrimSpaces(ParseSubToken(args, pos));
	LastEmailTo() = TrimSpaces(ParseSubToken(args, pos));
	LastEmailCC() = TrimSpaces(ParseSubToken(args, pos));
}

void UDXTextParser::ParseName(const std::string& args)
{
	size_t pos = args.find('=');
	if (pos != std::string::npos)
		LastName() = NameString(args.substr(pos + 1));
	else
		LastName() = NameString();
}

void UDXTextParser::ParseColor(const std::string& args)
{
	Color color;
	color.R = 0;
	color.G = 0;
	color.B = 0;
	color.A = 255;

	size_t pos = args.find('=');
	if (pos != std::string::npos)
	{
		pos++;
		color.R = (uint8_t)std::atoi(ParseSubToken(args, pos).c_str());
		color.G = (uint8_t)std::atoi(ParseSubToken(args, pos).c_str());
		color.B = (uint8_t)std::atoi(ParseSubToken(args, pos).c_str());
	}

	LastColor() = color;
}

// Reads the text up to an end tag. The tags in this text also go through the parser.
std::string UDXTextParser::FindEndTag(DeusExTextTags endTag)
{
	std::string result;

	while ((size_t)TextPos() < text.size())
	{
		char c = text[(size_t)TextPos()];
		if (c == '<')
		{
			result += ParseTag();
			if (LastTag() == endTag)
				break;
		}
		else
		{
			if (c == '\r' || c == '\n')
				c = ' ';
			result += c;
		}

		// The original also moves one character forward after a tag that is not the end tag
		TextPos()++;
	}

	if ((size_t)TextPos() > text.size())
		TextPos() = (int)text.size();

	return result;
}

std::string UDXTextParser::GetText()
{
	return LastText();
}

bool UDXTextParser::GotoLabel(const std::string& label)
{
	// The original function does nothing
	return false;
}

uint8_t UDXTextParser::GetTag()
{
	return static_cast<uint8_t>(LastTag());
}

NameString UDXTextParser::GetName()
{
	DeusExTextTags tag = LastTag();
	if (tag == DeusExTextTags::TT_Note || (tag >= DeusExTextTags::TT_Graphic && tag <= DeusExTextTags::TT_Label))
		return LastName();
	return {};
}

Color UDXTextParser::GetColor()
{
	switch (LastTag())
	{
	case DeusExTextTags::TT_DefaultColor:
	case DeusExTextTags::TT_TextColor:
		return LastColor();
	case DeusExTextTags::TT_RevertColor:
		return DefaultColor();
	default:
		break;
	}

	Color color;
	color.R = 0;
	color.G = 0;
	color.B = 0;
	color.A = 255;
	return color;
}

void UDXTextParser::GetEmailInfo(std::string& name, std::string& subject, std::string& from, std::string& to, std::string& cc)
{
	name = LastEmailName();
	subject = LastEmailSubject();
	from = LastEmailFrom();
	to = LastEmailTo();
	cc = LastEmailCC();
}

void UDXTextParser::GetFileInfo(std::string& fileName, std::string& fileDescription)
{
	fileName = LastFileName();
	fileDescription = LastFileDescription();
}

void UDXTextParser::SetPlayerName(const std::string& newPlayerName)
{
	PlayerName() = newPlayerName;

	// The first name is the part before the first space
	size_t pos = newPlayerName.find(' ');
	if (pos != std::string::npos)
		PlayerFirstName() = newPlayerName.substr(0, pos);
	else
		PlayerFirstName() = newPlayerName;
}
