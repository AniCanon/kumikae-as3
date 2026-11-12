// The parts of flash.utils that the first version keeps, under their original package name.
package flash.utils {
	public dynamic native class Dictionary {
		public native function Dictionary(weakKeys:Boolean = false);
		public native function toJSON(k:String):*;
	}

	public native function getTimer():int;
}
