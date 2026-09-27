// Exports everything the Undertale VM port needs from data.win into
// $UT_GAME_DIR:
//   asm/<code entry>.asm   disassembled bytecode of every code entry
//   game.json              objects, sprites, backgrounds, rooms, fonts,
//                          scripts, sounds, room order, general info
//   masks/<sprite>_<n>.bin precise collision masks
//   backgrounds/<bg>.png   background images
//   fonts/<font>.png       font sheets
// Sprite frames themselves come from `UndertaleModCli dump --sprites`.
using System.Linq;
using System.Text.Json;
using UndertaleModLib.Util;

EnsureDataLoaded();
string outDir = Environment.GetEnvironmentVariable("UT_GAME_DIR");
foreach (var d in new[] { "asm", "masks", "backgrounds", "fonts" })
    Directory.CreateDirectory(Path.Combine(outDir, d));

string N(UndertaleNamedResource r) => r?.Name?.Content;

foreach (var code in Data.Code.Where(c => c.ParentEntry is null))
{
    string path = Path.Combine(outDir, "asm", code.Name.Content + ".asm");
    try { File.WriteAllText(path, code.Disassemble(Data.Variables, Data.CodeLocals?.For(code))); }
    catch (Exception e) { File.WriteAllText(path, "; FAILED " + e.Message); }
}

var objects = Data.GameObjects.Select((o, i) => new {
    index = i, name = N(o), sprite = N(o.Sprite), mask = N(o.TextureMaskId), parent = N(o.ParentId),
    depth = o.Depth, visible = o.Visible, solid = o.Solid, persistent = o.Persistent,
    events = o.Events.Select((list, type) => list.Select(e => new {
        type, subtype = e.EventSubtype,
        code = e.Actions.Count > 0 ? N(e.Actions[0].CodeId) : null }).ToList()).SelectMany(x => x).ToList()
}).ToList();

var sprites = Data.Sprites.Select((s, i) => new {
    index = i, name = N(s), width = s.Width, height = s.Height, ox = s.OriginX, oy = s.OriginY,
    left = s.MarginLeft, right = s.MarginRight, top = s.MarginTop, bottom = s.MarginBottom,
    sep = s.SepMasks.ToString(), frames = s.Textures.Count, masks = s.CollisionMasks.Count
}).ToList();
foreach (var s in Data.Sprites)
    for (int m = 0; m < s.CollisionMasks.Count; m++)
        if (s.SepMasks.ToString() == "Precise")
            File.WriteAllBytes(Path.Combine(outDir, "masks", $"{N(s)}_{m}.bin"), s.CollisionMasks[m].Data);

using (var worker = new TextureWorker())
{
    foreach (var b in Data.Backgrounds)
        if (b.Texture != null)
            worker.ExportAsPNG(b.Texture, Path.Combine(outDir, "backgrounds", N(b) + ".png"));
    foreach (var f in Data.Fonts)
        if (f.Texture != null)
            worker.ExportAsPNG(f.Texture, Path.Combine(outDir, "fonts", N(f) + ".png"));
}

var backgrounds = Data.Backgrounds.Select((b, i) => new {
    index = i, name = N(b), width = b.Texture?.BoundingWidth ?? 0, height = b.Texture?.BoundingHeight ?? 0,
    transparent = b.Transparent }).ToList();

var fonts = Data.Fonts.Select((f, i) => new {
    index = i, name = N(f), display = f.DisplayName?.Content, size = f.EmSize, first = f.RangeStart, last = f.RangeEnd,
    glyphs = f.Glyphs.Select(g => new { ch = g.Character, x = g.SourceX, y = g.SourceY, w = g.SourceWidth, h = g.SourceHeight, shift = g.Shift, offset = g.Offset }).ToList()
}).ToList();

var rooms = Data.Rooms.Select((r, i) => new {
    index = i, name = N(r), width = r.Width, height = r.Height, speed = r.Speed, persistent = r.Persistent,
    color = r.BackgroundColor, drawColor = r.DrawBackgroundColor, creation = N(r.CreationCodeId), views = r.Flags.ToString(),
    backgrounds = r.Backgrounds.Select(b => new { enabled = b.Enabled, fg = b.Foreground, bg = N(b.BackgroundDefinition),
        x = b.X, y = b.Y, htile = b.TiledHorizontally, vtile = b.TiledVertically, hspeed = b.SpeedX, vspeed = b.SpeedY, stretch = b.Stretch }).ToList(),
    viewList = r.Views.Select(v => new { enabled = v.Enabled, x = v.ViewX, y = v.ViewY, w = v.ViewWidth, h = v.ViewHeight,
        px = v.PortX, py = v.PortY, pw = v.PortWidth, ph = v.PortHeight, bx = v.BorderX, by = v.BorderY,
        sx = v.SpeedX, sy = v.SpeedY, follow = N(v.ObjectId) }).ToList(),
    instances = r.GameObjects.Select(o => new { obj = N(o.ObjectDefinition), x = o.X, y = o.Y, id = o.InstanceID,
        sx = o.ScaleX, sy = o.ScaleY, color = o.Color, angle = o.Rotation, creation = N(o.CreationCode) }).ToList(),
    tiles = r.Tiles.Select(t => new { bg = N(t.BackgroundDefinition), x = t.X, y = t.Y, sx = t.SourceX, sy = t.SourceY,
        w = t.Width, h = t.Height, depth = t.TileDepth, id = t.InstanceID, scx = t.ScaleX, scy = t.ScaleY, color = t.Color }).ToList()
}).ToList();

var game = new {
    name = Data.GeneralInfo.Name.Content,
    roomOrder = Data.GeneralInfo.RoomOrder.Select(r => N(r.Resource)).ToList(),
    objects, sprites, backgrounds, fonts, rooms,
    scripts = Data.Scripts.Select((s, i) => new { index = i, name = N(s), code = N(s.Code) }).ToList(),
    sounds = Data.Sounds.Select((s, i) => new { index = i, name = N(s), file = s.File?.Content }).ToList(),
    paths = Data.Paths.Select((p, i) => new { index = i, name = N(p), smooth = p.IsSmooth, closed = p.IsClosed, precision = p.Precision,
        points = p.Points.Select(pt => new { x = pt.X, y = pt.Y, speed = pt.Speed }).ToList() }).ToList(),
    timelines = Data.Timelines.Select((t, i) => new { index = i, name = N(t) }).ToList(),
    globalInit = Data.GlobalInitScripts?.Select(g => N(g.Code)).ToList(),
    functions = Data.Functions.Select(f => N(f)).ToList(),
};
File.WriteAllText(Path.Combine(outDir, "game.json"), JsonSerializer.Serialize(game));
File.WriteAllText(Path.Combine(outDir, "done"), "");
