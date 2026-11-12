package game {
	import godot.Node2D;
	import godot.Vector2;

	public class Inventory extends Node2D {
		private var items:Vector.<Item> = new Vector.<Item>();
		private var byName:Object = {};
		private var capacity:int = 8;

		public function add(item:Item):Boolean {
			if (items.length >= capacity) {
				return false;
			}
			items.push(item);
			byName[item.name] = item;
			item.owner = this;
			return true;
		}

		public function heaviest():Item {
			var best:Item = null;
			for each (var item:Item in items) {
				best = best == null || item.weight > best.weight ? item : best;
			}
			return best;
		}

		public function get totalWeight():Number {
			var total:Number = 0;
			for (var i:int = 0; i < items.length; i++) {
				total += items[i].weight * items[i].count;
			}
			return total;
		}

		override public function _process(delta:Number):void {
			var where:Vector2 = position;
			var half:Number = where.x / 2 + totalWeight;
			var label:String = "Items: " + items.length + " of " + capacity;
			var names:Array = items.map(function(item:Item, index:int, all:Vector.<Item>):String {
				return item.name.toUpperCase();
			});
			var first:String = names.length > 0 ? names[0] : null;
			var slots:int = capacity - items.length;
			var mask:int = slots << 2 | 1;
			var ratio:Number = items.length / capacity;
			trace(label, half, first, mask, ratio, Math.round(ratio * 100), heaviest().name.length);
		}
	}
}

import game.Inventory;

class Item {
	public var name:String;
	public var weight:Number = 1;
	public var count:uint = 1;
	public var owner:Inventory;
}
