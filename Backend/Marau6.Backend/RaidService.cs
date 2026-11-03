namespace Marau6;

public static class RaidService
{
    static Raid Raid(State s, Guid id) => s.Raids.SingleOrDefault(x => x.Id == id) ?? throw new Conflict("Partida desconhecida");
    static void Require(bool valid, string message) { if (!valid) throw new Conflict(message); }
    public static object Open(State s, RaidCommand c)
    {
        Require(c.RaidId != Guid.Empty && !s.Raids.Any(x => x.Id == c.RaidId), "Partida já existente");
        Require(!s.Raids.Any(x => !x.Closed), "Recupere a partida anterior antes de abrir outra");
        var r = new Raid { Id = c.RaidId }; s.Raids.Add(r); return r;
    }
    public static object Enter(State s, EnterCommand c)
    {
        var p = Profiles.Get(s, c.Profile); var r = Raid(s, c.RaidId);
        Require(!r.Closed && p.Revision == c.Revision, "Partida fechada ou versão antiga");
        Require(!r.Participants.Any(x => x.Profile == c.Profile), "Uma entrada por rodada");
        Require(!s.Raids.Any(x => !x.Closed && x.Participants.Any(y => y.Profile == c.Profile && y.Outcome == "active")), "Perfil já está em uma partida");
        Require(c.Kit is null or "custom" or "basic", "Kit inválido");
        var basic = c.Kit == "basic";
        // BASIC kit: the profile keeps its gear; rifle, two ammo boxes and a medkit are minted as reserved equipment.
        var equipment = basic ? BasicKit(p) : p.Items.Where(x => x.Slot < 100).ToList();
        foreach (var i in equipment) { p.Items.Remove(i); var live=i.Copy(); live.Custody="inventory"; r.Items.Add(live); }
        r.Participants.Add(new Participation { Profile = c.Profile, Kit = basic ? "basic" : "custom" }); p.Revision++;
        return new { profile = p, items = equipment };
    }
    public static object Snapshot(State s, SnapshotCommand c)
    {
        var r = Raid(s, c.RaidId);
        Require(!r.Closed && c.Sequence == r.Sequence + 1, "Sequência inválida");
        Require(c.Items.Count <= 4096 && c.Items.Select(x => x.Id).Distinct().Count() == c.Items.Count, "Identidades inválidas");
        var ids = c.Items.ToDictionary(x => x.Id);
        foreach (var old in r.Items)
        {
            Require(ids.TryGetValue(old.Id, out var next), "Snapshot não pode apagar identidades");
            Require(next!.Kind == old.Kind, "Tipo de item imutável");
            if(old.Kind==0) Require(next.EffectiveDurability <= old.EffectiveDurability, "Durabilidade não pode aumentar");
            if (old.Custody is "result" or "consumed") Require(Same(old, next), "Resultado ou consumo confirmado é imutável");
        }
        var activeOwners = r.Participants.Where(x => x.Outcome == "active").Select(x => Profiles.Get(s, x.Profile).Id.ToString()).ToHashSet();
        var known = r.Items.Select(x => x.Id).ToHashSet();
        var finalOwners = r.Participants.Where(x => x.Outcome != "active").Select(x => Profiles.Get(s, x.Profile).Id.ToString()).ToHashSet();
        foreach (var i in c.Items)
        {
            Require(i.Id != Guid.Empty && i.Kind is >= 0 and <= 3 && i.Magazine is >= 0 and <= 20 && i.Reserve is >= 0 and <= 120, "Dados de item inválidos");
            Require(i.Kind == 0 || i.Magazine == 0 && i.Reserve == 0, "Munição em item incompatível");
            Require(i.Kind==0 ? i.EffectiveDurability is >=0 and <=10000 : i.Durability==null, "Durabilidade incompatível");
            Require(i.Custody is "world" or "recovery" or "inventory" or "result" or "consumed", "Custódia inválida");
            if(i.Custody=="consumed")
            {
                var previous=r.Items.SingleOrDefault(x=>x.Id==i.Id);
                Require(i.Kind is 1 or 3 && previous!=null &&
                    (previous.Custody=="consumed" && Same(previous,i) ||
                     previous.Custody=="inventory" && activeOwners.Contains(previous.Owner) && previous.Slot is >=0 and <10),
                    "Consumo exige suprimento ou munição de inventário ativo");
            }
            if (!known.Contains(i.Id))
            {
                Require(i.Kind!=0 || i.EffectiveDurability==10000, "Rifle novo deve nascer íntegro");
                Require(i.Owner == "" && i.Custody is "world" or "recovery", "Novo loot deve nascer no mundo");
                Require(!s.ConsumedItems.Contains(i.Id) && !s.Profiles.Any(p => p.Items.Any(x => x.Id == i.Id)) && !s.Raids.Where(x => x.Id != r.Id).Any(x => x.Items.Any(y => y.Id == i.Id)), "Identidade já registrada");
            }
            if (i.Custody is "inventory" or "result")
                Require((activeOwners.Contains(i.Owner) || i.Custody == "result" && finalOwners.Contains(i.Owner)) && Inventory.Accepts(i.Slot, i.Kind) && i.Slot < 100, "Dono/slot inválido");
            else Require(i.Owner == "" && i.Slot == -1, "Loot no mundo não tem dono nem slot");
            if(i.Custody == "result" && finalOwners.Contains(i.Owner)) Require(r.Items.Any(old => old.Id==i.Id && old.Custody=="result" && Same(old,i)), "Resultado nao pode receber novos itens apos finalizacao");
        }
        foreach(var owner in activeOwners)
        {
            int spent=r.Items.Count(old=>old.Kind==3 && old.Custody=="inventory" && old.Owner==owner && ids[old.Id].Custody=="consumed");
            int gained=r.Items.Where(old=>old.Kind==0 && old.Custody=="inventory" && old.Owner==owner)
                .Sum(old=>Math.Max(0,ids[old.Id].Reserve-old.Reserve));
            Require(gained==spent*20,"Aumento de reserva exige consumo correspondente de munição");
        }
        Require(c.Items.Where(x => x.Owner != "").Select(x => (x.Owner,x.Slot)).Distinct().Count() == c.Items.Count(x => x.Owner != ""), "Slot duplicado");
        Require(c.Results.Select(x => x.Profile).Distinct().Count() == c.Results.Count, "Resultados repetidos");
        foreach (var result in c.Results)
        {
            var participant = r.Participants.SingleOrDefault(x => x.Profile == result.Profile) ?? throw new Conflict("Resultado sem participante");
            Require(result.Outcome is "extracted" or "eliminated", "Resultado inválido");
            Require(participant.Outcome == "active", "Resultado já confirmado");
            var p = Profiles.Get(s, result.Profile);
            var kept = c.Items.Where(x => x.Owner == p.Id.ToString()).ToList();
            Require(kept.All(x => x.Custody == "result" && (result.Outcome == "extracted" || x.Slot is 8 or 9)), "Itens mantidos incompatíveis com resultado");
            foreach (var x in kept) Return(p, x, participant.Kit == "basic");
            p.Revision++; participant.Outcome = result.Outcome;
        }
        foreach (var i in c.Items.Where(x => x.Custody == "result" && activeOwners.Contains(x.Owner)))
            Require(c.Results.Any(x => Profiles.Get(s,x.Profile).Id.ToString() == i.Owner), "Item de resultado sem finalização");
        r.Items = c.Items.Select(x => x.Copy()).ToList(); r.Sequence = c.Sequence;
        return new { sequence = r.Sequence };
    }
    static bool Same(Item a, Item b) => a.Id == b.Id && a.Kind == b.Kind && a.Slot == b.Slot && a.Magazine == b.Magazine && a.Reserve == b.Reserve && a.Owner == b.Owner && a.Custody == b.Custody && (a.Kind==0 ? a.EffectiveDurability==b.EffectiveDurability : a.Durability==b.Durability);
    public static object Close(State s, RaidCommand c)
    {
        var r = Raid(s, c.RaidId);
        if (r.Closed) return r;
        foreach (var active in r.Participants.Where(x => x.Outcome == "active"))
        {
            var p = Profiles.Get(s, active.Profile);
            foreach (var i in r.Items.Where(x => x.Owner == p.Id.ToString() && x.Custody == "inventory" && x.Slot is 8 or 9))
                Return(p, i, active.Kit == "basic");
            p.Revision++; active.Outcome = "abandoned";
        }
        r.Closed = true; return r;
    }
    static List<Item> BasicKit(Profile p) =>
    [
        new Item { Id = Guid.NewGuid(), Kind = 0, Slot = 10, Magazine = 20, Reserve = 40, Durability = 10000, Owner = p.Id.ToString() },
        new Item { Id = Guid.NewGuid(), Kind = 3, Slot = 0, Owner = p.Id.ToString() },
        new Item { Id = Guid.NewGuid(), Kind = 3, Slot = 1, Owner = p.Id.ToString() },
        new Item { Id = Guid.NewGuid(), Kind = 1, Slot = 2, Owner = p.Id.ToString() },
    ];
    // A BASIC participant still owns its base gear: a colliding slot moves to the first free stash slot,
    // and with a full stash the item is lost (never throw on a raid path; the unique slot index would 503).
    static void Return(Profile p, Item i, bool remap)
    {
        var returned = i.Copy(); returned.Custody = "inventory";
        bool Free(int slot) => Inventory.Accepts(slot, returned.Kind) && !p.Items.Any(x => x.Slot == slot);
        if (remap && !Free(returned.Slot))
        {
            var slot = Enumerable.Range(100, 32).Where(Free).DefaultIfEmpty(-1).First();
            if (slot < 0) return;
            returned.Slot = slot;
        }
        p.Items.Add(returned);
    }
    public static object ReadRaid(State s, Guid id) => Raid(s,id);
}
