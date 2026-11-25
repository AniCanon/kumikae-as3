using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public sealed class CommandQueue {
    private List<TickCommand> commands = new List<TickCommand>();
    private int cursor;

    public void enqueue(TickCommand command) {
        if (commands.Count > cursor && commands[commands.Count - 1].tick > command.tick) {
            throw new InvalidOperationException("Command ticks must be ordered");
        }
        commands.Add(command);
    }

    public TickCommand take(int tick) {
        if (cursor == commands.Count || commands[cursor].tick > tick) return null;
        TickCommand command = commands[cursor++];
        if (cursor == commands.Count) {
            cursor = 0;
            commands.Clear();
        }
        return command;
    }
}
