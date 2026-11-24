package dev.kumikae.lanternreach.sim {
    public final class StateHash {
        private var high:uint = 0xcbf29ce4;
        private var low:uint = 0x84222325;

        // FNV-1a's prime is 2^40 + 435. Both partial products fit exactly in a double.
        public function word(value:uint):void {
            for (var i:int = 0; i < 4; i++) {
                low ^= value & 255;
                var product:Number = Number(low) * 435;
                var upper:Number = Number(high) * 435 + Number(low) * 256 + Math.floor(product / 4294967296.0);
                high = uint(upper % 4294967296.0);
                low = uint(product % 4294967296.0);
                value = value >>> 8;
            }
        }

        public function coordinate(value:Number):void {
            word(uint(int(Math.floor(value * 65536))));
        }

        private function hex(value:uint):String {
            var digits:String = "0123456789abcdef";
            var result:String = "";
            for (var i:int = 0; i < 8; i++) {
                result = digits.charAt(value & 15) + result;
                value = value >>> 4;
            }
            return result;
        }

        public function finish():String {
            return hex(high) + hex(low);
        }
    }
}
