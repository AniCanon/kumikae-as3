// The top-level package of ActionScript 3, as declarations, from Adobe's ActionScript 3.0 Reference.
//
// A declaration file is ordinary AS3 in which classes and functions are `native` and have no body. The
// compiler reads it to learn which names exist; it generates no code for it.
//
// Three things are special to this file:
// - `T` inside Vector is the vector's element type.
// - A member declared here may be redefined in a subclass with or without `override`. Which one AS3 wants
//   depends on whether the member is in the AS3 namespace, on the prototype, or public, and this compiler
//   does not model that.
// - Where the reference documents two constructor forms, or leaves trailing parameters optional without
//   giving a default, the parameters here are given defaults so that every documented call is accepted.
package {
	/** The base type for AS3 objects. An Object literal stores named properties. */
	public dynamic native class Object {
		/** Returns value as an Object, or creates an empty object when value is null or undefined. */
		public native function Object(value:* = undefined);
		/** Returns whether the object has the named property. */
		public native function hasOwnProperty(name:* = null):Boolean;
		/** Returns false in Kumikae AS3, which has no prototype chain. */
		public native function isPrototypeOf(theClass:* = null):Boolean;
		/** Returns whether the named property is included when enumerating the object. */
		public native function propertyIsEnumerable(name:* = null):Boolean;
		/** Controls whether property enumeration visits the named dynamic property. */
		public native function setPropertyIsEnumerable(name:String, isEnum:Boolean = true):void;
		/** Returns a string representation suitable for display. */
		public native function toLocaleString():String;
		/** Returns the value as a string. */
		public native function toString():String;
		/** Returns the underlying value. */
		public native function valueOf():*;
	}

	/** A class definition held as a value. */
	public dynamic native class Class {}

	/** A callable value, such as a function expression or a bound method. */
	public final native class Function {
		/** The number of declared parameters, excluding the rest parameter. */
		public native function get length():int;
		/** Calls the function with arguments supplied in an array. A bound method keeps its receiver. */
		public native function apply(thisArg:* = NaN, argArray:* = NaN):*;
		/** Calls the function with the supplied arguments. A bound method keeps its receiver. */
		public native function call(thisArg:* = NaN, ...args):*;
	}

	/** A truth value, either true or false. */
	public final native class Boolean {
		/** Converts the expression to true or false. */
		public native function Boolean(expression:Object = false);
		/** Returns the value as a string. */
		public native function toString():String;
		/** Returns the underlying value. */
		public native function valueOf():Boolean;
	}

	/** A signed 32-bit integer, from -2147483648 to 2147483647. */
	public final native class int {
		/** The largest finite int value. */
		public static const MAX_VALUE:int;
		/** The smallest int value. */
		public static const MIN_VALUE:int;
		/** Converts the value to a signed 32-bit integer. */
		public native function int(num:Object = 0);
		/** Formats the number in exponential notation with fractionDigits digits after the decimal point. */
		public native function toExponential(fractionDigits:uint = 0):String;
		/** Formats the number with the requested number of digits after the decimal point. */
		public native function toFixed(fractionDigits:uint = 0):String;
		/** Formats the number to the requested number of significant digits. */
		public native function toPrecision(precision:uint = 0):String;
		/** Formats the integer in the given radix, from 2 through 36. */
		public native function toString(radix:uint = 10):String;
		/** Returns the underlying value. */
		public native function valueOf():int;
	}

	/** An unsigned 32-bit integer, from 0 to 4294967295. */
	public final native class uint {
		/** The largest finite uint value. */
		public static const MAX_VALUE:uint;
		/** The smallest uint value. */
		public static const MIN_VALUE:uint;
		/** Converts the value to an unsigned 32-bit integer. */
		public native function uint(num:Object = 0);
		/** Formats the number in exponential notation with fractionDigits digits after the decimal point. */
		public native function toExponential(fractionDigits:uint = 0):String;
		/** Formats the number with the requested number of digits after the decimal point. */
		public native function toFixed(fractionDigits:uint = 0):String;
		/** Formats the number to the requested number of significant digits. */
		public native function toPrecision(precision:uint = 0):String;
		/** Formats the integer in the given radix, from 2 through 36. */
		public native function toString(radix:uint = 10):String;
		/** Returns the underlying value. */
		public native function valueOf():uint;
	}

	// A 64-bit integer: what Godot's own integer is. An int or a uint always fits one. One converts to an
	// int, a uint or a Number where that is expected, and a RangeError says so if its value does not fit.
	/** A signed 64-bit integer. Use it to keep the full precision of a Godot integer. */
	public final native class Int64 {
		/** The largest finite Int64 value. */
		public static const MAX_VALUE:Int64;
		/** The smallest Int64 value. */
		public static const MIN_VALUE:Int64;
		/** Converts the value to a signed 64-bit integer. */
		public native function Int64(num:Object = 0);
		// Written from its own digits, which a Number would round past 2^53.
		/** Formats the number in exponential notation with fractionDigits digits after the decimal point. */
		public native function toExponential(fractionDigits:uint = 0):String;
		/** Formats the number with the requested number of digits after the decimal point. */
		public native function toFixed(fractionDigits:uint = 0):String;
		/** Formats the number to the requested number of significant digits. */
		public native function toPrecision(precision:uint = 0):String;
		/** Formats the integer in the given radix, from 2 through 36. */
		public native function toString(radix:uint = 10):String;
		/** Returns the underlying value. */
		public native function valueOf():Int64;
	}

	/** A double-precision floating-point number. */
	public final native class Number {
		/** The largest finite Number value. */
		public static const MAX_VALUE:Number;
		/** The smallest positive nonzero Number value. */
		public static const MIN_VALUE:Number;
		/** The special Number value for a result that is not a number. Test it with isNaN(). */
		public static const NaN:Number;
		/** The Number value for negative infinity. */
		public static const NEGATIVE_INFINITY:Number;
		/** The Number value for positive infinity. */
		public static const POSITIVE_INFINITY:Number;
		/** Converts the value to a double-precision number. */
		public native function Number(num:Object = 0);
		/** Formats the number in exponential notation with fractionDigits digits after the decimal point. */
		public native function toExponential(fractionDigits:uint = 0):String;
		/** Formats the number with the requested number of digits after the decimal point. */
		public native function toFixed(fractionDigits:uint = 0):String;
		/** Formats the number to the requested number of significant digits. */
		public native function toPrecision(precision:uint = 0):String;
		/** Returns a string representation of the number in the given radix. */
		public native function toString(radix:Number = 10):String;
		/** Returns the underlying value. */
		public native function valueOf():Number;
	}

	/** Immutable text indexed by UTF-16 code unit. */
	public final native class String {
		/** Converts the value to text. */
		public native function String(val:* = "");
		/** The number of UTF-16 code units. A supplementary character occupies two units. */
		public native function get length():int;
		/** Builds a string from UTF-16 code units. */
		public static native function fromCharCode(...charCodes):String;
		/** Returns the code unit at the zero-based index as a string, or an empty string outside the text. */
		public native function charAt(index:Number = 0):String;
		/** Returns the numeric UTF-16 code unit at the index, or NaN outside the text. */
		public native function charCodeAt(index:Number = 0):Number;
		/** Returns the string followed by the text of each argument. */
		public native function concat(...args):String;
		/** Returns the first index of val at or after startIndex, or -1 if absent. */
		public native function indexOf(val:String = "undefined", startIndex:Number = 0):int;
		/** Searches backward from startIndex for val and returns its index, or -1 if absent. */
		public native function lastIndexOf(val:String = "undefined", startIndex:Number = 0x7FFFFFFF):int;
		/** Compares the two strings and returns a negative, zero, or positive number for their ordering. */
		public native function localeCompare(other:String = "undefined"):int;
		/** Matches a pattern against the text and returns the matches, or null if none were found. */
		public native function match(pattern:* = null):Array;
		/** Returns text with matching parts replaced. The replacement may be a string or a function. */
		public native function replace(pattern:* = null, repl:* = null):String;
		/** Returns the first index matched by the pattern, or -1 if there is no match. */
		public native function search(pattern:* = null):int;
		/** Returns text from startIndex up to but excluding endIndex. Negative indices count from the end. */
		public native function slice(startIndex:Number = 0, endIndex:Number = 0x7FFFFFFF):String;
		/** Splits the string at the delimiter and returns at most limit parts. */
		public native function split(delimiter:* = null, limit:Number = 0x7FFFFFFF):Array;
		/** Returns up to len code units beginning at startIndex. */
		public native function substr(startIndex:Number = 0, len:Number = 0x7FFFFFFF):String;
		/** Returns the text between the two indices, excluding the larger index. */
		public native function substring(startIndex:Number = 0, endIndex:Number = 0x7FFFFFFF):String;
		/** Returns the text converted to lowercase. */
		public native function toLocaleLowerCase():String;
		/** Returns the text converted to uppercase. */
		public native function toLocaleUpperCase():String;
		/** Returns the text converted to lowercase. */
		public native function toLowerCase():String;
		/** Returns the text converted to uppercase. */
		public native function toUpperCase():String;
		/** Returns the value as a string. */
		public native function toString():String;
		/** Returns the underlying value. */
		public native function valueOf():String;
	}

	/** A resizable, zero-indexed collection that can hold values of different types. */
	public dynamic native class Array {
		/** Sorting flag that ignores letter case when comparing strings. */
		public static const CASEINSENSITIVE:uint;
		/** Sorting flag that reverses the comparison order. */
		public static const DESCENDING:uint;
		/** Sorting flag that compares elements as numbers. */
		public static const NUMERIC:uint;
		/** Sorting flag that returns the sorted indices and preserves the original order of the array. */
		public static const RETURNINDEXEDARRAY:uint;
		/** Sorting flag that leaves the array untouched and returns 0 if any two elements compare equal. */
		public static const UNIQUESORT:uint;
		/** The number of elements. Reducing the length discards trailing elements. */
		public var length:uint;
		/** Creates an array of the given values. One numeric argument sets the length. */
		public native function Array(...values);
		/** Returns a new collection with the supplied values appended. */
		public native function concat(...args):Array;
		/** Calls callback with each element, index, and collection until one returns false. Returns true if all pass. */
		public native function every(callback:Function, thisObject:* = null):Boolean;
		/** Returns a new collection containing the elements for which callback returns true. */
		public native function filter(callback:Function, thisObject:* = null):Array;
		/** Calls callback with the element, index, and collection for each element. */
		public native function forEach(callback:Function, thisObject:* = null):void;
		/** Searches forward for searchElement using strict equality. Returns its index or -1. */
		public native function indexOf(searchElement:*, fromIndex:int = 0):int;
		/** Inserts element at index and shifts the following elements one position to the right. */
		public native function insertAt(index:int, element:*):void;
		/** Returns the elements as text separated by sep. The default separator is a comma. */
		public native function join(sep:* = NaN):String;
		/** Searches backward for searchElement using strict equality. Returns its index or -1. */
		public native function lastIndexOf(searchElement:*, fromIndex:int = 0x7FFFFFFF):int;
		/** Returns a new collection containing the result of calling callback for each element. */
		public native function map(callback:Function, thisObject:* = null):Array;
		/** Removes and returns the last element. */
		public native function pop():*;
		/** Appends the supplied values and returns the new length. */
		public native function push(...args):uint;
		/** Removes and returns the element at index, shifting later elements one place to the left. */
		public native function removeAt(index:int):*;
		/** Reverses the elements in place and returns this collection. */
		public native function reverse():Array;
		/** Removes and returns the first element, shifting subsequent elements to the left. */
		public native function shift():*;
		/** Copies the elements from startIndex up to but excluding endIndex. Negative indices count from the end. */
		public native function slice(startIndex:int = 0, endIndex:int = 16777215):Array;
		/** Returns true as soon as callback accepts an element, or false if none pass. */
		public native function some(callback:Function, thisObject:* = null):Boolean;
		/** Sorts elements using the supplied comparison function or sort flags. */
		public native function sort(...args):*;
		/** Sorts objects by one or more named fields, with optional sort flags for each field. */
		public native function sortOn(fieldName:Object, options:Object = null):*;
		/** Removes deleteCount elements at startIndex, inserts any supplied values there, and returns the removed elements. */
		public native function splice(startIndex:int, deleteCount:uint = 4294967295, ...values):Array;
		/** Returns a string representation suitable for display. */
		public native function toLocaleString():String;
		/** Returns the value as a string. */
		public native function toString():String;
		/** Inserts the supplied values at the beginning and returns the new length. */
		public native function unshift(...args):uint;
	}

	/** A zero-indexed collection with a declared element type, written Vector.<T>. */
	public dynamic native class Vector {
		/** When true, the vector keeps its current length. */
		public var fixed:Boolean;
		/** The number of elements. Reducing the length discards trailing elements. */
		public var length:uint;
		/** Creates a vector with length elements of its declared type. Set fixed to prevent resizing. */
		public native function Vector(length:uint = 0, fixed:Boolean = false);
		/** Returns a new collection with the supplied values appended. */
		public native function concat(...args):Vector.<T>;
		/** Calls callback with each element, index, and collection until one returns false. Returns true if all pass. */
		public native function every(callback:Function, thisObject:Object = null):Boolean;
		/** Returns a new collection containing the elements for which callback returns true. */
		public native function filter(callback:Function, thisObject:Object = null):Vector.<T>;
		/** Calls callback with the element, index, and collection for each element. */
		public native function forEach(callback:Function, thisObject:Object = null):void;
		/** Searches forward for searchElement using strict equality. Returns its index or -1. */
		public native function indexOf(searchElement:*, fromIndex:int = 0):int;
		/** Inserts element at index and shifts the following elements one position to the right. */
		public native function insertAt(index:int, element:T):void;
		/** Returns the elements as text separated by sep. The default separator is a comma. */
		public native function join(sep:String = ","):String;
		/** Searches backward for searchElement using strict equality. Returns its index or -1. */
		public native function lastIndexOf(searchElement:*, fromIndex:int = 0x7FFFFFFF):int;
		/** Returns a new collection containing the result of calling callback for each element. */
		public native function map(callback:Function, thisObject:Object = null):Vector.<T>;
		/** Removes and returns the last element. */
		public native function pop():T;
		/** Appends the supplied values and returns the new length. */
		public native function push(...args):uint;
		/** Removes and returns the element at index, shifting later elements one place to the left. */
		public native function removeAt(index:int):T;
		/** Reverses the elements in place and returns this collection. */
		public native function reverse():Vector.<T>;
		/** Removes and returns the first element, shifting subsequent elements to the left. */
		public native function shift():T;
		/** Copies the elements from startIndex up to but excluding endIndex. Negative indices count from the end. */
		public native function slice(startIndex:int = 0, endIndex:int = 16777215):Vector.<T>;
		/** Returns true as soon as callback accepts an element, or false if none pass. */
		public native function some(callback:Function, thisObject:Object = null):Boolean;
		/** Sorts elements using the supplied comparison function or sort flags. */
		public native function sort(sortBehavior:*):Vector.<T>;
		/** Removes deleteCount elements at startIndex, inserts any supplied values there, and returns the removed elements. */
		public native function splice(startIndex:int, deleteCount:uint = 4294967295, ...items):Vector.<T>;
		/** Returns a string representation suitable for display. */
		public native function toLocaleString():String;
		/** Returns the value as a string. */
		public native function toString():String;
		/** Inserts the supplied values at the beginning and returns the new length. */
		public native function unshift(...args):uint;
	}

	/** Numeric constants and static functions for arithmetic and trigonometry. Angles are in radians. */
	public final native class Math {
		/** Euler's number, the base of the natural logarithm. */
		public static const E:Number;
		/** The natural logarithm of 10. */
		public static const LN10:Number;
		/** The natural logarithm of 2. */
		public static const LN2:Number;
		/** The base-10 logarithm of Euler's number. */
		public static const LOG10E:Number;
		/** The base-2 logarithm of Euler's number. */
		public static const LOG2E:Number;
		/** The ratio of a circle's circumference to its diameter. */
		public static const PI:Number;
		/** The square root of one half. */
		public static const SQRT1_2:Number;
		/** The square root of two. */
		public static const SQRT2:Number;
		/** Returns the absolute value. */
		public static native function abs(val:Number):Number;
		/** Returns the inverse cosine in radians. */
		public static native function acos(val:Number):Number;
		/** Returns the inverse sine in radians. */
		public static native function asin(val:Number):Number;
		/** Returns the inverse tangent in radians. */
		public static native function atan(val:Number):Number;
		/** Rounds up to the nearest integer. */
		public static native function ceil(val:Number):Number;
		/** Returns the cosine of an angle in radians. */
		public static native function cos(val:Number):Number;
		/** Returns Euler's number raised to the given power. */
		public static native function exp(val:Number):Number;
		/** Rounds down to the nearest integer. */
		public static native function floor(val:Number):Number;
		/** Returns the natural logarithm. */
		public static native function log(val:Number):Number;
		/** Rounds to the nearest integer, with ties toward positive infinity. */
		public static native function round(val:Number):Number;
		/** Returns the sine of an angle in radians. */
		public static native function sin(val:Number):Number;
		/** Returns the nonnegative square root, or NaN for a negative argument. */
		public static native function sqrt(val:Number):Number;
		/** Returns the tangent of an angle in radians. */
		public static native function tan(val:Number):Number;
		/** Returns the angle in radians from the positive x-axis to the point (x, y). */
		public static native function atan2(y:Number, x:Number):Number;
		/** Returns the largest argument, or negative infinity if called without arguments. */
		public static native function max(val1:Number = -Infinity, val2:Number = -Infinity, ...rest):Number;
		/** Returns the smallest argument, or positive infinity if called without arguments. */
		public static native function min(val1:Number = Infinity, val2:Number = Infinity, ...rest):Number;
		/** Returns base raised to exp. */
		public static native function pow(base:Number, pow:Number):Number;
		/** Returns a pseudorandom Number greater than or equal to 0 and less than 1. */
		public static native function random():Number;
	}

	/** Converts between AS3 values and JSON text. */
	public final native class JSON {
		/** Parses JSON text. An optional reviver can transform or remove properties as they are read. */
		public static native function parse(text:String, reviver:Function = null):Object;
		/** Serializes a value as JSON. Use replacer to select or transform properties and space to indent the output. */
		public static native function stringify(value:Object, replacer:* = null, space:* = null):String;
	}

	/** An instant in time stored as milliseconds since 1 January 1970 at 00:00 UTC. */
	public final dynamic native class Date {
		/** Creates a date from the current time, a timestamp, a date string, or local date and time components. */
		public native function Date(yearOrTimevalue:Object = null, month:Number = 0, date:Number = 1, hour:Number = 0,
				minute:Number = 0, second:Number = 0, millisecond:Number = 0);
		/** Parses a date string and returns its timestamp in milliseconds, or NaN if parsing fails. */
		public static native function parse(date:String):Number;
		/** Returns a timestamp in milliseconds from UTC components. January is month 0. */
		public static native function UTC(year:Number, month:Number, date:Number = 1, hour:Number = 0, minute:Number = 0,
				second:Number = 0, millisecond:Number = 0):Number;
		/** The local day of the month (1 through 31). */
		public var date:Number;
		/** The UTC day of the month (1 through 31). */
		public var dateUTC:Number;
		/** The local year. */
		public var fullYear:Number;
		/** The UTC year. */
		public var fullYearUTC:Number;
		/** The local hour (0 through 23). */
		public var hours:Number;
		/** The UTC hour (0 through 23). */
		public var hoursUTC:Number;
		/** The local millisecond (0 through 999). */
		public var milliseconds:Number;
		/** The UTC millisecond (0 through 999). */
		public var millisecondsUTC:Number;
		/** The local minute (0 through 59). */
		public var minutes:Number;
		/** The UTC minute (0 through 59). */
		public var minutesUTC:Number;
		/** The local month (0 for January). */
		public var month:Number;
		/** The UTC month (0 for January). */
		public var monthUTC:Number;
		/** The local second (0 through 59). */
		public var seconds:Number;
		/** The UTC second (0 through 59). */
		public var secondsUTC:Number;
		/** The timestamp in milliseconds since 1 January 1970 at 00:00 UTC. */
		public var time:Number;
		/** The local day of the week (0 for Sunday). */
		public native function get day():Number;
		/** The UTC day of the week (0 for Sunday). */
		public native function get dayUTC():Number;
		/** The difference between UTC and local time in minutes, positive west of UTC. */
		public native function get timezoneOffset():Number;
		/** Returns the timestamp in milliseconds since 1 January 1970 at 00:00 UTC. */
		public native function getTime():Number;
		/** Returns the difference between UTC and local time in minutes, positive west of UTC. */
		public native function getTimezoneOffset():Number;
		/** Returns the local day of the month (1 through 31). */
		public native function getDate():Number;
		/** Returns the UTC day of the month (1 through 31). */
		public native function getUTCDate():Number;
		/** Returns the local day of the week (0 for Sunday). */
		public native function getDay():Number;
		/** Returns the UTC day of the week (0 for Sunday). */
		public native function getUTCDay():Number;
		/** Returns the local year. */
		public native function getFullYear():Number;
		/** Returns the UTC year. */
		public native function getUTCFullYear():Number;
		/** Returns the local hour (0 through 23). */
		public native function getHours():Number;
		/** Returns the UTC hour (0 through 23). */
		public native function getUTCHours():Number;
		/** Returns the local millisecond (0 through 999). */
		public native function getMilliseconds():Number;
		/** Returns the UTC millisecond (0 through 999). */
		public native function getUTCMilliseconds():Number;
		/** Returns the local minute (0 through 59). */
		public native function getMinutes():Number;
		/** Returns the UTC minute (0 through 59). */
		public native function getUTCMinutes():Number;
		/** Returns the local month (0 for January). */
		public native function getMonth():Number;
		/** Returns the UTC month (0 for January). */
		public native function getUTCMonth():Number;
		/** Returns the local second (0 through 59). */
		public native function getSeconds():Number;
		/** Returns the UTC second (0 through 59). */
		public native function getUTCSeconds():Number;
		/** Sets the local day of the month (1 through 31) and returns the updated timestamp in milliseconds. */
		public native function setDate(day:Number):Number;
		/** Sets the local year and returns the updated timestamp in milliseconds. */
		public native function setFullYear(year:Number, month:Number = NaN, day:Number = NaN):Number;
		/** Sets the local hour (0 through 23) and returns the updated timestamp in milliseconds. */
		public native function setHours(hour:Number, minute:Number = NaN, second:Number = NaN, millisecond:Number = NaN):Number;
		/** Sets the local millisecond (0 through 999) and returns the updated timestamp in milliseconds. */
		public native function setMilliseconds(millisecond:Number):Number;
		/** Sets the local minute (0 through 59) and returns the updated timestamp in milliseconds. */
		public native function setMinutes(minute:Number, second:Number = NaN, millisecond:Number = NaN):Number;
		/** Sets the local month (0 for January) and returns the updated timestamp in milliseconds. */
		public native function setMonth(month:Number, day:Number = NaN):Number;
		/** Sets the local second (0 through 59) and returns the updated timestamp in milliseconds. */
		public native function setSeconds(second:Number, millisecond:Number = NaN):Number;
		/** Sets the UTC day of the month (1 through 31) and returns the updated timestamp in milliseconds. */
		public native function setUTCDate(day:Number):Number;
		/** Sets the UTC year and returns the updated timestamp in milliseconds. */
		public native function setUTCFullYear(year:Number, month:Number = NaN, day:Number = NaN):Number;
		/** Sets the UTC hour (0 through 23) and returns the updated timestamp in milliseconds. */
		public native function setUTCHours(hour:Number, minute:Number = NaN, second:Number = NaN, millisecond:Number = NaN):Number;
		/** Sets the UTC millisecond (0 through 999) and returns the updated timestamp in milliseconds. */
		public native function setUTCMilliseconds(millisecond:Number):Number;
		/** Sets the UTC minute (0 through 59) and returns the updated timestamp in milliseconds. */
		public native function setUTCMinutes(minute:Number, second:Number = NaN, millisecond:Number = NaN):Number;
		/** Sets the UTC month (0 for January) and returns the updated timestamp in milliseconds. */
		public native function setUTCMonth(month:Number, day:Number = NaN):Number;
		/** Sets the UTC second (0 through 59) and returns the updated timestamp in milliseconds. */
		public native function setUTCSeconds(second:Number, millisecond:Number = NaN):Number;
		/** Sets the timestamp in milliseconds since 1 January 1970 at 00:00 UTC and returns the new value. */
		public native function setTime(millisecond:Number):Number;
		/** Formats the local date without the time. */
		public native function toDateString():String;
		/** Returns the local date and time as text, using the same format as toString(). */
		public native function toJSON(k:String):*;
		/** Returns a display string for the local date. */
		public native function toLocaleDateString():String;
		/** Returns a display string for the local date and time. */
		public native function toLocaleString():String;
		/** Returns a display string for the local time. */
		public native function toLocaleTimeString():String;
		/** Formats the local date and time with the time zone. */
		public native function toString():String;
		/** Formats the local time with its time zone. */
		public native function toTimeString():String;
		/** Formats the date and time in UTC. */
		public native function toUTCString():String;
		/** Returns the timestamp in milliseconds. */
		public native function valueOf():Number;
	}

	/** A regular expression for searching and matching text. */
	public dynamic native class RegExp {
		/** The index at which the next global match starts. */
		public var lastIndex:int;
		/** Compiles a pattern and optional flags. An invalid pattern raises SyntaxError. */
		public native function RegExp(re:* = null, flags:* = null);
		/** Whether a dot also matches a line break (s flag). */
		public native function get dotall():Boolean;
		/** Whether unescaped whitespace and pattern comments are ignored (x flag). */
		public native function get extended():Boolean;
		/** Whether successive matches advance lastIndex (g flag). */
		public native function get global():Boolean;
		/** Whether matches ignore letter case (i flag). */
		public native function get ignoreCase():Boolean;
		/** Whether the start and end anchors can match line boundaries (m flag). */
		public native function get multiline():Boolean;
		/** The text of the regular expression pattern. */
		public native function get source():String;
		/** Returns the match and captured groups, or null if no match was found. */
		public native function exec(str:String = ""):*;
		/** Returns whether the pattern matches the string. */
		public native function test(str:String):Boolean;
	}

	/** The base class for errors. An error carries a message and can report an AS3 stack trace. */
	public dynamic native class Error {
		/** The error's message text. */
		public var message:String;
		/** The name displayed for this error. */
		public var name:String;
		/** Creates an error with the given message and numeric identifier. */
		public native function Error(message:* = "", id:* = 0);
		/** The numeric identifier associated with the error. */
		public native function get errorID():int;
		/** Returns the AS3 stack trace for the error. */
		public native function getStackTrace():String;
	}
	/** An error indicating an invalid argument or argument count. */
	public dynamic native class ArgumentError extends Error {
		/** Creates an error with the given message and numeric identifier. */
		public native function ArgumentError(message:* = "", id:* = 0);
	}
	/** An error concerning a definition in the program. */
	public dynamic native class DefinitionError extends Error {
		/** Creates an error with the given message and numeric identifier. */
		public native function DefinitionError(message:* = "", id:* = 0);
	}
	/** The AS3 error type for evaluation errors. */
	public dynamic native class EvalError extends Error {
		/** Creates an error with the given message and numeric identifier. */
		public native function EvalError(message:* = "", id:* = 0);
	}
	/** An error indicating a value outside the permitted range. */
	public dynamic native class RangeError extends Error {
		/** Creates an error with the given message and numeric identifier. */
		public native function RangeError(message:* = "", id:* = 0);
	}
	/** An error indicating that a referenced name or property cannot be used. */
	public dynamic native class ReferenceError extends Error {
		/** Creates an error with the given message and numeric identifier. */
		public native function ReferenceError(message:* = "", id:* = 0);
	}
	/** The AS3 error type for a security restriction. */
	public dynamic native class SecurityError extends Error {
		/** Creates an error with the given message and numeric identifier. */
		public native function SecurityError(message:* = "", id:* = 0);
	}
	/** An error indicating invalid syntax, such as malformed JSON or a regular expression. */
	public dynamic native class SyntaxError extends Error {
		/** Creates an error with the given message and numeric identifier. */
		public native function SyntaxError(message:* = "", id:* = 0);
	}
	/** An error indicating that an operation cannot be performed on a value of this type. */
	public dynamic native class TypeError extends Error {
		/** Creates an error with the given message and numeric identifier. */
		public native function TypeError(message:* = "", id:* = 0);
	}
	/** An error indicating a malformed URI or invalid escape sequence. */
	public dynamic native class URIError extends Error {
		/** Creates an error with the given message and numeric identifier. */
		public native function URIError(message:* = "", id:* = 0);
	}
	/** The AS3 error type for a program that fails verification. */
	public dynamic native class VerifyError extends Error {
		/** Creates an error with the given message and numeric identifier. */
		public native function VerifyError(message:* = "", id:* = 0);
	}

	/** The value of an uninitialized untyped variable or a missing dynamic property. */
	public const undefined:*;
	/** The special Number value for a result that is not a number. Test it with isNaN(). */
	public const NaN:Number;
	/** The Number value for positive infinity. */
	public const Infinity:Number;

	/** Writes the arguments as text separated by spaces, followed by a newline. */
	public native function trace(...rest):void;
	/** Parses the leading integer in text using radix, or returns NaN if no integer can be read. */
	public native function parseInt(text:String = "NaN", radix:int = 0):Number;
	/** Parses the leading floating-point number in text, or returns NaN if no number can be read. */
	public native function parseFloat(text:String = "NaN"):Number;
	/** Returns whether the value converts to NaN. */
	public native function isNaN(value:Number = undefined):Boolean;
	/** Returns whether the value converts to a finite number. */
	public native function isFinite(value:Number = undefined):Boolean;
	/** Escapes special characters using percent encoding, including %uXXXX for larger code units. */
	public native function escape(text:String = "undefined"):String;
	/** Decodes the percent escapes written by escape(). */
	public native function unescape(text:String = "undefined"):String;
	/** Percent-encodes a URI, preserving reserved characters used to separate its parts. */
	public native function encodeURI(uri:String = "undefined"):String;
	/** Percent-encodes one URI component, including characters that would otherwise separate parts of a URI. */
	public native function encodeURIComponent(uri:String = "undefined"):String;
	/** Decodes percent escapes in a URI while preserving escapes for reserved URI characters. */
	public native function decodeURI(uri:String = "undefined"):String;
	/** Decodes a percent-encoded URI component. */
	public native function decodeURIComponent(uri:String = "undefined"):String;
}
