package {
	public class Hierarchy extends Second implements Error {
	}
}

class Second extends Third {
}

class Third extends Hierarchy {
	public function f():void {
		anything();
	}
}

interface Loop extends Loop {
}

final class Sealed {
}

class Opened extends Sealed {
	var a:int;
	var a:int;
}

dynamic class Loose extends String {
}
