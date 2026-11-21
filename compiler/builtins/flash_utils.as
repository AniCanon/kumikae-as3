// The parts of flash.utils that the first version keeps, under their original package name.
package flash.utils {
	/** A collection of key-value pairs. Objects used as keys are compared by identity. */
	public dynamic native class Dictionary {
		/** Creates a dictionary with strong keys. Kumikae AS3 does not support weak keys. */
		public native function Dictionary(weakKeys:Boolean = false);
		/** Returns the string "Dictionary" when the dictionary is serialized as JSON. */
		public native function toJSON(k:String):*;
	}

	/** AS3 elapsed time in milliseconds. Kumikae declares getTimer for type checking only. */
	public native function getTimer():int;
}
