// Writes what the overworld port needs from data.win into $UT_ROOM_DIR:
// room layouts as JSON, room backgrounds as PNG, and collision masks.
using System.Linq;
using System.Text;
using UndertaleModLib.Util;

EnsureDataLoaded();
string outDir = Environment.GetEnvironmentVariable("UT_ROOM_DIR");
Directory.CreateDirectory(outDir);

string[] rooms = { "room_area1" };
string[] masks = { "spr_sur", "spr_sul", "spr_sdr", "spr_sdl" };

using (var worker = new TextureWorker())
{
    foreach (var name in rooms)
    {
        var r = Data.Rooms.ByName(name);
        var sb = new StringBuilder();
        sb.Append($"{{\"name\":\"{name}\",\"width\":{r.Width},\"height\":{r.Height},\"backgrounds\":[");
        sb.Append(string.Join(",", r.Backgrounds.Where(b => b.Enabled && b.BackgroundDefinition != null)
            .Select(b => $"{{\"name\":\"{b.BackgroundDefinition.Name.Content}\",\"x\":{b.X},\"y\":{b.Y}}}")));
        sb.Append("],\"objects\":[");
        sb.Append(string.Join(",", r.GameObjects.Where(o => o.ObjectDefinition != null)
            .Select(o => $"{{\"object\":\"{o.ObjectDefinition.Name.Content}\",\"x\":{o.X},\"y\":{o.Y}}}")));
        sb.Append("]}");
        File.WriteAllText(Path.Combine(outDir, name + ".json"), sb.ToString());
        foreach (var b in r.Backgrounds.Where(b => b.Enabled && b.BackgroundDefinition != null))
        {
            var bg = b.BackgroundDefinition;
            worker.ExportAsPNG(bg.Texture, Path.Combine(outDir, bg.Name.Content + ".png"));
        }
    }
}
foreach (var name in masks)
{
    // 1 bit per pixel, rows padded to whole bytes, leftmost pixel in the MSB.
    File.WriteAllBytes(Path.Combine(outDir, name + "_mask.bin"), Data.Sprites.ByName(name).CollisionMasks[0].Data);
}
File.WriteAllText(Path.Combine(outDir, "done"), "");
