package {
	public interface IDamageable extends IEntity, IEventDispatcher {
		function damage(amount:int, source:Object = null):Boolean;
		function get hp():int;
		function set hp(value:int):void;
	}
}
