package dev.kumikae.lanternreach.sim {
    public final class CommandQueue {
        private var commands:Vector.<TickCommand> = new Vector.<TickCommand>();
        private var cursor:int;

        public function enqueue(command:TickCommand):void {
            if (commands.length > cursor && commands[commands.length - 1].tick > command.tick) {
                throw new Error("Command ticks must be ordered");
            }
            commands.push(command);
        }

        public function take(tick:int):TickCommand {
            if (cursor == commands.length || commands[cursor].tick > tick) return null;
            var command:TickCommand = commands[cursor++];
            if (cursor == commands.length) {
                cursor = 0;
                commands.length = 0;
            }
            return command;
        }
    }
}
