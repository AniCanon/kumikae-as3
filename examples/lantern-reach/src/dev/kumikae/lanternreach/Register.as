package dev.kumikae.lanternreach {
    import dev.kumikae.lanternreach.sim.*;
    import godot.DirAccess;
    import godot.FileAccess;

    public final class Register {
        public var entries:Vector.<LogEntry> = new Vector.<LogEntry>();
        public var best:int;
        public var hand:int;
        public var saved:String = "";
        public var volume:Number = 0.65;
        public var shake:Boolean = true;
        public var accessible:Boolean;
        private var path:String;
        private var rules:Rules;
        private var bus:EventBus;
        private var listener:Subscription;

        public function Register(game:Simulation, location:String) {
            rules = game.rules;
            path = location;
            bus = game.events;
            listener = bus.listen(record);
            load();
        }

        public function close():void { bus.remove(listener); }

        public function load():void {
            if (!FileAccess.fileExists(path)) return;
            var data:Object = JSON.parse(FileAccess.getFileAsString(path));
            entries.length = 0;
            for each (var row:Object in data.entries) {
                var page:LogEntry = new LogEntry();
                page.hand = row.hand;
                page.night = row.night;
                page.lesson = row.lesson;
                page.lost = row.lost;
                entries.push(page);
            }
            best = data.best;
            hand = data.hand;
            saved = data.saved;
            volume = data.volume;
            shake = data.shake;
            accessible = data.accessible;
        }

        public function save():void {
            var rows:Array = [];
            for each (var page:LogEntry in entries)
                rows.push({hand:page.hand, night:page.night, lesson:page.lesson, lost:page.lost});
            var data:Object = {version:1, entries:rows, best:best, hand:hand, saved:saved,
                volume:volume, shake:shake, accessible:accessible};
            var file:FileAccess = FileAccess.open(path + ".tmp", FileAccess.WRITE);
            if (file == null) throw new Error("Cannot write keeper register");
            var stored:Boolean = file.storeString(JSON.stringify(data));
            file.close();
            if (!stored || DirAccess.renameAbsolute(path + ".tmp", path) != 0)
                throw new Error("Cannot replace keeper register");
        }

        private function record(event:WorldEvent):void {
            if (event.kind != WorldEvent.DAWN && event.kind != WorldEvent.LOST_WATCH) return;
            var page:LogEntry = new LogEntry();
            page.hand = hand;
            page.night = event.value;
            page.lost = event.kind == WorldEvent.LOST_WATCH;
            page.lesson = entries.length % rules.knowledge.length;
            entries.push(page);
            best = Math.max(best, event.value);
        }

        public function knows(subject:String, value:int):Boolean {
            for each (var page:LogEntry in entries) {
                var advice:Advice = rules.knowledge[page.lesson];
                if (advice.subject == subject && advice.value == value) return true;
            }
            return false;
        }
    }
}
