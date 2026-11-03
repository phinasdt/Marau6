using System.Net;
using System.Text.Json;
using System.Security.Cryptography;
using Npgsql;
using Marau6;
using System.Net.Http.Json;
using System.Text.Json.Nodes;

// Integration contract against a real API/PostgreSQL instance. Profiles are fixed;
// tests use an isolated database configured by the test launcher, never the play database.
AmmoContracts.Run();
KitContracts.Run();
var client = new HttpClient { BaseAddress = new Uri("http://127.0.0.1:5187") };
client.DefaultRequestHeaders.Add("X-Marau6-Key", Environment.GetEnvironmentVariable("MARAU6_API_KEY") ?? "missing");
int passed = 0;
void Check(bool value, string name) { if (!value) throw new Exception(name); passed++; Console.WriteLine("PASS " + name); }
async Task<JsonObject> Get(int p) => (await client.GetFromJsonAsync<JsonObject>($"/state/{p}"))!;
async Task<JsonObject> Post(string path, object body, HttpStatusCode expected = HttpStatusCode.OK)
{
    var r = await client.PostAsJsonAsync(path, body);
    if (r.StatusCode != expected) throw new Exception($"{path}: {r.StatusCode} {await r.Content.ReadAsStringAsync()}");
    return (await r.Content.ReadFromJsonAsync<JsonObject>())!;
}
string Id(JsonNode n) => n["id"]!.GetValue<string>();
int Rev(JsonObject n) => n["revision"]!.GetValue<int>();
JsonArray Items(JsonObject n) => n["items"]!.AsArray();
// A receipt produced by the PRE-durability wire format, not the current Item serializer.
var legacyBody=new {operationId=Guid.NewGuid(),raidId=Guid.NewGuid(),sequence=1L,items=new[]{new{id=Guid.NewGuid(),kind=0,slot=-1,magazine=10,reserve=30,owner="",custody="world"}},results=Array.Empty<object>()};
var legacyJson=JsonSerializer.Serialize(legacyBody,Store.Json);
var oldHash=Convert.ToHexString(SHA256.HashData(JsonSerializer.SerializeToUtf8Bytes(new{action="snapshot",data=legacyBody},Store.Json)));
var parsedLegacy=JsonSerializer.Deserialize<SnapshotCommand>(legacyJson,Store.Json)!;
Check(oldHash==Convert.ToHexString(SHA256.HashData(JsonSerializer.SerializeToUtf8Bytes(new{action="snapshot",data=parsedLegacy},Store.Json))),"old snapshot byte hash remains identical");
var isolated=Environment.GetEnvironmentVariable("MARAU6_DB")!;
if(!isolated.Contains("Search Path=test_"))throw new Exception("Legacy fixture requires isolated test schema");
await using(var connection=new NpgsqlConnection(isolated))
{
    await connection.OpenAsync();await using var seed=new NpgsqlCommand("INSERT INTO marau6_receipts VALUES($1,$2,$3::jsonb)",connection);
    seed.Parameters.AddWithValue(legacyBody.operationId);seed.Parameters.AddWithValue(oldHash);seed.Parameters.AddWithValue("{\"sequence\":123}");await seed.ExecuteNonQueryAsync();
}
var legacyReceipt=await Post("/raids/snapshot",legacyBody);Check(legacyReceipt["sequence"]!.GetValue<int>()==123,"actual legacy receipt replay accepted without running unknown raid");
var a = await Get(1); var b = await Get(2);
Check(Items(a).Count == 5, "initial grant once");
Check(Items(await Get(1)).Count == 5, "read does not grant again");
var rifle = Items(a).First(x => x!["slot"]!.GetValue<int>() == 10)!;
Check(rifle["durability"]?.GetValue<int>() == 10000 && rifle["reserve"]!.GetValue<int>() == 40, "new rifle full condition and 20/40");
var op = Guid.NewGuid();
var move = new { operationId = op, profile = 1, revision = Rev(a), item = Id(rifle), destination = 105 };
a = await Post("/move", move);
Check(Items(a).Any(x => Id(x!) == Id(rifle) && x!["slot"]!.GetValue<int>() == 105), "move preserves identity");
var duplicate = await Post("/move", move);
Check(Rev(duplicate) == Rev(a), "replay returns original receipt");
await Post("/move", new { operationId = op, profile = 1, revision = Rev(a), item = Id(rifle), destination = 101 }, HttpStatusCode.Conflict);
await Post("/move", new { operationId = Guid.NewGuid(), profile = 1, revision = 0, item = Id(rifle), destination = 10 }, HttpStatusCode.Conflict);
await Post("/move", new { operationId = Guid.NewGuid(), profile = 1, revision = Rev(a), item = Id(rifle), destination = 8 }, HttpStatusCode.Conflict);
Check(true, "changed replay, stale revision and rifle safe rejected");
a = await Post("/move", new { operationId = Guid.NewGuid(), profile = 1, revision = Rev(a), item = Id(rifle), destination = 10 });
var raid = Guid.NewGuid();
await Post("/raids/open", new { operationId = Guid.NewGuid(), raidId = raid });
await Post("/raids/enter", new { operationId = Guid.NewGuid(), raidId = raid, profile = 1, revision = Rev(a) });
a = await Get(1);
Check(Items(a).All(x => x!["slot"]!.GetValue<int>() >= 100), "entry reserves equipment out of base");
await Post("/raids/enter", new { operationId = Guid.NewGuid(), raidId = raid, profile = 1, revision = Rev(a) }, HttpStatusCode.Conflict);
var enteredB = await Post("/raids/enter", new { operationId = Guid.NewGuid(), raidId = raid, profile = 2, revision = Rev(b) });
var enteredA = (await client.GetFromJsonAsync<JsonObject>($"/raids/{raid}"))!;
var worldItems = enteredA["items"]!.AsArray();
var ownerA = a["id"]!.GetValue<string>();
var ownerB = b["id"]!.GetValue<string>();
// A dies; B has picked A's rifle and extracts first. Full ordered server snapshot.
foreach (var n in worldItems)
{
    var item = n!;
    if (Id(item) == Id(rifle)) { item["owner"] = ownerB; item["slot"] = 0; item["magazine"] = 7; item["durability"] = 2500; item["custody"] = "result"; }
    else if (item["owner"]!.GetValue<string>() == ownerA)
    { item["owner"] = ""; item["slot"] = -1; item["custody"] = "world"; }
    else item["custody"] = "result";
}
var snapshotOp = Guid.NewGuid();
var snap = new { operationId = snapshotOp, raidId = raid, sequence = 1, items = worldItems, results = new[] { new { profile = 2, outcome = "extracted" }, new { profile = 1, outcome = "eliminated" } } };
await Post("/raids/snapshot", snap);
await Post("/raids/snapshot", snap);
b = await Get(2);
Check(Items(b).Count(x => Id(x!) == Id(rifle)) == 1, "new owner extracts exactly one transferred rifle");
Check(Items(b).First(x => Id(x!) == Id(rifle))!["magazine"]!.GetValue<int>() == 7, "ammo preserved");
Check(Items(b).First(x=>Id(x!)==Id(rifle))!["durability"]!.GetValue<int>()==2500,"condition survives transfer and extraction");
Check(Items(await Get(1)).All(x => Id(x!) != Id(rifle)), "death cannot restore old owner's transferred rifle");
await Post("/raids/snapshot", new { operationId = Guid.NewGuid(), raidId = raid, sequence = 1, items = worldItems, results = Array.Empty<object>() }, HttpStatusCode.Conflict);
await Post("/raids/close", new { operationId = Guid.NewGuid(), raidId = raid });
// Abandonment conserves the latest safe, not an entry copy.
a = await Get(1);
var supply = Items(a).First(x => x!["kind"]!.GetValue<int>() == 1)!;
a = await Post("/move", new { operationId = Guid.NewGuid(), profile = 1, revision = Rev(a), item = Id(supply), destination = 0 });
var raid2 = Guid.NewGuid();
await Post("/raids/open", new { operationId = Guid.NewGuid(), raidId = raid2 });
await Post("/raids/enter", new { operationId = Guid.NewGuid(), raidId = raid2, profile = 1, revision = Rev(a) });
var state2 = (await client.GetFromJsonAsync<JsonObject>($"/raids/{raid2}"))!;
var safeItems = state2["items"]!.AsArray();
safeItems.Single()!["slot"] = 8;
await Post("/raids/snapshot", new { operationId = Guid.NewGuid(), raidId = raid2, sequence = 1, items = safeItems, results = Array.Empty<object>() });
await Post("/raids/close", new { operationId = Guid.NewGuid(), raidId = raid2 });
a = await Get(1);
Check(Items(a).Any(x => Id(x!) == Id(supply) && x!["slot"]!.GetValue<int>() == 8), "abandonment preserves last durable safe");
Console.WriteLine($"ALL {passed} CONTRACT ASSERTIONS PASSED");
// Result custody must become usable equipment on the next raid.
b = await Get(2);
var raid3 = Guid.NewGuid();
var openOp = Guid.NewGuid();
await Post("/raids/open", new { operationId = openOp, raidId = raid3 });
await Post("/raids/close", new { operationId = openOp, raidId = raid3 }, HttpStatusCode.Conflict);
await Post("/raids/enter", new { operationId = Guid.NewGuid(), raidId = raid3, profile = 2, revision = Rev(b) });
var third = (await client.GetFromJsonAsync<JsonObject>($"/raids/{raid3}"))!;
var thirdItems = third["items"]!.AsArray();
Check(thirdItems.All(x => x!["custody"]!.GetValue<string>() == "inventory"), "returned equipment is active inventory next raid");
var worn=thirdItems.First(x => Id(x!) == Id(rifle))!;
foreach(var invalid in new[]{-1,10001,2750})
{
    worn["durability"]=invalid;
    await Post("/raids/snapshot",new{operationId=Guid.NewGuid(),raidId=raid3,sequence=1,items=thirdItems,results=Array.Empty<object>()},HttpStatusCode.Conflict);
}
worn.AsObject().Remove("durability");
await Post("/raids/snapshot",new{operationId=Guid.NewGuid(),raidId=raid3,sequence=1,items=thirdItems,results=Array.Empty<object>()},HttpStatusCode.Conflict);
Check(true,"invalid, increased or omitted worn condition rejected");
worn["durability"]=0;
worn["magazine"]=6;
await Post("/raids/snapshot", new { operationId = Guid.NewGuid(), raidId = raid3, sequence = 1, items = thirdItems, results = Array.Empty<object>() });
var persistedThird=(await client.GetFromJsonAsync<JsonObject>($"/raids/{raid3}"))!;
Check(persistedThird["items"]!.AsArray().Single(x=>Id(x!)==Id(rifle))!["durability"]!.GetValue<int>()==0,"zero condition persisted explicitly");
await Post("/raids/close", new { operationId = Guid.NewGuid(), raidId = raid3 });
Console.WriteLine("NEXT RAID AND CROSS-ROUTE RECEIPT PASSED");// Same revision concurrent requests serialize: precisely one winner.
a = await Get(1);
var concurrentItem = Items(a).First()!;
var payload1 = new { operationId=Guid.NewGuid(), profile=1, revision=Rev(a), item=Id(concurrentItem), destination=110 };
var payload2 = new { operationId=Guid.NewGuid(), profile=1, revision=Rev(a), item=Id(concurrentItem), destination=111 };
var races=await Task.WhenAll(client.PostAsJsonAsync("/move",payload1),client.PostAsJsonAsync("/move",payload2));
Check(races.Count(x => x.StatusCode==HttpStatusCode.OK)==1 && races.Count(x => x.StatusCode==HttpStatusCode.Conflict)==1,"concurrent stale moves have one winner");
// A terminal participant cannot acquire a new result from a later snapshot.
a=await Get(1);b=await Get(2);
var raid4=Guid.NewGuid();
await Post("/raids/open",new {operationId=Guid.NewGuid(),raidId=raid4});
await Post("/raids/enter",new {operationId=Guid.NewGuid(),raidId=raid4,profile=1,revision=Rev(a)});
await Post("/raids/enter",new {operationId=Guid.NewGuid(),raidId=raid4,profile=2,revision=Rev(b)});
var fourth=(await client.GetFromJsonAsync<JsonObject>($"/raids/{raid4}"))!;
var fourthItems=fourth["items"]!.AsArray();
foreach(var i in fourthItems.Where(x=>x!["owner"]!.GetValue<string>()==ownerA))i!["custody"]="result";
var extra=JsonSerializerNode(new {id=Guid.NewGuid(),kind=0,slot=-1,magazine=10,reserve=30,owner="",custody="world"});
fourthItems.Add(extra);
extra["durability"]=9750;
await Post("/raids/snapshot",new{operationId=Guid.NewGuid(),raidId=raid4,sequence=1,items=fourthItems,results=new[]{new{profile=1,outcome="extracted"}}},HttpStatusCode.Conflict);
extra.Remove("durability");
Check(true,"new worn identity rejected; legacy absent condition remains full");
await Post("/raids/snapshot",new {operationId=Guid.NewGuid(),raidId=raid4,sequence=1,items=fourthItems,results=new[]{new{profile=1,outcome="extracted"}}});
extra["custody"]="result";extra["owner"]=ownerA;extra["slot"]=1;
await Post("/raids/snapshot",new {operationId=Guid.NewGuid(),raidId=raid4,sequence=2,items=fourthItems,results=Array.Empty<object>()},HttpStatusCode.Conflict);
Check(true,"finished participant cannot silently receive new loot");
extra["custody"]="world";extra["owner"]="";extra["slot"]=-1;
foreach(var i in fourthItems.Where(x=>x!["owner"]!.GetValue<string>()==ownerB)){i!["owner"]="";i["custody"]="world";i["slot"]=-1;}
await Post("/raids/snapshot",new {operationId=Guid.NewGuid(),raidId=raid4,sequence=2,items=fourthItems,results=new[]{new{profile=2,outcome="eliminated"}}});
Check(Items(await Get(1)).Count==Items(a).Count,"separate later elimination preserves earlier extraction");
await Post("/raids/close",new {operationId=Guid.NewGuid(),raidId=raid4});
// Healing consumption is a terminal identity, never an omitted snapshot record.
a=await Get(1);
var kits=Items(a).Where(x=>x!["kind"]!.GetValue<int>()==1).Take(2).Select(x=>Id(x!)).ToArray();
var healingScrap=Id(Items(a).First(x=>x!["kind"]!.GetValue<int>()==2)!);
for(int n=0;n<2;n++)
{
    var kit=Items(a).Single(x=>Id(x!)==kits[n])!;
    if(kit["slot"]!.GetValue<int>()!=8+n)a=await Post("/move",new{operationId=Guid.NewGuid(),profile=1,revision=Rev(a),item=kits[n],destination=8+n});
}
if(Items(a).Single(x=>Id(x!)==healingScrap)!["slot"]!.GetValue<int>()!=0)
    a=await Post("/move",new{operationId=Guid.NewGuid(),profile=1,revision=Rev(a),item=healingScrap,destination=0});
