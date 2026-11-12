// The top-level package of ActionScript 3, as declarations.
//
// A declaration file is ordinary AS3 in which every class and function is `native` and has no body. The
// compiler reads it to learn which names exist; it generates no code for it.
//
// Stage 1c (types) adds the full member lists. Until then only the members that a subclass can reach by
// their bare names are declared: those of Object and of the Error family. A class that can be extended and
// whose members are still missing is marked [Incomplete]: the compiler reports no undefined name inside
// its subclasses.
//
// In AS3, Object's methods live on the prototype, so a class may define its own without `override`. The
// compiler knows that about Object.
package {
	public native class Object {
		public native function hasOwnProperty(name:String):Boolean;
		public native function isPrototypeOf(theClass:Object):Boolean;
		public native function propertyIsEnumerable(name:String):Boolean;
		public native function setPropertyIsEnumerable(name:String, isEnum:Boolean = true):void;
		public native function toLocaleString():String;
		public native function toString():String;
		public native function valueOf():Object;
	}
	public final native class Class {}
	public final native class Function {}
	public final native class Boolean {}
	public final native class int {}
	public final native class uint {}
	public final native class Number {}
	public final native class String {}
	[Incomplete]
	public native class Array {}
	public final native class Vector {}
	public final native class Math {}
	public final native class JSON {}
	[Incomplete]
	public native class Date {}
	[Incomplete]
	public native class RegExp {}

	public native class Error {
		public var message:String;
		public var name:String;
		public native function Error(message:* = "", id:* = 0);
		public native function get errorID():int;
		public native function getStackTrace():String;
	}
	public native class ArgumentError extends Error {}
	public native class DefinitionError extends Error {}
	public native class EvalError extends Error {}
	public native class RangeError extends Error {}
	public native class ReferenceError extends Error {}
	public native class SecurityError extends Error {}
	public native class SyntaxError extends Error {}
	public native class TypeError extends Error {}
	public native class URIError extends Error {}
	public native class VerifyError extends Error {}

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
