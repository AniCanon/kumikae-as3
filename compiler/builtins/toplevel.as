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
	public dynamic native class Object {
		public native function Object(value:* = undefined);
		public native function hasOwnProperty(name:* = null):Boolean;
		public native function isPrototypeOf(theClass:* = null):Boolean;
		public native function propertyIsEnumerable(name:* = null):Boolean;
		public native function setPropertyIsEnumerable(name:String, isEnum:Boolean = true):void;
		public native function toLocaleString():String;
		public native function toString():String;
		public native function valueOf():*;
	}

	public dynamic native class Class {}

	public final native class Function {
		public native function get length():int;
		public native function apply(thisArg:* = NaN, argArray:* = NaN):*;
		public native function call(thisArg:* = NaN, ...args):*;
	}

	public final native class Boolean {
		public native function Boolean(expression:Object = false);
		public native function toString():String;
		public native function valueOf():Boolean;
	}

	public final native class int {
		public static const MAX_VALUE:int;
		public static const MIN_VALUE:int;
		public native function int(num:Object = 0);
		public native function toExponential(fractionDigits:uint = 0):String;
		public native function toFixed(fractionDigits:uint = 0):String;
		public native function toPrecision(precision:uint = 0):String;
		public native function toString(radix:uint = 10):String;
		public native function valueOf():int;
	}

	public final native class uint {
		public static const MAX_VALUE:uint;
		public static const MIN_VALUE:uint;
		public native function uint(num:Object = 0);
		public native function toExponential(fractionDigits:uint = 0):String;
		public native function toFixed(fractionDigits:uint = 0):String;
		public native function toPrecision(precision:uint = 0):String;
		public native function toString(radix:uint = 10):String;
		public native function valueOf():uint;
	}

	// A 64-bit integer: what Godot's own integer is. An int or a uint always fits one. One converts to an
	// int, a uint or a Number where that is expected, and a RangeError says so if its value does not fit.
	public final native class Int64 {
		public static const MAX_VALUE:Int64;
		public static const MIN_VALUE:Int64;
		public native function Int64(num:Object = 0);
		// Written from its own digits, which a Number would round past 2^53.
		public native function toExponential(fractionDigits:uint = 0):String;
		public native function toFixed(fractionDigits:uint = 0):String;
		public native function toPrecision(precision:uint = 0):String;
		public native function toString(radix:uint = 10):String;
		public native function valueOf():Int64;
	}

	public final native class Number {
		public static const MAX_VALUE:Number;
		public static const MIN_VALUE:Number;
		public static const NaN:Number;
		public static const NEGATIVE_INFINITY:Number;
		public static const POSITIVE_INFINITY:Number;
		public native function Number(num:Object = 0);
		public native function toExponential(fractionDigits:uint = 0):String;
		public native function toFixed(fractionDigits:uint = 0):String;
		public native function toPrecision(precision:uint = 0):String;
		public native function toString(radix:Number = 10):String;
		public native function valueOf():Number;
	}

	public final native class String {
		public native function String(val:* = "");
		public native function get length():int;
		public static native function fromCharCode(...charCodes):String;
		public native function charAt(index:Number = 0):String;
		public native function charCodeAt(index:Number = 0):Number;
		public native function concat(...args):String;
		public native function indexOf(val:String = "undefined", startIndex:Number = 0):int;
		public native function lastIndexOf(val:String = "undefined", startIndex:Number = 0x7FFFFFFF):int;
		public native function localeCompare(other:String, ...values):int;
		public native function match(pattern:* = null):Array;
		public native function replace(pattern:* = null, repl:* = null):String;
		public native function search(pattern:* = null):int;
		public native function slice(startIndex:Number = 0, endIndex:Number = 0x7FFFFFFF):String;
		public native function split(delimiter:* = null, limit:Number = 0x7FFFFFFF):Array;
		public native function substr(startIndex:Number = 0, len:Number = 0x7FFFFFFF):String;
		public native function substring(startIndex:Number = 0, endIndex:Number = 0x7FFFFFFF):String;
		public native function toLocaleLowerCase():String;
		public native function toLocaleUpperCase():String;
		public native function toLowerCase():String;
		public native function toUpperCase():String;
		public native function toString():String;
		public native function valueOf():String;
	}

	public dynamic native class Array {
		public static const CASEINSENSITIVE:uint;
		public static const DESCENDING:uint;
		public static const NUMERIC:uint;
		public static const RETURNINDEXEDARRAY:uint;
		public static const UNIQUESORT:uint;
		public var length:uint;
		public native function Array(...values);
		public native function concat(...args):Array;
		public native function every(callback:Function, thisObject:* = null):Boolean;
		public native function filter(callback:Function, thisObject:* = null):Array;
		public native function forEach(callback:Function, thisObject:* = null):void;
		public native function indexOf(searchElement:*, fromIndex:int = 0):int;
		public native function insertAt(index:int, element:*):void;
		public native function join(sep:* = NaN):String;
		public native function lastIndexOf(searchElement:*, fromIndex:int = 0x7FFFFFFF):int;
		public native function map(callback:Function, thisObject:* = null):Array;
		public native function pop():*;
		public native function push(...args):uint;
		public native function removeAt(index:int):*;
		public native function reverse():Array;
		public native function shift():*;
		public native function slice(startIndex:int = 0, endIndex:int = 16777215):Array;
		public native function some(callback:Function, thisObject:* = null):Boolean;
		public native function sort(...args):*;
		public native function sortOn(fieldName:Object, options:Object = null):*;
		public native function splice(startIndex:int, deleteCount:uint = 4294967295, ...values):Array;
		public native function toLocaleString():String;
		public native function toString():String;
		public native function unshift(...args):uint;
	}

	public dynamic native class Vector {
		public var fixed:Boolean;
		public var length:uint;
		public native function Vector(length:uint = 0, fixed:Boolean = false);
		public native function concat(...args):Vector.<T>;
		public native function every(callback:Function, thisObject:Object = null):Boolean;
		public native function filter(callback:Function, thisObject:Object = null):Vector.<T>;
		public native function forEach(callback:Function, thisObject:Object = null):void;
		public native function indexOf(searchElement:*, fromIndex:int = 0):int;
		public native function insertAt(index:int, element:T):void;
		public native function join(sep:String = ","):String;
		public native function lastIndexOf(searchElement:*, fromIndex:int = 0x7FFFFFFF):int;
		public native function map(callback:Function, thisObject:Object = null):Vector.<T>;
		public native function pop():T;
		public native function push(...args):uint;
		public native function removeAt(index:int):T;
		public native function reverse():Vector.<T>;
		public native function shift():T;
		public native function slice(startIndex:int = 0, endIndex:int = 16777215):Vector.<T>;
		public native function some(callback:Function, thisObject:Object = null):Boolean;
		public native function sort(sortBehavior:*):Vector.<T>;
		public native function splice(startIndex:int, deleteCount:uint = 4294967295, ...items):Vector.<T>;
		public native function toLocaleString():String;
		public native function toString():String;
		public native function unshift(...args):uint;
	}

	public final native class Math {
		public static const E:Number;
		public static const LN10:Number;
		public static const LN2:Number;
		public static const LOG10E:Number;
		public static const LOG2E:Number;
		public static const PI:Number;
		public static const SQRT1_2:Number;
		public static const SQRT2:Number;
		public static native function abs(val:Number):Number;
		public static native function acos(val:Number):Number;
		public static native function asin(val:Number):Number;
		public static native function atan(val:Number):Number;
		public static native function ceil(val:Number):Number;
		public static native function cos(val:Number):Number;
		public static native function exp(val:Number):Number;
		public static native function floor(val:Number):Number;
		public static native function log(val:Number):Number;
		public static native function round(val:Number):Number;
		public static native function sin(val:Number):Number;
		public static native function sqrt(val:Number):Number;
		public static native function tan(val:Number):Number;
		public static native function atan2(y:Number, x:Number):Number;
		public static native function max(val1:Number = -Infinity, val2:Number = -Infinity, ...rest):Number;
		public static native function min(val1:Number = Infinity, val2:Number = Infinity, ...rest):Number;
		public static native function pow(base:Number, pow:Number):Number;
		public static native function random():Number;
	}

	public final native class JSON {
		public static native function parse(text:String, reviver:Function = null):Object;
		public static native function stringify(value:Object, replacer:* = null, space:* = null):String;
	}

	public final dynamic native class Date {
		public native function Date(yearOrTimevalue:Object = null, month:Number = 0, date:Number = 1, hour:Number = 0,
				minute:Number = 0, second:Number = 0, millisecond:Number = 0);
		public static native function parse(date:String):Number;
		public static native function UTC(year:Number, month:Number, date:Number = 1, hour:Number = 0, minute:Number = 0,
				second:Number = 0, millisecond:Number = 0):Number;
		public var date:Number;
		public var dateUTC:Number;
		public var fullYear:Number;
		public var fullYearUTC:Number;
		public var hours:Number;
		public var hoursUTC:Number;
		public var milliseconds:Number;
		public var millisecondsUTC:Number;
		public var minutes:Number;
		public var minutesUTC:Number;
		public var month:Number;
		public var monthUTC:Number;
		public var seconds:Number;
		public var secondsUTC:Number;
		public var time:Number;
		public native function get day():Number;
		public native function get dayUTC():Number;
		public native function get timezoneOffset():Number;
		public native function getTime():Number;
		public native function getTimezoneOffset():Number;
		public native function getDate():Number;
		public native function getUTCDate():Number;
		public native function getDay():Number;
		public native function getUTCDay():Number;
		public native function getFullYear():Number;
		public native function getUTCFullYear():Number;
		public native function getHours():Number;
		public native function getUTCHours():Number;
		public native function getMilliseconds():Number;
		public native function getUTCMilliseconds():Number;
		public native function getMinutes():Number;
		public native function getUTCMinutes():Number;
		public native function getMonth():Number;
		public native function getUTCMonth():Number;
		public native function getSeconds():Number;
		public native function getUTCSeconds():Number;
		public native function setDate(day:Number):Number;
		public native function setFullYear(year:Number, month:Number = NaN, day:Number = NaN):Number;
		public native function setHours(hour:Number, minute:Number = NaN, second:Number = NaN, millisecond:Number = NaN):Number;
		public native function setMilliseconds(millisecond:Number):Number;
		public native function setMinutes(minute:Number, second:Number = NaN, millisecond:Number = NaN):Number;
		public native function setMonth(month:Number, day:Number = NaN):Number;
		public native function setSeconds(second:Number, millisecond:Number = NaN):Number;
		public native function setUTCDate(day:Number):Number;
		public native function setUTCFullYear(year:Number, month:Number = NaN, day:Number = NaN):Number;
		public native function setUTCHours(hour:Number, minute:Number = NaN, second:Number = NaN, millisecond:Number = NaN):Number;
		public native function setUTCMilliseconds(millisecond:Number):Number;
		public native function setUTCMinutes(minute:Number, second:Number = NaN, millisecond:Number = NaN):Number;
		public native function setUTCMonth(month:Number, day:Number = NaN):Number;
		public native function setUTCSeconds(second:Number, millisecond:Number = NaN):Number;
		public native function setTime(millisecond:Number):Number;
		public native function toDateString():String;
		public native function toJSON(k:String):*;
		public native function toLocaleDateString():String;
		public native function toLocaleString():String;
		public native function toLocaleTimeString():String;
		public native function toString():String;
		public native function toTimeString():String;
		public native function toUTCString():String;
		public native function valueOf():Number;
	}

	public dynamic native class RegExp {
		public var lastIndex:int;
		public native function RegExp(re:* = null, flags:* = null);
		public native function get dotall():Boolean;
		public native function get extended():Boolean;
		public native function get global():Boolean;
		public native function get ignoreCase():Boolean;
		public native function get multiline():Boolean;
		public native function get source():String;
		public native function exec(str:String = ""):*;
		public native function test(str:String):Boolean;
	}

	public dynamic native class Error {
		public var message:String;
		public var name:String;
		public native function Error(message:* = "", id:* = 0);
		public native function get errorID():int;
		public native function getStackTrace():String;
	}
	public dynamic native class ArgumentError extends Error {
		public native function ArgumentError(message:* = "", id:* = 0);
	}
	public dynamic native class DefinitionError extends Error {
		public native function DefinitionError(message:* = "", id:* = 0);
	}
	public dynamic native class EvalError extends Error {
		public native function EvalError(message:* = "", id:* = 0);
	}
	public dynamic native class RangeError extends Error {
		public native function RangeError(message:* = "", id:* = 0);
	}
	public dynamic native class ReferenceError extends Error {
		public native function ReferenceError(message:* = "", id:* = 0);
	}
	public dynamic native class SecurityError extends Error {
		public native function SecurityError(message:* = "", id:* = 0);
	}
	public dynamic native class SyntaxError extends Error {
		public native function SyntaxError(message:* = "", id:* = 0);
	}
	public dynamic native class TypeError extends Error {
		public native function TypeError(message:* = "", id:* = 0);
	}
	public dynamic native class URIError extends Error {
		public native function URIError(message:* = "", id:* = 0);
	}
	public dynamic native class VerifyError extends Error {
		public native function VerifyError(message:* = "", id:* = 0);
	}

	public const undefined:*;
	public const NaN:Number;
	public const Infinity:Number;

	public native function trace(...rest):void;
	public native function parseInt(text:String = "NaN", radix:int = 0):Number;
	public native function parseFloat(text:String = "NaN"):Number;
	public native function isNaN(value:Number = undefined):Boolean;
	public native function isFinite(value:Number = undefined):Boolean;
	public native function escape(text:String = "undefined"):String;
	public native function unescape(text:String = "undefined"):String;
	public native function encodeURI(uri:String = "undefined"):String;
	public native function encodeURIComponent(uri:String = "undefined"):String;
	public native function decodeURI(uri:String = "undefined"):String;
	public native function decodeURIComponent(uri:String = "undefined"):String;
}
