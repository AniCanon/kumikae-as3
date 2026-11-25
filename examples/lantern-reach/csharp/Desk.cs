using Godot;
using System;
using System.Collections.Generic;

namespace Kumikae.LanternReach;

public partial class Desk : CanvasLayer {
    public Strings words = new Strings();
    public int selected = -1;
    private Control page;
    private Label titleLabel;
    private Label entry;
    private TextureRect art;
    private Label status;
    private Label notice;
    private List<Button> buttons = new List<Button>();
    private List<Button> tools = new List<Button>();
    private Plate cards;
    private EventBus bus;
    private Subscription listener;
    private int wave;
    private int noticeUntil;
    private int shownTick;

    public void setup(Simulation game) {
        PackedScene scene = ResourceLoader.Load("res://scenes/overlay.tscn") as PackedScene;
        Control ui = scene.Instantiate() as Control;
        AddChild(ui);
        page = ui.GetNode("Page") as Control;
        titleLabel = ui.GetNode("Page/Heading") as Label;
        entry = ui.GetNode("Page/Entry") as Label;
        art = ui.GetNode("Page/Art") as TextureRect;
        status = ui.GetNode("Status") as Label;
        notice = ui.GetNode("Notice") as Label;
        cards = new Plate("cards", 2, 4);
        for (int i = 0; i < 6; i++) {
            Button choice = ui.GetNode("Page/Choice" + i) as Button;
            buttons.Add(choice);
            bindChoice(choice, i);
        }
        for (int j = 0; j < 5; j++) {
            Button tool = ui.GetNode("Tools/Slot" + j) as Button;
            tools.Add(tool);
            bindChoice(tool, j);
        }
        bus = game.events;
        listener = bus.listen(receive);
    }

    private void bindChoice(Button button, int index) {
        button.Pressed += () => selected = index;
    }

    public void close() {
        if (bus != null) bus.remove(listener);
    }

    private void receive(WorldEvent item) {
        if (item.kind == WorldEvent.WAVE_START) wave = item.value;
        if (item.kind == WorldEvent.REFUSED) {
            notice.Text = words.get("refused");
            noticeUntil = shownTick + 150;
        }
    }

    public void hud(Simulation game, int night, bool day) {
        shownTick = game.tick;
        if (shownTick >= noticeUntil) notice.Text = "";
        status.Text = words.get(day ? "day" : "night") + " " + night + "     " + words.get("beacons") +
            " " + game.grid.beacons.Count + "     " + words.get("salvage") + " " + game.keeper.salvage +
            "     " + words.get("level") + " " + game.keeper.level + "     " + words.get("xp") +
            " " + game.keeper.xp + "     " + words.get("wave") + " " + wave;
    }

    public void sheet(string heading, string body, string imageKey, int frameIndex) {
        page.Visible = true;
        status.Visible = false;
        entry.RemoveThemeFontOverride("font");
        titleLabel.Text = heading;
        entry.Text = body;
        if (imageKey == "cards") {
            AtlasTexture pic = new AtlasTexture();
            pic.Atlas = cards.ink;
            int width = (int)(cards.ink.GetWidth() / 2);
            int height = (int)(cards.ink.GetHeight() / 4);
            pic.Region = new Rect2(frameIndex % 2 * width, (int)(frameIndex / 2) * height, width, height);
            art.Texture = pic;
        } else {
            art.Texture = ResourceLoader.Load("res://assets/art/" + imageKey + ".png") as Texture2D;
        }
        foreach (Button tool in tools) tool.Visible = false;
        foreach (Button choice in buttons) choice.Visible = false;
    }

    public void field() {
        page.Visible = false;
        status.Visible = true;
        foreach (Button tool in tools) tool.Visible = false;
    }

    public void option(int slot, string caption) {
        Button button = page.Visible ? buttons[slot] : tools[slot];
        button.Icon = null;
        button.Text = caption;
        button.Visible = true;
        if (page.Visible && slot == 0) button.GrabFocus();
    }

    public void handwriting(int keeper) {
        FontVariation face = new FontVariation();
        face.BaseFont = entry.GetThemeDefaultFont();
        face.VariationEmbolden = keeper % 2 == 0 ? 0 : 0.25f;
        face.VariationTransform = new Transform2D(new Vector2(1, 0), new Vector2(keeper % 2 == 0 ? -0.15f : 0.08f, 1), new Vector2());
        entry.AddThemeFontOverride("font", face);
    }

    public void stamp(int slot, int id) {
        Texture2D ink = ResourceLoader.Load("res://assets/art/stamps.png") as Texture2D;
        AtlasTexture image = new AtlasTexture();
        image.Atlas = ink;
        int side = (int)(ink.GetWidth() / 5);
        image.Region = new Rect2(id % 5 * side, (int)(id / 5) * side, side, side);
        buttons[slot].Icon = image;
        buttons[slot].AddThemeConstantOverride("icon_max_width", 28);
    }
}
