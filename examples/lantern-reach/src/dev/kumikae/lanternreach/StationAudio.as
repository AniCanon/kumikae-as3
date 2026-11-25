package dev.kumikae.lanternreach {
    import dev.kumikae.lanternreach.sim.*;
    import godot.Node;
    import godot.OS;
    import godot.AudioStreamPlayer;
    import godot.AudioStream;
    import godot.ResourceLoader;

    public class StationAudio extends Node {
        private var voices:Vector.<AudioStreamPlayer> = new Vector.<AudioStreamPlayer>();
        private var sequence:RandomStream;
        private var subscription:Subscription;
        private var nextBell:int;
        private var nextPulse:int;
        private var nextDrone:int;
        public var volume:Number = 0.65;

        public function setup(game:Simulation):void {
            sequence = new RandomStream(game.random.state);
            var names:Vector.<String> = new <String>["drone", "chime", "pulse", "swing", "harpoon", "fog-bell", "fire", "splash", "gutter"];
            for each (var key:String in names) {
                var voice:AudioStreamPlayer = new AudioStreamPlayer();
                voice.stream = ResourceLoader.load("res://assets/audio/" + key + ".wav") as AudioStream;
                addChild(voice);
                voices.push(voice);
            }
            subscription = game.events.listen(receive);
        }

        public function silence():void {
            for each (var voice:AudioStreamPlayer in voices) voice.stop();
        }

        public function close(bus:EventBus):void {
            bus.remove(subscription);
            silence();
            OS.delayMsec(100);
        }

        private function sound(index:int, pitch:Number, gain:Number):void {
            var voice:AudioStreamPlayer = voices[index];
            voice.pitchScale = pitch;
            voice.volumeDb = -48 + 42 * volume + gain;
            if (volume > 0) voice.play();
        }

        private function receive(event:WorldEvent):void {
            if (event.kind == WorldEvent.SWING) sound(3, 1, -4);
            if (event.kind == WorldEvent.FIRED) {
                if (event.value == Structure.HARPOON) sound(4, 1, -8);
                if (event.value == Structure.BELL) sound(5, 0.68, -5);
                if (event.value == Structure.BRAZIER) sound(6, 1, -10);
            }
            if (event.kind == WorldEvent.BEACON_LOST) sound(8, 1, 0);
            if (event.kind == WorldEvent.WAVE_START) sound(7, 0.8, -5);
        }

        public function present(game:Simulation):void {
            if (game.tick >= nextDrone) {
                sound(0, 1, -6);
                nextDrone = game.tick + 240;
            }
            if (game.tick >= nextPulse) {
                sound(2, 1, -12);
                nextPulse = game.tick + Math.max(20, 120 - int(game.pool.count / 8));
            }
            if (game.tick >= nextBell) {
                var light:int = game.light.level[game.grid.cell(game.keeper.x, game.keeper.y)];
                var notes:Vector.<Number> = new <Number>[1, 1.122462, 1.259921, 1.498307, 1.681793];
                var note:int = (int(light / 55) + sequence.below(2)) % 5;
                sound(1, notes[note], -8);
                nextBell = game.tick + 180 + sequence.below(240);
            }
        }
    }
}