var healingRaid=Guid.NewGuid();
await Post("/raids/open",new{operationId=Guid.NewGuid(),raidId=healingRaid});
await Post("/raids/enter",new{operationId=Guid.NewGuid(),raidId=healingRaid,profile=1,revision=Rev(a)});
var healingState=(await client.GetFromJsonAsync<JsonObject>($"/raids/{healingRaid}"))!;
var healingItems=healingState["items"]!.AsArray();
var groundKit=Guid.NewGuid();
healingItems.Add(JsonSerializerNode(new{id=groundKit,kind=1,slot=-1,magazine=0,reserve=0,owner="",custody="world"}));
await Post("/raids/snapshot",new{operationId=Guid.NewGuid(),raidId=healingRaid,sequence=1,items=healingItems,results=Array.Empty<object>()});
var invalidSupply=healingItems.DeepClone().AsArray();invalidSupply.First(x=>x!["kind"]!.GetValue<int>()==1)!["durability"]=10000;
await Post("/raids/snapshot",new{operationId=Guid.NewGuid(),raidId=healingRaid,sequence=2,items=invalidSupply,results=Array.Empty<object>()},HttpStatusCode.Conflict);
Check(true,"supplies cannot acquire rifle durability");
JsonArray CopyHealing()=>healingItems.DeepClone().AsArray();
void Consumed(JsonNode item){item["custody"]="consumed";item["owner"]="";item["slot"]=-1;}
var badHeal=CopyHealing();Consumed(badHeal.Single(x=>Id(x!)==healingScrap)!);
await Post("/raids/snapshot",new{operationId=Guid.NewGuid(),raidId=healingRaid,sequence=2,items=badHeal,results=Array.Empty<object>()},HttpStatusCode.Conflict);
badHeal=CopyHealing();Consumed(badHeal.Single(x=>Id(x!)==groundKit.ToString())!);
await Post("/raids/snapshot",new{operationId=Guid.NewGuid(),raidId=healingRaid,sequence=2,items=badHeal,results=Array.Empty<object>()},HttpStatusCode.Conflict);
badHeal=CopyHealing();badHeal.Add(JsonSerializerNode(new{id=Guid.NewGuid(),kind=1,slot=-1,magazine=0,reserve=0,owner="",custody="consumed"}));
await Post("/raids/snapshot",new{operationId=Guid.NewGuid(),raidId=healingRaid,sequence=2,items=badHeal,results=Array.Empty<object>()},HttpStatusCode.Conflict);
Check(true,"consumption rejects scrap, world item and new consumed identity");
var consumedKit=healingItems.Single(x=>Id(x!)==kits[0])!;Consumed(consumedKit);
var consumeOp=Guid.NewGuid();
var consumeCommand=new{operationId=consumeOp,raidId=healingRaid,sequence=2,items=healingItems,results=Array.Empty<object>()};
await Post("/raids/snapshot",consumeCommand);
await Post("/raids/snapshot",consumeCommand);
Check(true,"owned safe supply consumption accepted once with idempotent receipt");
badHeal=CopyHealing();var restored=badHeal.Single(x=>Id(x!)==kits[0])!;restored["custody"]="inventory";restored["owner"]=ownerA;restored["slot"]=8;
await Post("/raids/snapshot",new{operationId=Guid.NewGuid(),raidId=healingRaid,sequence=3,items=badHeal,results=Array.Empty<object>()},HttpStatusCode.Conflict);
badHeal=CopyHealing();badHeal.Remove(badHeal.Single(x=>Id(x!)==kits[0]));
await Post("/raids/snapshot",new{operationId=Guid.NewGuid(),raidId=healingRaid,sequence=3,items=badHeal,results=Array.Empty<object>()},HttpStatusCode.Conflict);
Check(true,"consumed identity cannot be resurrected or omitted");
foreach(var node in healingItems.Where(x=>x!["custody"]!.GetValue<string>()=="inventory"))
{
    if(node!["slot"]!.GetValue<int>()==9)node["custody"]="result";
    else{node["custody"]="world";node["owner"]="";node["slot"]=-1;}
}
await Post("/raids/snapshot",new{operationId=Guid.NewGuid(),raidId=healingRaid,sequence=3,items=healingItems,results=new[]{new{profile=1,outcome="eliminated"}}});
a=await Get(1);
Check(!Items(a).Any(x=>Id(x!)==kits[0]) && Items(a).Any(x=>Id(x!)==kits[1]),"death preserves unused safe supply without restoring consumed kit");
badHeal=CopyHealing();Consumed(badHeal.Single(x=>Id(x!)==kits[1])!);
await Post("/raids/snapshot",new{operationId=Guid.NewGuid(),raidId=healingRaid,sequence=4,items=badHeal,results=Array.Empty<object>()},HttpStatusCode.Conflict);
Check(true,"confirmed result cannot be consumed afterward");
await Post("/raids/close",new{operationId=Guid.NewGuid(),raidId=healingRaid});
var afterHealing=Guid.NewGuid();await Post("/raids/open",new{operationId=Guid.NewGuid(),raidId=afterHealing});
var resurrected=JsonSerializerNode(new{id=kits[0],kind=1,slot=-1,magazine=0,reserve=0,owner="",custody="world"});
await Post("/raids/snapshot",new{operationId=Guid.NewGuid(),raidId=afterHealing,sequence=1,items=new[]{resurrected},results=Array.Empty<object>()},HttpStatusCode.Conflict);
Check(true,"new raid cannot reuse consumed historical identity");
await Post("/raids/close",new{operationId=Guid.NewGuid(),raidId=afterHealing});
var healingReport=Environment.GetEnvironmentVariable("MARAU6_HEALING_REPORT");
if(healingReport!=null)await File.WriteAllTextAsync(healingReport,System.Text.Json.JsonSerializer.Serialize(new{raid=healingRaid,item=kits[0],profile=1}));

