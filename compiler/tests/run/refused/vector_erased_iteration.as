package {
    public class vector_erased_iteration {
        public function vector_erased_iteration() {
            var sequence:Vector.<*> = new Vector.<*>();
            for each (var row:Vector.<int> in sequence) { trace(row); }
        }
    }
}
