package {
	[Tool]
	[Signal(name="hit", args="amount:int")]
	public class Tagged {
		[Export] public var speed:Number = 200
		[Export(min=0, max=10)]
		[Inspectable]
		public var level:int = 1

		[Embed(source="a.png", mimeType="image/png")]
		private static const ICON:Class;
	}
}
