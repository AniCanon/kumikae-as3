// Token kinds, their display names, and the reserved-word table.
#pragma once

#include <cstdint>
#include <string_view>

namespace kas3 {

// X(kind, display name). Order matters: reserved words are contiguous, between KwAs and KwWith.
#define KAS3_TOKENS(X) \
	X(EndOfFile, "end of file") \
	X(Error, "error") \
	X(Identifier, "identifier") \
	X(Number, "number") \
	X(String, "string") \
	X(Regex, "regular expression") \
	X(KwAs, "'as'") \
	X(KwBreak, "'break'") \
	X(KwCase, "'case'") \
	X(KwCatch, "'catch'") \
	X(KwClass, "'class'") \
	X(KwConst, "'const'") \
	X(KwContinue, "'continue'") \
	X(KwDefault, "'default'") \
	X(KwDelete, "'delete'") \
	X(KwDo, "'do'") \
	X(KwElse, "'else'") \
	X(KwExtends, "'extends'") \
	X(KwFalse, "'false'") \
	X(KwFinally, "'finally'") \
	X(KwFor, "'for'") \
	X(KwFunction, "'function'") \
	X(KwIf, "'if'") \
	X(KwImplements, "'implements'") \
	X(KwImport, "'import'") \
	X(KwIn, "'in'") \
	X(KwInstanceof, "'instanceof'") \
	X(KwInterface, "'interface'") \
	X(KwInternal, "'internal'") \
	X(KwIs, "'is'") \
	X(KwNew, "'new'") \
	X(KwNull, "'null'") \
	X(KwPackage, "'package'") \
	X(KwPrivate, "'private'") \
	X(KwProtected, "'protected'") \
	X(KwPublic, "'public'") \
	X(KwReturn, "'return'") \
	X(KwSuper, "'super'") \
	X(KwSwitch, "'switch'") \
	X(KwThis, "'this'") \
	X(KwThrow, "'throw'") \
	X(KwTrue, "'true'") \
	X(KwTry, "'try'") \
	X(KwTypeof, "'typeof'") \
	X(KwUse, "'use'") \
	X(KwVar, "'var'") \
	X(KwVoid, "'void'") \
	X(KwWhile, "'while'") \
	X(KwWith, "'with'") \
	X(LParen, "'('") \
	X(RParen, "')'") \
	X(LBrace, "'{'") \
	X(RBrace, "'}'") \
	X(LBracket, "'['") \
	X(RBracket, "']'") \
	X(Semicolon, "';'") \
	X(Comma, "','") \
	X(Dot, "'.'") \
	X(DotLess, "'.<'") \
	X(Ellipsis, "'...'") \
	X(DotDot, "'..'") \
	X(Colon, "':'") \
	X(ColonColon, "'::'") \
	X(Question, "'?'") \
	X(At, "'@'") \
	X(Plus, "'+'") \
	X(Minus, "'-'") \
	X(Star, "'*'") \
	X(Slash, "'/'") \
	X(Percent, "'%'") \
	X(PlusPlus, "'++'") \
	X(MinusMinus, "'--'") \
	X(Assign, "'='") \
	X(PlusAssign, "'+='") \
	X(MinusAssign, "'-='") \
	X(StarAssign, "'*='") \
	X(SlashAssign, "'/='") \
	X(PercentAssign, "'%='") \
	X(Equal, "'=='") \
	X(NotEqual, "'!='") \
	X(StrictEqual, "'==='") \
	X(StrictNotEqual, "'!=='") \
	X(Less, "'<'") \
	X(Greater, "'>'") \
	X(LessEqual, "'<='") \
	X(GreaterEqual, "'>='") \
	X(ShiftLeft, "'<<'") \
	X(ShiftRight, "'>>'") \
	X(ShiftRightUnsigned, "'>>>'") \
	X(ShiftLeftAssign, "'<<='") \
	X(ShiftRightAssign, "'>>='") \
	X(ShiftRightUnsignedAssign, "'>>>='") \
	X(Amp, "'&'") \
	X(Pipe, "'|'") \
	X(Caret, "'^'") \
	X(Tilde, "'~'") \
	X(Bang, "'!'") \
	X(AmpAmp, "'&&'") \
	X(PipePipe, "'||'") \
	X(AmpAssign, "'&='") \
	X(PipeAssign, "'|='") \
	X(CaretAssign, "'^='") \
	X(AmpAmpAssign, "'&&='") \
	X(PipePipeAssign, "'||='")

enum class TokenKind : uint8_t {
#define X(kind, name) kind,
	KAS3_TOKENS(X)
#undef X
};

struct Token {
	TokenKind kind;
	uint32_t offset; // Byte offset of the first character.
	uint32_t length; // In bytes.
	bool newline_before; // A line break lies between the previous token and this one.
};

// "identifier", "'('", "'function'", and so on.
const char *token_kind_name(TokenKind p_kind);
// The operator or keyword as written in source ("+", "function"); empty for identifiers and literals.
std::string_view token_kind_spelling(TokenKind p_kind);
// Words that can never be plain identifiers.
bool is_reserved_word(TokenKind p_kind);
// An identifier or any reserved word: what may follow a dot or name an object-literal key.
bool is_identifier_like(TokenKind p_kind);
// The reserved word spelled p_text, or Identifier.
TokenKind keyword_kind(std::string_view p_text);

} // namespace kas3
