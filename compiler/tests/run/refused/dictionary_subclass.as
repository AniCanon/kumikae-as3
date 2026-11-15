package { public class dictionary_subclass { public function dictionary_subclass() { var c:Cache = new Cache(); c["a"] = 1; } } }
import flash.utils.Dictionary;
class Cache extends Dictionary {}
