package { public class catch_kind { public function catch_kind() { try { trace(1); } catch (e:Int64) { trace(2); } a: b: for (var i:int = 0; i < 2; i++) { continue a; } } } }