var wearReport=Environment.GetEnvironmentVariable("MARAU6_WEAR_REPORT");
if(wearReport!=null)await File.WriteAllTextAsync(wearReport,JsonSerializer.Serialize(new{legacy=legacyBody,raid=raid3,item=Id(rifle)}));
// Save a replay contract for the launcher to verify after restarting the backend.
a=await Get(1);
var restartBody=new {operationId=Guid.NewGuid(),profile=1,revision=Rev(a),item=Id(Items(a).First()!),destination=120};
var restartResult=await Post("/move",restartBody);
var replayFile=Environment.GetEnvironmentVariable("MARAU6_REPLAY_FILE");
if(replayFile!=null)await File.WriteAllTextAsync(replayFile,System.Text.Json.JsonSerializer.Serialize(new {body=restartBody,response=restartResult}));
// Real HTTP ammunition lifecycle: world -> inventory -> consumed -> extraction -> base.
var ammoProfile=await Get(1);var ammoRaid=Guid.NewGuid();
await Post("/raids/open",new{operationId=Guid.NewGuid(),raidId=ammoRaid});
await Post("/raids/enter",new{operationId=Guid.NewGuid(),raidId=ammoRaid,profile=1,revision=Rev(ammoProfile)});
var ammoState=(await client.GetFromJsonAsync<JsonObject>($"/raids/{ammoRaid}"))!;var ammoItems=ammoState["items"]!.AsArray();
var ammoOwner=ammoProfile["id"]!.GetValue<string>();var gunId=Guid.NewGuid();var boxId=Guid.NewGuid();var spareBox=Guid.NewGuid();
var gunNode=JsonSerializerNode(new{id=gunId,kind=0,slot=-1,magazine=20,reserve=40,durability=10000,owner="",custody="world"});
var boxNode=JsonSerializerNode(new{id=boxId,kind=3,slot=-1,magazine=0,reserve=0,owner="",custody="world"});
var spareNode=JsonSerializerNode(new{id=spareBox,kind=3,slot=-1,magazine=0,reserve=0,owner="",custody="world"});
ammoItems.Add(gunNode);ammoItems.Add(boxNode);ammoItems.Add(spareNode);
await Post("/raids/snapshot",new{operationId=Guid.NewGuid(),raidId=ammoRaid,sequence=1,items=ammoItems,results=Array.Empty<object>()});
var freeSlots=Enumerable.Range(0,8).Where(slot=>!ammoItems.Any(i=>i!["owner"]!.GetValue<string>()==ammoOwner && i["slot"]!.GetValue<int>()==slot)).Take(3).ToArray();
foreach(var pair in new[]{(gunNode,freeSlots[0]),(boxNode,freeSlots[1]),(spareNode,freeSlots[2])}){pair.Item1["owner"]=ammoOwner;pair.Item1["custody"]="inventory";pair.Item1["slot"]=pair.Item2;}
await Post("/raids/snapshot",new{operationId=Guid.NewGuid(),raidId=ammoRaid,sequence=2,items=ammoItems,results=Array.Empty<object>()});
gunNode["reserve"]=60;
await Post("/raids/snapshot",new{operationId=Guid.NewGuid(),raidId=ammoRaid,sequence=3,items=ammoItems,results=Array.Empty<object>()},HttpStatusCode.Conflict);
Consumed(boxNode);var ammoConsume=new{operationId=Guid.NewGuid(),raidId=ammoRaid,sequence=3,items=ammoItems,results=Array.Empty<object>()};
await Post("/raids/snapshot",ammoConsume);await Post("/raids/snapshot",ammoConsume);Check(true,"ammo reserve increase paid exactly once by consumed box");
var badAmmo=ammoItems.DeepClone().AsArray();var revivedBox=badAmmo.Single(x=>Id(x!)==boxId.ToString())!;revivedBox["custody"]="inventory";revivedBox["owner"]=ammoOwner;revivedBox["slot"]=freeSlots[1];
await Post("/raids/snapshot",new{operationId=Guid.NewGuid(),raidId=ammoRaid,sequence=4,items=badAmmo,results=Array.Empty<object>()},HttpStatusCode.Conflict);
foreach(var n in ammoItems.Where(x=>x!["owner"]!.GetValue<string>()==ammoOwner))n!["custody"]="result";
await Post("/raids/snapshot",new{operationId=Guid.NewGuid(),raidId=ammoRaid,sequence=4,items=ammoItems,results=new[]{new{profile=1,outcome="extracted"}}});
await Post("/raids/close",new{operationId=Guid.NewGuid(),raidId=ammoRaid});
ammoProfile=await Get(1);var baseAmmoBody=new{operationId=Guid.NewGuid(),profile=1,revision=Rev(ammoProfile),item=spareBox,destination=freeSlots[0]};
var ammoResponse=await Post("/move",baseAmmoBody);var ammoReplay=await Post("/move",baseAmmoBody);
Check(Rev(ammoResponse)==Rev(ammoReplay) && Items(ammoResponse).Single(x=>Id(x!)==gunId.ToString())!["reserve"]!.GetValue<int>()==80 && !Items(ammoResponse).Any(x=>Id(x!)==spareBox.ToString()),"base ammo drag consumes once and preserves rifle identity");
await Post("/move",new{operationId=Guid.NewGuid(),profile=1,revision=Rev(ammoResponse),item=spareBox,destination=freeSlots[0]},HttpStatusCode.Conflict);
var ammoReport=Environment.GetEnvironmentVariable("MARAU6_AMMO_REPORT");if(ammoReport!=null)await File.WriteAllTextAsync(ammoReport,JsonSerializer.Serialize(new{body=baseAmmoBody,response=ammoResponse,rifle=gunId,consumed=spareBox,profile=1}));
// Real HTTP BASIC kit raid: base gear stays, the kit rifle extracts into a free stash slot past the unique slot index.
var kitProfile=await Get(1);
if(!Items(kitProfile).Any(x=>x!["slot"]!.GetValue<int>()==10))
    kitProfile=await Post("/move",new{operationId=Guid.NewGuid(),profile=1,revision=Rev(kitProfile),item=Id(Items(kitProfile).First(x=>x!["kind"]!.GetValue<int>()==0)!),destination=10});
