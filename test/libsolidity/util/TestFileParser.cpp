/*
	This file is part of solidity.

	solidity is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	solidity is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <test/libsolidity/util/TestFileParser.h>
#include <test/Options.h>
#include <liblangutil/Common.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/optional.hpp>
#include <boost/throw_exception.hpp>
#include <fstream>
#include <memory>
#include <stdexcept>

using namespace dev;
using namespace langutil;
using namespace solidity;
using namespace dev::solidity::test;
using namespace std;
using namespace soltest;

vector<dev::solidity::test::FunctionCall> TestFileParser::parseFunctionCalls()
{
	vector<FunctionCall> calls;
	if (!accept(Token::EOS))
	{
		assert(m_scanner.currentToken() == Token::Unknown);
		m_scanner.scanNextToken();

		while (!accept(Token::EOS))
		{
			if (!accept(Token::Whitespace))
			{
				FunctionCall call;

				/// If this is not the first call in the test,
				/// the last call to parseParameter could have eaten the
				/// new line already. This could only be fixed with a one
				/// token lookahead that checks parseParameter
				/// if the next token is an identifier.
				if (calls.empty())
					expect(Token::Newline);
				else
					accept(Token::Newline, true);

				call.signature = parseFunctionSignature();
				if (accept(Token::Comma, true))
					call.value = parseFunctionCallValue();
				if (accept(Token::Colon, true))
					call.arguments = parseFunctionCallArguments();

				if (accept(Token::Newline, true))
					call.displayMode = FunctionCall::DisplayMode::MultiLine;

				call.arguments.comment = parseComment();

				if (accept(Token::Newline, true))
					call.displayMode = FunctionCall::DisplayMode::MultiLine;

				expect(Token::Arrow);
				call.expectations = parseFunctionCallExpectations();

				accept(Token::Newline, true);
				call.expectations.comment = parseComment();

				calls.emplace_back(std::move(call));
			}
		}
	}
	return calls;
}

bool TestFileParser::accept(soltest::Token _token, bool const _expect)
{
	if (m_scanner.currentToken() != _token)
		return false;
	if (_expect)
		return expect(_token);
	return true;
}

bool TestFileParser::expect(soltest::Token _token, bool const _advance)
{
	if (m_scanner.currentToken() != _token || m_scanner.currentToken() == Token::Invalid)
		throw Error(
			Error::Type::ParserError,
			"Unexpected " + formatToken(m_scanner.currentToken()) + ": \"" +
			m_scanner.currentLiteral() + "\". " +
			"Expected \"" + formatToken(_token) + "\"."
			);
	if (_advance)
		m_scanner.scanNextToken();
	return true;
}

string TestFileParser::parseFunctionSignature()
{
	string signature = m_scanner.currentLiteral();
	expect(Token::Identifier);

	signature += formatToken(Token::LParen);
	expect(Token::LParen);

	string parameters;
	if (!accept(Token::RParen, false))
		parameters = parseIdentifierOrTuple();

	while (accept(Token::Comma))
	{
		parameters += formatToken(Token::Comma);
		expect(Token::Comma);
		parameters += parseIdentifierOrTuple();
	}
	if (accept(Token::Arrow, true))
		throw Error(Error::Type::ParserError, "Invalid signature detected: " + signature);

	signature += parameters;

	expect(Token::RParen);
	signature += formatToken(Token::RParen);
	return signature;
}

u256 TestFileParser::parseFunctionCallValue()
{
	u256 value = convertNumber(parseDecimalNumber());
	expect(Token::Ether);
	return value;
}

FunctionCallArgs TestFileParser::parseFunctionCallArguments()
{
	FunctionCallArgs arguments;

	auto param = parseParameter();
	if (param.abiType.type == ABIType::None)
		throw Error(Error::Type::ParserError, "No argument provided.");
	arguments.parameters.emplace_back(param);

	while (accept(Token::Comma, true))
		arguments.parameters.emplace_back(parseParameter());
	return arguments;
}

FunctionCallExpectations TestFileParser::parseFunctionCallExpectations()
{
	FunctionCallExpectations expectations;

	auto param = parseParameter();
	if (param.abiType.type == ABIType::None)
	{
		expectations.failure = false;
		return expectations;
	}
	expectations.result.emplace_back(param);

	while (accept(Token::Comma, true))
		expectations.result.emplace_back(parseParameter());

	/// We have always one virtual parameter in the parameter list.
	/// If its type is FAILURE, the expected result is also a REVERT etc.
	if (expectations.result.at(0).abiType.type != ABIType::Failure)
		expectations.failure = false;
	return expectations;
}

Parameter TestFileParser::parseParameter()
{
	Parameter parameter;
	if (accept(Token::Newline, true))
		parameter.format.newline = true;
	auto literal = parseABITypeLiteral();
	parameter.rawBytes = get<0>(literal);
	parameter.abiType = get<1>(literal);
	parameter.rawString = get<2>(literal);
	return parameter;
}

tuple<bytes, ABIType, string> TestFileParser::parseABITypeLiteral()
{
	try
	{
		u256 number{0};
		ABIType abiType{ABIType::None, ABIType::AlignRight, 0};
		string rawString;

		if (accept(Token::Sub))
		{
			abiType = ABIType{ABIType::SignedDec, ABIType::AlignRight, 32};
			expect(Token::Sub);
			rawString += formatToken(Token::Sub);
			string parsed = parseDecimalNumber();
			rawString += parsed;
			number = convertNumber(parsed) * -1;
		}
		else
		{
			if (accept(Token::Boolean))
			{
				abiType = ABIType{ABIType::Boolean, ABIType::AlignRight, 32};
				string parsed = parseBoolean();
				rawString += parsed;
				return make_tuple(toBigEndian(u256{convertBoolean(parsed)}), abiType, rawString);
			}
			else if (accept(Token::HexNumber))
			{
				abiType = ABIType{ABIType::Hex, ABIType::AlignLeft, 32};
				string parsed = parseHexNumber();
				rawString += parsed;
				return make_tuple(convertHexNumber(parsed), abiType, rawString);
			}
			else if (accept(Token::Number))
			{
				abiType = ABIType{ABIType::UnsignedDec, ABIType::AlignRight, 32};
				string parsed = parseDecimalNumber();
				rawString += parsed;
				number = convertNumber(parsed);
			}
			else if (accept(Token::Failure, true))
			{
				abiType = ABIType{ABIType::Failure, ABIType::AlignRight, 0};
				return make_tuple(bytes{}, abiType, rawString);
			}
		}
		return make_tuple(toBigEndian(number), abiType, rawString);
	}
	catch (std::exception const&)
	{
		throw Error(Error::Type::ParserError, "Number encoding invalid.");
	}
}

string TestFileParser::parseIdentifierOrTuple()
{
	string identOrTuple;

	if (accept(Token::Identifier))
	{
		identOrTuple = m_scanner.currentLiteral();
		expect(Token::Identifier);
		return identOrTuple;
	}
	expect(Token::LParen);
	identOrTuple += formatToken(Token::LParen);
	identOrTuple += parseIdentifierOrTuple();

	while (accept(Token::Comma))
	{
		identOrTuple += formatToken(Token::Comma);
		expect(Token::Comma);
		identOrTuple += parseIdentifierOrTuple();
	}
	expect(Token::RParen);
	identOrTuple += formatToken(Token::RParen);
	return identOrTuple;
}

string TestFileParser::parseBoolean()
{
	string literal = m_scanner.currentLiteral();
	expect(Token::Boolean);
	return literal;
}

string TestFileParser::parseComment()
{
	string comment = m_scanner.currentLiteral();
	if (accept(Token::Comment, true))
		return comment;
	return string{};
}

string TestFileParser::parseDecimalNumber()
{
	string literal = m_scanner.currentLiteral();
	expect(Token::Number);
	return literal;
}

string TestFileParser::parseHexNumber()
{
	string literal = m_scanner.currentLiteral();
	expect(Token::HexNumber);
	return literal;
}

bool TestFileParser::convertBoolean(string const& _literal)
{
	if (_literal == "true")
		return true;
	else if (_literal == "false")
		return false;
	else
		throw Error(Error::Type::ParserError, "Boolean literal invalid.");
}

u256 TestFileParser::convertNumber(string const& _literal)
{
	try
	{
		return u256{_literal};
	}
	catch (std::exception const&)
	{
		throw Error(Error::Type::ParserError, "Number encoding invalid.");
	}
}

bytes TestFileParser::convertHexNumber(string const& _literal)
{
	try
	{
		if (_literal.size() % 2)
		{
			throw Error(Error::Type::ParserError, "Hex number encoding invalid.");
		}
		else
		{
			bytes result = fromHex(_literal);
			return result + bytes(32 - result.size(), 0);
		}
	}
	catch (std::exception const&)
	{
		throw Error(Error::Type::ParserError, "Hex number encoding invalid.");
	}
}

void TestFileParser::Scanner::readStream(istream& _stream)
{
	std::string line;
	while (std::getline(_stream, line))
		m_line += line;
	m_char = m_line.begin();
}

void TestFileParser::Scanner::scanNextToken()
{
	// Make code coverage happy.
	assert(formatToken(Token::NUM_TOKENS) == "");

	auto detectKeyword = [](std::string const& _literal = "") -> TokenDesc {
		if (_literal == "true") return TokenDesc{Token::Boolean, _literal};
		if (_literal == "false") return TokenDesc{Token::Boolean, _literal};
		if (_literal == "ether") return TokenDesc{Token::Ether, _literal};
		if (_literal == "FAILURE") return TokenDesc{Token::Failure, _literal};
		return TokenDesc{Token::Identifier, _literal};
	};

	auto selectToken = [this](Token _token, std::string const& _literal = "") -> TokenDesc {
		advance();
		return make_pair(_token, !_literal.empty() ? _literal : formatToken(_token));
	};

	TokenDesc token = make_pair(Token::Unknown, "");
	do
	{
		switch(current())
		{
		case '/':
			advance();
			if (current() == '/')
				token = selectToken(Token::Newline);
			else
				token = selectToken(Token::Invalid);
			break;
		case '-':
			if (peek() == '>')
			{
				advance();
				token = selectToken(Token::Arrow);
			}
			else
				token = selectToken(Token::Sub);
			break;
		case ':':
			token = selectToken(Token::Colon);
			break;
		case '#':
			token = selectToken(Token::Comment, scanComment());
			break;
		case ',':
			token = selectToken(Token::Comma);
			break;
		case '(':
			token = selectToken(Token::LParen);
			break;
		case ')':
			token = selectToken(Token::RParen);
			break;
		default:
			if (langutil::isIdentifierStart(current()))
			{
				TokenDesc detectedToken = detectKeyword(scanIdentifierOrKeyword());
				token = selectToken(detectedToken.first, detectedToken.second);
			}
			else if (langutil::isDecimalDigit(current()))
			{
				if (current() == '0' && peek() == 'x')
				{
					advance();
					advance();
					token = selectToken(Token::HexNumber, "0x" + scanHexNumber());
				}
				else
					token = selectToken(Token::Number, scanDecimalNumber());
			}
			else if (langutil::isWhiteSpace(current()))
				token = selectToken(Token::Whitespace);
			else if (isEndOfLine())
				token = selectToken(Token::EOS);
			break;
		}
	}
	while (token.first == Token::Whitespace);
	m_currentToken = token;
}

string TestFileParser::Scanner::scanComment()
{
	string comment;
	advance();

	while (current() != '#')
	{
		comment += current();
		advance();
	}
	return comment;
}

string TestFileParser::Scanner::scanIdentifierOrKeyword()
{
	string identifier;
	identifier += current();
	while (langutil::isIdentifierPart(peek()))
	{
		advance();
		identifier += current();
	}
	return identifier;
}

string TestFileParser::Scanner::scanDecimalNumber()
{
	string number;
	number += current();
	while (langutil::isDecimalDigit(peek()))
	{
		advance();
		number += current();
	}
	return number;
}

string TestFileParser::Scanner::scanHexNumber()
{
	string number;
	number += current();
	while (langutil::isHexDigit(peek()))
	{
		advance();
		number += current();
	}
	return number;
}
