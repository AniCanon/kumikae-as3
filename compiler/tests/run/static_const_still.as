package {
    // A constant of a function's own class is read once for the whole function, whatever the function runs:
    // while the class's static fields are still being given their values it is what it is then, for as long
    // as the function called meanwhile runs, and what the class gives it afterwards.
    public class static_const_still {
        public function static_const_still() {
            trace(Early.FIRST, Early.count(), Early.N, Early.LATE);
            trace(new Later().total(), Later.seen, Later.LIMIT);
            trace(new Sub().both(), Sub.MORE);
        }
    }
}

class Early {
    public static const FIRST:int = count();
    public static const N:int = 3;
    public static const LATE:int = count();

    public static function count():int {
        var total:int = 0;
        for (var i:int = 0; i < N; i++) { total += note(i); }
        return total + N;
    }

    private static function note(value:int):int { return new Made(value).value + 1; }
}

class Made {
    public var value:int;
    public function Made(value:int) { this.value = value; }
}

class Later {
    public static var seen:String = "";
    public static const LIMIT:int = 4;

    public function total():int {
        var sum:int = 0;
        for (var i:int = 0; i < LIMIT; i++) {
            seen += i;
            sum += [i, LIMIT].length * LIMIT;
        }
        return sum + LIMIT;
    }
}

class Sub extends Later {
    public static const MORE:int = LIMIT + 2;

    public function both():int {
        var sum:int = 0;
        for (var i:int = 0; i < MORE; i++) { sum += new Made(LIMIT).value; }
        for (var j:int = MORE; j > LIMIT; j--) { sum += MORE; }
        return sum;
    }
}
