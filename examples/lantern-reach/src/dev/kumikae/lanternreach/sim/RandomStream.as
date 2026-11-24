package dev.kumikae.lanternreach.sim {
    public final class RandomStream {
        public var state:uint;

        public function RandomStream(seed:uint) {
            state = seed == 0 ? 1 : seed;
        }

        public function next():uint {
            var x:uint = state;
            x ^= x << 13;
            x ^= x >>> 17;
            x ^= x << 5;
            state = x;
            return x;
        }

        public function below(limit:int):int {
            return int(next() % uint(limit));
        }
    }
}
