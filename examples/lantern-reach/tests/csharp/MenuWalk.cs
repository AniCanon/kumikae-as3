using System;
using Godot;

namespace Kumikae.LanternReach.Tests;

public partial class MenuWalk : Node {
    private readonly Strings words = new();
    private Control overlay;
    private int step;
    private int frames;
    private readonly (string heading, string path, string caption)[] actions = {
        ("title", "Page/Choice4", "credits"), ("credits", "Page/Choice0", "back"),
        ("title", "Page/Choice1", "log"), ("log", "Page/Choice0", "back"),
        ("title", "Page/Choice2", "settings"), ("settings", "Page/Choice3", "back"),
        ("title", "Page/Choice0", "start"), ("open", "Page/Choice0", "next"),
        ("card", "Page/Choice0", "keep"), ("", "Tools/Slot0", "tools"),
        ("tool", "Page/Choice0", "lamp"), ("", "Tools/Slot1", "watch"),
        ("", "Tools/Slot2", "pause"), ("paused", "Page/Choice0", "resume")
    };

    public override void _Ready() {
        ProjectSettings.SetSetting("lantern/register", "res://menu-test.json");
        var scene = GD.Load<PackedScene>("res://main.tscn");
        var main = scene.Instantiate();
        AddChild(main);
        overlay = (Control)main.FindChild("Overlay", true, false);
    }

    public override void _Process(double delta) {
        if (++frames % 2 != 0) return;
        try {
            if (step == actions.Length) {
                if (overlay.GetNode<Control>("Page").Visible) throw new InvalidOperationException("Pause did not resume");
                GD.Print("PASS menu walk");
                GetTree().Quit();
                return;
            }
            var (heading, path, caption) = actions[step++];
            var page = overlay.GetNode<Control>("Page");
            var title = overlay.GetNode<Label>("Page/Heading").Text;
            var button = overlay.GetNode<Button>(path);
            if ((heading != "") != page.Visible || (page.Visible && !title.StartsWith(words.get(heading)))
                    || !button.IsVisibleInTree() || !button.Text.StartsWith(words.get(caption)))
                throw new InvalidOperationException($"Menu step {step} at {title} with {button.Text}");
            button.EmitSignal(BaseButton.SignalName.Pressed);
        } catch (Exception error) {
            GD.PushError(error.ToString());
            GetTree().Quit(1);
        }
    }
}
