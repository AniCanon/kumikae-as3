package dev.kumikae.coindash.presentation {
    import godot.AudioStreamPlayer;
    import godot.DisplayServer;
    import godot.Node;

    public class SoundBank extends Node {
        [Export] public var startPlayer:AudioStreamPlayer;
        [Export] public var silverPlayer:AudioStreamPlayer;
        [Export] public var goldPlayer:AudioStreamPlayer;
        [Export] public var hitPlayer:AudioStreamPlayer;
        [Export] public var dashPlayer:AudioStreamPlayer;
        [Export] public var tickPlayer:AudioStreamPlayer;
        [Export] public var finishPlayer:AudioStreamPlayer;

        private var silent:Boolean = false;

        override public function _ready():void {
            silent = DisplayServer.getName() == "headless";
        }

        public function playStart():void { trigger(startPlayer); }
        public function playSilver():void { trigger(silverPlayer); }
        public function playGold():void { trigger(goldPlayer); }
        public function playHit():void { trigger(hitPlayer); }
        public function playDash():void { trigger(dashPlayer); }
        public function playTick():void { trigger(tickPlayer); }
        public function playFinish():void { trigger(finishPlayer); }

        private function trigger(player:AudioStreamPlayer):void {
            if (!silent && player != null) {
                player.play();
            }
        }
    }
}
