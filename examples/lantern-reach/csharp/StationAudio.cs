using Godot;
using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public partial class StationAudio : Node {
    private List<AudioStreamPlayer> voices = new List<AudioStreamPlayer>();
    private RandomStream sequence;
    private Subscription subscription;
    private int nextBell;
    private int nextPulse;
    private int nextDrone;
    public double volume = 0.65;

    public void setup(Simulation game) {
        sequence = new RandomStream(game.random.state);
        List<string> names = new List<string> {"drone", "chime", "pulse", "swing", "harpoon", "fog-bell", "fire", "splash", "gutter" };
        foreach (string key in names) {
            AudioStreamPlayer voice = new AudioStreamPlayer();
            voice.Stream = ResourceLoader.Load("res://assets/audio/" + key + ".wav") as AudioStream;
            AddChild(voice);
            voices.Add(voice);
        }
        subscription = game.events.listen(receive);
    }

    public void silence() {
        foreach (AudioStreamPlayer voice in voices) voice.Stop();
    }

    public void close(EventBus bus) {
        bus.remove(subscription);
        silence();
        OS.DelayMsec(100);
    }

    private void sound(int index, double pitch, double gain) {
        AudioStreamPlayer voice = voices[index];
        voice.PitchScale = (float)pitch;
        voice.VolumeDb = (float)(-48 + 42 * volume + gain);
        if (volume > 0) voice.Play();
    }

    private void receive(WorldEvent item) {
        if (item.kind == WorldEvent.SWING) sound(3, 1, -4);
        if (item.kind == WorldEvent.FIRED) {
            if (item.value == Structure.HARPOON) sound(4, 1, -8);
            if (item.value == Structure.BELL) sound(5, 0.68, -5);
            if (item.value == Structure.BRAZIER) sound(6, 1, -10);
        }
        if (item.kind == WorldEvent.BEACON_LOST) sound(8, 1, 0);
        if (item.kind == WorldEvent.WAVE_START) sound(7, 0.8, -5);
    }

    public void present(Simulation game) {
        if (game.tick >= nextDrone) {
            sound(0, 1, -6);
            nextDrone = game.tick + 240;
        }
        if (game.tick >= nextPulse) {
            sound(2, 1, -12);
            nextPulse = game.tick + Math.Max(20, 120 - (int)(game.pool.count / 8));
        }
        if (game.tick >= nextBell) {
            int light = game.light.level[game.grid.cell(game.keeper.x, game.keeper.y)];
            List<double> notes = new List<double> {1, 1.122462, 1.259921, 1.498307, 1.681793 };
            int note = ((int)(light / 55) + sequence.below(2)) % 5;
            sound(1, notes[note], -8);
            nextBell = game.tick + 180 + sequence.below(240);
        }
    }
}
