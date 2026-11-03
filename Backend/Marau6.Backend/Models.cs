using System.Text.Json.Serialization;
namespace Marau6;

public sealed class Conflict(string message) : Exception(message);
public sealed class Item
{
    public Guid Id { get; set; }
    public int Kind { get; set; }
    public int Slot { get; set; }
    public int Magazine { get; set; }
    public int Reserve { get; set; }
    public string Owner { get; set; } = "";
    public string Custody { get; set; } = "inventory";
    // Omit absent legacy fields: existing command receipt hashes must remain valid.
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public int? Durability { get; set; }
    [JsonIgnore] public int EffectiveDurability => Durability ?? 10000;
    public Item Copy() => (Item)MemberwiseClone();
}
public sealed class Profile
{
    public Guid Id { get; set; }
    public int Number { get; set; }
    public int Revision { get; set; }
    public List<Item> Items { get; set; } = [];
}
public sealed class Participation
{
    public int Profile { get; set; }
    public string Outcome { get; set; } = "active";
    public string Kit { get; set; } = "custom";
}
public sealed class Raid
{
    public Guid Id { get; set; }
    public long Sequence { get; set; }
    public bool Closed { get; set; }
    public List<Item> Items { get; set; } = [];
    public List<Participation> Participants { get; set; } = [];
}
public sealed class State
{
    public List<Profile> Profiles { get; set; } = [];
    public HashSet<Guid> ConsumedItems { get; set; } = [];
    public List<Raid> Raids { get; set; } = [];
}
public record MoveCommand(Guid OperationId, int Profile, int Revision, Guid Item, int Destination);
public record RaidCommand(Guid OperationId, Guid RaidId);
// Kit absent on legacy bodies: omitted when null so their receipt hashes are unchanged.
public record EnterCommand(Guid OperationId, Guid RaidId, int Profile, int Revision, [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] string? Kit = null);
public record Result(int Profile, string Outcome);
public record SnapshotCommand(Guid OperationId, Guid RaidId, long Sequence, List<Item> Items, List<Result> Results);
