#include "typer.h"

namespace kas3 {

const Type * Typer::unary(Unary *p_unary) {
	const Type *operand = expression(p_unary->operand);
	if (p_unary->op == TokenKind::Minus || p_unary->op == TokenKind::Plus) {
		if (const Type *result = described_unary(p_unary->op, operand, p_unary->offset)) {
			return result;
		}
	}
	switch (p_unary->op) {
		case TokenKind::Bang:
		case TokenKind::KwDelete:
			return boolean_;
		case TokenKind::KwTypeof:
			return string_;
		case TokenKind::KwVoid:
			return any_;
		case TokenKind::Tilde:
			if (never_operator(p_unary->op, operand, p_unary->offset)) {
				return unknown_;
			}
			return operand == int64_ ? int64_ : (primitive(operand) ? int_ : unknown_);
		case TokenKind::PlusPlus:
		case TokenKind::MinusMinus:
			if (never_operator(p_unary->op, operand, p_unary->offset)) {
				return unknown_;
			}
			written(p_unary->operand);
			return numeric(operand) ? operand : (primitive(operand) ? number_ : unknown_);
		case TokenKind::Minus:
			if (p_unary->operand && p_unary->operand->kind == NodeKind::NumberLit) {
				// `-1` is an int, and so is -2147483648, whose digits alone are not.
				// Not -0: an int has no negative zero.
				unsigned long long value = 0;
				if (whole_value(static_cast<NumberLit *>(p_unary->operand), &value) && value > 0 &&
						value <= 2147483648ull) {
					return int_;
				}
			}
			return operand == int64_ ? int64_ : (primitive(operand) ? number_ : unknown_);
		case TokenKind::Plus:
			return operand == int64_ ? int64_ : (primitive(operand) ? number_ : unknown_);
		default:
			return unknown_;
	}
}

const Type * Typer::arithmetic(TokenKind p_op, const Type *p_left, const Type *p_right) {
	if (p_left->kind == Type::Unknown || p_right->kind == Type::Unknown) {
		return unknown_;
	}
	// A string plus anything is a string: the other side is converted.
	if ((p_op == TokenKind::Plus || p_op == TokenKind::PlusAssign) && (p_left == string_ || p_right == string_)) {
		return string_;
	}
	// On anything but the primitives the result depends on the class. Godot's math types get their
	// rules in a later stage; until then the type is unknown.
	if (!primitive(p_left) || !primitive(p_right)) {
		return unknown_;
	}
	if (p_left == int64_ || p_right == int64_) {
		// An Int64 keeps its 64 bits through whole-number arithmetic. Division gives a Number, as it
		// always does in AS3, and so does anything with a Number on the other side.
		switch (p_op) {
			case TokenKind::Amp:
			case TokenKind::Pipe:
			case TokenKind::Caret:
			case TokenKind::ShiftLeft:
			case TokenKind::ShiftRight:
			case TokenKind::ShiftRightUnsigned:
			case TokenKind::AmpAssign:
			case TokenKind::PipeAssign:
			case TokenKind::CaretAssign:
			case TokenKind::ShiftLeftAssign:
			case TokenKind::ShiftRightAssign:
			case TokenKind::ShiftRightUnsignedAssign:
				return int64_;
			case TokenKind::Plus:
			case TokenKind::PlusAssign:
			case TokenKind::Minus:
			case TokenKind::MinusAssign:
			case TokenKind::Star:
			case TokenKind::StarAssign:
			case TokenKind::Percent:
			case TokenKind::PercentAssign:
				if (integral(p_left) && integral(p_right)) {
					return int64_;
				}
				break;
			default:
				break;
		}
	}
	switch (p_op) {
		case TokenKind::Plus:
		case TokenKind::PlusAssign:
			if (p_left == string_ || p_right == string_) {
				return string_;
			}
			if (p_left->kind == Type::Any || p_right->kind == Type::Any) {
				return any_; // A string or a number, decided when it runs.
			}
			return number_;
		case TokenKind::Amp:
		case TokenKind::Pipe:
		case TokenKind::Caret:
		case TokenKind::ShiftLeft:
		case TokenKind::ShiftRight:
		case TokenKind::AmpAssign:
		case TokenKind::PipeAssign:
		case TokenKind::CaretAssign:
		case TokenKind::ShiftLeftAssign:
		case TokenKind::ShiftRightAssign:
			return int_;
		case TokenKind::ShiftRightUnsigned:
		case TokenKind::ShiftRightUnsignedAssign:
			return uint_;
		default:
			// AS3 computes int + int as a Number; the wrap to 32 bits happens when it is stored in an int.
			return number_;
	}
}

const Type * Typer::binary(Binary *p_binary) {
	const Type *left = expression(p_binary->left);
	const Type *right = expression(p_binary->right);
	// A whole number written out beside an Int64 is an Int64 too, read digit by digit.
	// Not under `/`: division is done in Numbers.
	if (p_binary->op != TokenKind::Slash && left == int64_ && wide_whole(p_binary->right)) {
		right = int64_;
	} else if (p_binary->op != TokenKind::Slash && right == int64_ && wide_whole(p_binary->left)) {
		left = int64_;
	}
	switch (p_binary->op) {
		case TokenKind::Less:
		case TokenKind::Greater:
		case TokenKind::LessEqual:
		case TokenKind::GreaterEqual:
			described_binary(p_binary->op, p_binary->left, p_binary->right, p_binary->offset);
			return boolean_;
		case TokenKind::Equal:
		case TokenKind::NotEqual:
		case TokenKind::StrictEqual:
		case TokenKind::StrictNotEqual:
		case TokenKind::KwIs:
		case TokenKind::KwIn:
		case TokenKind::KwInstanceof:
			return boolean_;
		case TokenKind::KwAs:
			if (right->kind == Type::ClassObject) {
				// The type named, as in Adobe's compiler: `x as Number` may be stored in a Number.
				// (When the value is not one, the null it gives becomes 0 on being stored.)
				return right->element;
			}
			return right->kind == Type::Unknown ? unknown_ : any_;
		case TokenKind::AmpAmp:
		case TokenKind::PipePipe:
			return join(left, right);
		case TokenKind::Comma:
			return right;
		default:
			// A string plus anything is a string, whatever the other side is.
			if (!(p_binary->op == TokenKind::Plus && (left == string_ || right == string_))) {
				if (const Type *result = described_binary(p_binary->op, p_binary->left, p_binary->right,
							p_binary->offset)) {
					return result;
				}
			}
			return arithmetic(p_binary->op, left, right);
	}
}

} // namespace kas3
