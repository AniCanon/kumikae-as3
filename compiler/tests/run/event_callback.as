package {
    public class event_callback {
        private function notify(cb:(Notice)=>void, event:Notice):void {
            cb(event);
        }

        public function event_callback() {
            var event:Notice = new Notice();
            event.code = 7;
            var seen:int = 0;
            var listener:(Notice)=>void = function(item:Notice):void {
                if (item != null) seen += item.code;
            };
            notify(listener, event);
            notify(listener, null);
            trace(seen);
            var erased:Function = notify;
            var other:Function = function(item:Other):void { trace("wrong listener"); };
            try { erased(other, null); } catch (error:TypeError) { trace("wrong signature"); }
        }
    }
}
class Notice { public var code:int; }
class Other {}
