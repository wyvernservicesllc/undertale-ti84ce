// Writes every sound embedded in data.win (the .wav effects) to
// $UT_SOUND_DIR/<file name>, then a "done" marker. Music is outside
// data.win already (mus_*.ogg next to it).
using System;
using System.IO;

EnsureDataLoaded();

string dir = Environment.GetEnvironmentVariable("UT_SOUND_DIR");
if (dir is null)
{
    return;
}
Directory.CreateDirectory(dir);
int n = 0;
foreach (UndertaleSound sound in Data.Sounds)
{
    byte[] data = null;
    if (sound.AudioFile is not null)
    {
        data = sound.AudioFile.Data;
    }
    else if (sound.AudioID >= 0 && sound.AudioID < Data.EmbeddedAudio.Count && sound.GroupID == 0)
    {
        data = Data.EmbeddedAudio[sound.AudioID].Data;
    }
    if (data is not null)
    {
        File.WriteAllBytes(Path.Combine(dir, sound.File.Content), data);
        n++;
    }
}
File.WriteAllText(Path.Combine(dir, "done"), n.ToString());
