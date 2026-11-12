// The parts of flash.utils that the first version keeps, under their original package name.
package flash.utils {
	[Incomplete]
	public native class Dictionary {
		public native function Dictionary(weakKeys:Boolean = false);
	}

	public native function getTimer():int;
}
