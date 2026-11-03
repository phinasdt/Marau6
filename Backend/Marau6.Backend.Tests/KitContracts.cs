using Marau6;
using System.Security.Cryptography;
using System.Text.Json;
static class KitContracts
{
    static void Check(bool value,string name){if(!value)throw new Exception(name);}
    static string Hash(object command)=>Convert.ToHexString(SHA256.HashData(JsonSerializer.SerializeToUtf8Bytes(command,Store.Json)));
    static List<(Guid,int)> Layout(Profile p)=>p.Items.Select(x=>(x.Id,x.Slot)).OrderBy(x=>x.Id).ToList();
    static bool Unique(Profile p)=>p.Items.Select(x=>x.Slot).Distinct().Count()==p.Items.Count;
    static List<Item> Copies(List<Item> items,string custody)=>items.Select(x=>{var c=x.Copy();c.Custody=custody;return c;}).ToList();
    static Item Gone(Item i,string custody){i.Owner="";i.Slot=-1;i.Custody=custody;return i;}
    static (State s,Profile p,Guid raid,List<Item> kit) Basic(Action<State,Profile>? before=null)
    {
        var s=new State();var p=Profiles.Get(s,1);before?.Invoke(s,p);
        var raid=Guid.NewGuid();RaidService.Open(s,new RaidCommand(Guid.NewGuid(),raid));
        var entered=RaidService.Enter(s,new EnterCommand(Guid.NewGuid(),raid,1,p.Revision,"basic"));
        return (s,p,raid,(List<Item>)entered.GetType().GetProperty("items")!.GetValue(entered)!);
    }
    static Item Place(State s,Profile p,int kind,int slot)
    {
        var item=p.Items.First(x=>x.Kind==kind && x.Slot>=100);Inventory.Move(s,new MoveCommand(Guid.NewGuid(),1,p.Revision,item.Id,slot));return item;
    }
    public static void Run()
    {
        // BASIC entry leaves the base untouched and mints rifle, two ammo boxes and a medkit.
        var s=new State();var p=Profiles.Get(s,1);var before=Layout(p);var revision=p.Revision;
        var raid=Guid.NewGuid();RaidService.Open(s,new RaidCommand(Guid.NewGuid(),raid));
        var entered=RaidService.Enter(s,new EnterCommand(Guid.NewGuid(),raid,1,p.Revision,"basic"));
        var kit=(List<Item>)entered.GetType().GetProperty("items")!.GetValue(entered)!;
        var r=s.Raids.Single();var owner=p.Id.ToString();
        Check(Layout(p).SequenceEqual(before) && p.Revision==revision+1,"Basic entry must keep profile items and slots");
        Check(kit.Count==4 && kit.Select(x=>(x.Kind,x.Slot)).SequenceEqual([(0,10),(3,0),(3,1),(1,2)]),"Kit must be rifle, ammo, ammo, medkit at 10/0/1/2");
        Check(kit[0].Magazine==20 && kit[0].Reserve==40 && kit[0].Durability==10000,"Kit rifle must be a fresh 20/40 rifle");
        Check(kit.Skip(1).All(x=>x.Magazine==0 && x.Reserve==0 && x.Durability==null),"Kit ammo and medkit carry no ammo or durability");
        Check(kit.All(x=>x.Owner==owner && x.Custody=="inventory" && x.Id!=Guid.Empty && !p.Items.Any(y=>y.Id==x.Id)) && kit.Select(x=>x.Id).Distinct().Count()==4,"Kit items must be new owned identities");
        Check(r.Items.Select(x=>x.Id).SequenceEqual(kit.Select(x=>x.Id)) && r.Participants.Single().Kit=="basic","Kit must be reserved in the raid and participant basic");
        // Invalid kit is rejected without side effects; null kit is today's entry.
        var p2=Profiles.Get(s,2);
        try{RaidService.Enter(s,new EnterCommand(Guid.NewGuid(),raid,2,p2.Revision,"deluxe"));throw new Exception("Invalid kit accepted");}catch(Conflict){}
        Check(r.Participants.Count==1 && p2.Revision==0,"Invalid kit must not change state");
        var equipment=p2.Items.Where(x=>x.Slot<100).Select(x=>x.Id).ToList();
        RaidService.Enter(s,new EnterCommand(Guid.NewGuid(),raid,2,p2.Revision));
        Check(p2.Items.All(x=>x.Slot>=100) && equipment.All(id=>r.Items.Any(x=>x.Id==id)) && r.Participants.Single(x=>x.Profile==2).Kit=="custom","Null kit must reserve profile equipment");
        Check(JsonSerializer.Deserialize<Participation>("{\"profile\":1,\"outcome\":\"active\"}",Store.Json)!.Kit=="custom","Legacy participant must read as custom");
        // Kit ammo box pays the rifle reserve, the medkit is consumable, survivors extract.
        (s,p,raid,kit)=Basic();
        var live=Copies(kit,"inventory");live[0].Reserve=60;Gone(live[1],"consumed");
        RaidService.Snapshot(s,new SnapshotCommand(Guid.NewGuid(),raid,1,live,[]));
        live=Copies(live,"");live.ForEach(x=>x.Custody=x.Owner==""?"consumed":"inventory");Gone(live[3],"consumed");
        RaidService.Snapshot(s,new SnapshotCommand(Guid.NewGuid(),raid,2,live,[]));
        Check(s.Raids.Single().Items.Count(x=>x.Custody=="consumed")==2,"Kit ammo box and medkit consumption accepted");
        live.Where(x=>x.Custody=="inventory").ToList().ForEach(x=>x.Custody="result");
        RaidService.Snapshot(s,new SnapshotCommand(Guid.NewGuid(),raid,3,live,[new Result(1,"extracted")]));
        Check(p.Items.Single(x=>x.Id==kit[0].Id).Reserve==60 && p.Items.Any(x=>x.Id==kit[2].Id) && !p.Items.Any(x=>x.Id==kit[1].Id||x.Id==kit[3].Id) && Unique(p),"Survivors extract and consumed kit stays gone");
        // Extraction while the profile owns slots 10, 0 and 2 remaps colliding kit items to the stash.
        (s,p,raid,kit)=Basic((st,pr)=>{Place(st,pr,1,0);Place(st,pr,2,2);});
        before=Layout(p);
        RaidService.Snapshot(s,new SnapshotCommand(Guid.NewGuid(),raid,1,Copies(kit,"result"),[new Result(1,"extracted")]));
        var slots=kit.Select(k=>p.Items.Single(x=>x.Id==k.Id).Slot).ToList();
        Check(before.All(x=>p.Items.Any(y=>y.Id==x.Item1 && y.Slot==x.Item2)) && Unique(p) && slots[0]>=100 && slots[1]>=100 && slots[2]==1 && slots[3]>=100 && p.Items.All(x=>x.Custody=="inventory"),"Extracted kit must take free slots without moving base items");
        // Death with looted safe item while the profile's slot 8 is occupied.
        Item supply=null!;(s,p,raid,kit)=Basic((st,pr)=>supply=Place(st,pr,1,8));
        var loot=new Item{Id=Guid.NewGuid(),Kind=1,Slot=-1,Custody="world"};
        RaidService.Snapshot(s,new SnapshotCommand(Guid.NewGuid(),raid,1,[..Copies(kit,"inventory"),loot],[]));
        var held=loot.Copy();held.Owner=p.Id.ToString();held.Slot=8;held.Custody="result";
        RaidService.Snapshot(s,new SnapshotCommand(Guid.NewGuid(),raid,2,[..Copies(kit,"").Select(x=>Gone(x,"world")),held],[new Result(1,"eliminated")]));
        Check(supply.Slot==8 && p.Items.Single(x=>x.Id==loot.Id).Slot is >=100 and <132 && !p.Items.Any(x=>kit.Any(k=>k.Id==x.Id)) && Unique(p),"Death safe loot must remap to stash");
        // Abandonment of a basic participant remaps the same way.
        (s,p,raid,kit)=Basic((st,pr)=>supply=Place(st,pr,1,8));
        loot=new Item{Id=Guid.NewGuid(),Kind=1,Slot=-1,Custody="world"};
        RaidService.Snapshot(s,new SnapshotCommand(Guid.NewGuid(),raid,1,[..Copies(kit,"inventory"),loot],[]));
        held=loot.Copy();held.Owner=p.Id.ToString();held.Slot=8;held.Custody="inventory";
        RaidService.Snapshot(s,new SnapshotCommand(Guid.NewGuid(),raid,2,[..Copies(kit,"inventory"),held],[]));
        RaidService.Close(s,new RaidCommand(Guid.NewGuid(),raid));
        Check(supply.Slot==8 && p.Items.Single(x=>x.Id==loot.Id).Slot is >=100 and <132 && s.Raids.Single().Participants.Single().Outcome=="abandoned" && Unique(p),"Abandoned safe loot must remap to stash");
        // A full stash loses the colliding rifle instead of failing the raid path; free backpack slots still take the rest.
        (s,p,raid,kit)=Basic((st,pr)=>{foreach(var slot in Enumerable.Range(100,32).Where(x=>!pr.Items.Any(i=>i.Slot==x)).ToList())pr.Items.Add(new Item{Id=Guid.NewGuid(),Kind=2,Slot=slot,Owner=pr.Id.ToString()});});
        var count=p.Items.Count;revision=p.Revision;
        RaidService.Snapshot(s,new SnapshotCommand(Guid.NewGuid(),raid,1,Copies(kit,"result"),[new Result(1,"extracted")]));
        Check(p.Items.Count==count+3 && !p.Items.Any(x=>x.Id==kit[0].Id) && p.Revision==revision+1 && Unique(p) && s.Raids.Single().Participants.Single().Outcome=="extracted","Full stash must drop the item without throwing");
        // Legacy enter body keeps its receipt hash; an explicit kit is part of the content.
        var legacy=new{operationId=Guid.NewGuid(),raidId=Guid.NewGuid(),profile=1,revision=3};
        var parsed=JsonSerializer.Deserialize<EnterCommand>(JsonSerializer.Serialize(legacy,Store.Json),Store.Json)!;
        Check(parsed.Kit==null && Hash(new{action="enter",data=legacy})==Hash(new{action="enter",data=parsed}),"Legacy enter hash must be unchanged");
        var withKit=JsonSerializer.Deserialize<EnterCommand>(JsonSerializer.Serialize(new{legacy.operationId,legacy.raidId,legacy.profile,legacy.revision,kit="basic"},Store.Json),Store.Json)!;
        Check(withKit.Kit=="basic" && Hash(new{action="enter",data=withKit})!=Hash(new{action="enter",data=parsed}),"Kit must change the receipt hash");
        Console.WriteLine("PASS basic kit entry, kit consumption, return remapping, full stash and legacy enter hash");
    }
}