var kitBase=Items(kitProfile).Select(x=>(Id(x!),x!["slot"]!.GetValue<int>())).OrderBy(x=>x.Item1).ToList();
var kitRaid=Guid.NewGuid();
await Post("/raids/open",new{operationId=Guid.NewGuid(),raidId=kitRaid});
var kitEnter=await Post("/raids/enter",new{operationId=Guid.NewGuid(),raidId=kitRaid,profile=1,revision=Rev(kitProfile),kit="basic"});
var kitRifle=Items(kitEnter)[0]!;
Check(Items(kitEnter).Select(x=>(x!["kind"]!.GetValue<int>(),x["slot"]!.GetValue<int>())).SequenceEqual(new[]{(0,10),(3,0),(3,1),(1,2)}),"basic entry mints rifle, two ammo boxes and medkit");
Check(kitRifle["magazine"]!.GetValue<int>()==20 && kitRifle["reserve"]!.GetValue<int>()==40 && kitRifle["durability"]!.GetValue<int>()==10000,"basic kit rifle is fresh 20/40");
var kitDuring=await Get(1);
Check(Items(kitDuring).Select(x=>(Id(x!),x!["slot"]!.GetValue<int>())).OrderBy(x=>x.Item1).SequenceEqual(kitBase) && Rev(kitDuring)==Rev(kitProfile)+1,"basic entry leaves base items and slots");
var kitState=(await client.GetFromJsonAsync<JsonObject>($"/raids/{kitRaid}"))!;
Check(kitState["participants"]!.AsArray().Single()!["kit"]!.GetValue<string>()=="basic","basic participant recorded");
var kitItems=kitState["items"]!.AsArray();var kitBox=Id(kitItems[1]!);var kitSpare=Id(kitItems[2]!);var kitMed=Id(kitItems[3]!);
kitItems.Single(x=>Id(x!)==Id(kitRifle))!["reserve"]=60;Consumed(kitItems.Single(x=>Id(x!)==kitBox)!);
await Post("/raids/snapshot",new{operationId=Guid.NewGuid(),raidId=kitRaid,sequence=1,items=kitItems,results=Array.Empty<object>()});
Consumed(kitItems.Single(x=>Id(x!)==kitMed)!);
await Post("/raids/snapshot",new{operationId=Guid.NewGuid(),raidId=kitRaid,sequence=2,items=kitItems,results=Array.Empty<object>()});
Check(true,"basic kit ammo box pays rifle reserve and medkit is consumable");
foreach(var n in kitItems.Where(x=>x!["custody"]!.GetValue<string>()=="inventory"))n!["custody"]="result";
await Post("/raids/snapshot",new{operationId=Guid.NewGuid(),raidId=kitRaid,sequence=3,items=kitItems,results=new[]{new{profile=1,outcome="extracted"}}});
await Post("/raids/close",new{operationId=Guid.NewGuid(),raidId=kitRaid});
var kitAfter=await Get(1);var kitSlots=Items(kitAfter).Select(x=>x!["slot"]!.GetValue<int>()).ToList();
Check(kitBase.All(x=>Items(kitAfter).Any(y=>Id(y!)==x.Item1 && y!["slot"]!.GetValue<int>()==x.Item2)) && Items(kitAfter).Single(x=>Id(x!)==Id(kitRifle))!["slot"]!.GetValue<int>() is >=100 and <132 && kitSlots.Distinct().Count()==kitSlots.Count,"basic extraction remaps kit rifle to free stash slot");
Check(Items(kitAfter).Single(x=>Id(x!)==Id(kitRifle))!["reserve"]!.GetValue<int>()==60 && Items(kitAfter).Count(x=>Id(x!)==kitSpare)==1 && !Items(kitAfter).Any(x=>Id(x!)==kitBox||Id(x!)==kitMed) && Items(kitAfter).Count==kitBase.Count+2,"basic extraction keeps surviving kit and drops consumed kit");
Console.WriteLine($"ALL {passed} CONTRACT ASSERTIONS PASSED");
static JsonObject JsonSerializerNode(object value)=>System.Text.Json.JsonSerializer.SerializeToNode(value)!.AsObject();