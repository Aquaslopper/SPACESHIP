#include "voyage.hpp"
#include "native_data.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace spaceship {
namespace {
const Json* find(const Json& list, const std::string& id) {
  for (const auto& item : list) if (item.value("id", "") == id) return &item;
  return nullptr;
}
std::string text(const Json& j, const char* key) { return j.value(key, std::string{}); }
int number(const Json& j, const char* key) { return j.value(key, 0); }
void add(Json& j, const char* key, int amount) { j[key] = number(j,key) + amount; }
std::string bodySystem(const Json& b) { return b.value("system", std::string("sec-kessler")); }
std::string keyFor(const std::string& c, const std::string& a) {
  if (c=="travel" || c=="jump" || c=="scan") return "nav:"+c;
  return c+":"+a;
}
int tier(const std::string& c) {
  if(c=="watch" || c=="claim") return 0;
  if(c=="travel" || c=="jump" || c=="scan" || c=="salvage" || c=="dock" || c=="undock") return 15;
  if(c=="ship" || c=="jettison" || c=="preserve" || c=="sell-signal" || c=="abandon") return 20;
  if(c=="project" || c=="repair" || c=="maintain" || c=="install" || c=="filters" || c=="fuel" || c=="transfer" || c=="medical" || c=="social") return 10;
  return 5;
}
}
const Json& Voyage::catalog() { static const Json data=[] { std::string source; for(const char* part:nativeDataParts) source+=part; return Json::parse(source); }(); return data; }
Voyage::Voyage() : save_(catalog()["initial"]) {
  save_["format"]="spaceship-native-1";
  save_["state"]["nativeTasks"]=Json::array();
  save_["state"]["nativeIncidents"]=Json::array();
  save_["state"]["nativeVisited"]=Json::object();
  save_["state"]["sim"]["memories"]=Json::array();
  save_["state"]["sim"]["serial"]=0;
  remember("Watch", "Native C++ voyage begins at Kessler Drift.");
}
int Voyage::actions() const { return number(save_,"actions"); }
int Voyage::stat(const std::string& id) const { const auto* s=find(save_["stats"],id); return s ? number(*s,"value") : 0; }
void Voyage::change(const std::string& id,int amount) { for(auto& s:save_["stats"]) if(text(s,"id")==id) s["value"]=std::clamp(number(s,"value")+amount,0,100); }
int Voyage::cargoUsed() const { int n=0;for(const auto& g:save_["goods"]) n+=number(g,"vol");return n; }
int Voyage::cooldown(const std::string& key) const { const auto& c=save_["state"]["actionCooldowns"];return c.contains(key)? number(c[key],"remaining"):0; }
std::string Voyage::location() const { auto* b=find(catalog()["bodies"],text(save_["map"],"here"));return b?text(*b,"name"):"Unknown"; }
bool Voyage::busy(const std::string& id) const { for(const auto& t:save_["state"]["nativeTasks"]) {
    if(text(t,"id")==id || text(t,"target")==id) return true;
    if(text(t,"id")=="social" && t["payload"].is_array()) for(const auto& member:t["payload"]) if(member==id) return true;
  }return false; }
bool Voyage::reject(const std::string& why) { message_=why;return false; }
void Voyage::task(const std::string& id,const std::string& target,int work,const Json& payload) {
  save_["state"]["nativeTasks"].push_back({{"id",id},{"target",target},{"left",work},{"total",work},{"payload",payload}});
}
void Voyage::remember(const std::string& kind,const std::string& line) {
  auto& sim=save_["state"]["sim"];add(sim,"serial",1);
  sim["memories"].push_back({{"id",sim["serial"]},{"kind",kind},{"text",line},{"at",actions()},{"processed",false}});
  if(sim["memories"].size()>32) sim["memories"].erase(sim["memories"].begin());
  auto& feed=save_["state"]["feed"];feed.push_back(std::to_string(actions())+" | "+line);
  if(feed.size()>80) feed.erase(feed.begin());
}
bool Voyage::act(const std::string& c,const std::string& a) {
  // Invalid and rejected commands cannot leave partial resource mutations behind.
  Json before=save_;
  try { if(execute(c,a)) return true; }
  catch(const std::exception& e) { message_=std::string("Command rejected: ")+e.what(); }
  save_=std::move(before);return false;
}
bool Voyage::execute(const std::string& c,const std::string& a) {
  auto& s=save_["state"];auto& sim=s["sim"];
  if(!text(s,"gameOver").empty()) return reject(text(s,"gameOver")+" Load a save or start a new voyage.");
  const auto key=keyFor(c,a);const int wait=cooldown(key);
  if(wait) return reject("Cooldown: "+std::to_string(wait)+" other committed actions.");
  std::string pending,target;int work=0;Json payload;
  const bool docked=s.value("docked",false);
  int reservedCargo=busy("salvage")?5:0;
  if(c=="watch") { /* Native clock advance; intentionally repeatable. */ }
  else if(c=="travel" || c=="jump") {
    const auto* b=find(catalog()[c=="jump"?"systems":"bodies"],a);
    const auto* here=find(catalog()["bodies"],text(save_["map"],"here"));
    if(!b || !here || docked || busy("travel")) return reject("Undock and finish the current crossing first.");
    if((c=="travel" && (a==text(*here,"id") || bodySystem(*b)!=bodySystem(*here))) ||
       (c=="jump" && a==bodySystem(*here))) return reject("Choose another reachable destination.");
    double distance=std::hypot(number(*b,"x")-number(*here,"x"),number(*b,"y")-number(*here,"y"));
    int cost=c=="jump"?20:std::max(2,static_cast<int>(std::ceil(distance/30.0)));
    for(const auto& crew:save_["crew"]) if(text(crew,"job")=="navigation" && !busy(text(crew,"id"))) {cost=std::max(1,cost-cost*std::clamp(number(crew,"jobAt"),1,5)*4/100);break;}
    if(s["upgrades"].value("drive",false)) cost=std::max(1,cost*80/100);
    if(stat("fuel")<cost || stat("energy")<1) return reject("Insufficient fuel or power.");
    change("fuel",-cost);pending="travel";target=c=="jump"?text(*b,"entry"):a;work=c=="jump"?8:std::max(3,static_cast<int>(std::ceil(distance/32.0)));
  }
  else if(c=="scan") {
    const auto* b=find(catalog()["bodies"],a);const auto* here=find(catalog()["bodies"],text(save_["map"],"here"));
    if(!b || !here || bodySystem(*b)!=bodySystem(*here) || busy("scan") || stat("energy")<3 || s["scanned"].value(a,false)) return reject("Choose an unscanned local body; needs 3 energy and idle sensors.");
    change("energy",-3);pending="scan";target=a;work=std::max(1,5-number(sim,"sensor"));sim["sensor"]=0;
  }
  else if(c=="dock" || c=="undock") {
    if(c=="undock") {if(!docked) return reject("Not docked.");s["docked"]=false;}
    else {if(docked || busy("travel") || !catalog()["ports"].contains(text(save_["map"],"here"))) return reject("Reach a station first.");s["docked"]=true;}
  }
  else if(c=="fuel") {
    if(number(s,"fuelCells")<1 || (stat("fuel")==100 && stat("energy")==100)) return reject("Needs a fuel cell and room in the reserves.");
    add(s,"fuelCells",-1);change("fuel",20);change("energy",20);
  }
  else if(c=="repair" || c=="maintain") {
    if(number(s,"repairKits")<1 || busy(c) || (c=="repair"?stat("hull"):number(s,"engineCondition"))>=100) return reject("Needs damage, a repair kit and no matching work in progress.");
    add(s,"repairKits",-1);pending=c;work=4;
  }
  else if(c=="install") {
    int i=-1;for(int n=0;n<3;++n) if(a=="s"+std::to_string(n)) i=n;
    if(i<0 || text(save_["life"]["slots"][i],"state")!="empty" || number(save_["life"],"canisters")<1) return reject("Needs an empty slot and an O2 canister.");
    add(save_["life"],"canisters",-1);save_["life"]["slots"][i]["state"]="installing";pending="install-"+a;target=a;work=3;
  }
  else if(c=="filters") {
    for(const auto& slot:save_["life"]["slots"]) if(text(slot,"state")!="seated") return reject("Seat all three canisters first.");
    if(stat("energy")<1) return reject("Needs power.");
    // Switch-on happens after the initiating world tick, like new work.
    pending="start-filters";
  }
  else if(c=="cook") {
    if(busy("cook") || busy("project-food") || number(save_["mess"],"rack")>number(save_["mess"],"capacity")-6 || stat("energy")<3) return reject("Needs room for six rations, 3 energy and an idle galley.");
    change("energy",-3);pending="cook";work=4;
  }
  else if(c=="salvage") {
    if(!busy("travel") || busy("salvage") || cargoUsed()+5>number(save_["cargo"],"capacity") || stat("energy")<4) return reject("Recover salvage during a crossing; needs 5 cargo space and 4 energy.");
    change("energy",-4);pending="salvage";work=3;
  }
  else if(c=="buy" || c=="sell") {
    if(!docked) return reject("Trade at a station.");
    const auto& port=catalog()["ports"][text(save_["map"],"here")];
    if(c=="buy" && (a=="fuel" || a=="kit" || a=="med" || a=="oxygen" || a=="rations")) {
      const int price=a=="fuel"?45:a=="kit"?60:a=="med"?75:a=="oxygen"?25:20;
      if(number(s,"shiplets")<price) return reject("Insufficient shiplets.");
      if(a=="oxygen" && number(save_["life"],"canisters")>=number(save_["life"],"capacity")) return reject("Canister rack full.");
      const int reservedRations=(busy("cook")?6:0)+(busy("project-food")?8:0);
      if(a=="rations" && number(save_["mess"],"rack")+reservedRations>number(save_["mess"],"capacity")-6) return reject("Needs room for six rations.");
      add(s,"shiplets",-price);
      if(a=="fuel") add(s,"fuelCells",1);else if(a=="kit") add(s,"repairKits",1);else if(a=="med") add(s,"medkits",1);
      else if(a=="oxygen") add(save_["life"],"canisters",1);else add(save_["mess"],"rack",6);
    } else if(c=="buy") {
      const auto* good=find(catalog()["market"],a);if(!good) return reject("Unknown cargo.");
      int price=static_cast<int>(number(*good,"value")*port["prices"].value(text(*good,"kind"),1.0));
      if(number(s,"shiplets")<price || cargoUsed()+reservedCargo+number(*good,"vol")>number(save_["cargo"],"capacity")) return reject("Insufficient money or cargo space.");
      add(s,"shiplets",-price);Json lot=*good;add(s,"lotSerial",1);lot["id"]="lot-"+std::to_string(number(s,"lotSerial"));save_["goods"].push_back(lot);
    } else {
      auto& goods=save_["goods"];auto it=std::find_if(goods.begin(),goods.end(),[&](const Json& g){return text(g,"id")==a;});
      if(it==goods.end() || it->contains("contractId")) return reject("Choose uncommitted cargo.");
      add(s,"shiplets",static_cast<int>(number(*it,"value")*port["prices"].value(text(*it,"kind"),1.0)));goods.erase(it);
    }
  }
  else if(c=="sell-data") {
    if(!docked || save_["survey"].empty()) return reject("Bring survey records to a station.");
    int value=0;for(const auto& record:save_["survey"]) value+=number(record,"value");add(s,"shiplets",value);save_["survey"]=Json::array();
  }
  else if(c=="jettison") {
    auto& goods=save_["goods"];auto it=std::find_if(goods.begin(),goods.end(),[&](const Json& g){return text(g,"id")==a && !g.contains("contractId");});
    if(it==goods.end()) return reject("Choose uncommitted cargo.");
    goods.erase(it);
  }
  else if(c=="project") {
    if(!catalog()["projects"].contains(a)) return reject("Unknown project.");
    const auto& p=catalog()["projects"][a];
    if(busy("project-"+a) || stat("energy")<=number(p,"energy")) return reject("Project is running or needs more power.");
    if(a=="oxygen" && number(save_["life"],"canisters")>=number(save_["life"],"capacity")) return reject("Canister rack full.");
    if(a=="food" && (busy("cook") || number(save_["mess"],"rack")>number(save_["mess"],"capacity")-8)) return reject("Needs room for eight rations and an idle galley.");
    if(a=="cooling" && number(s,"engineCondition")>=100) return reject("Drive is already sound.");
    if(a=="brace" && stat("hull")==100) return reject("Hull is already sound.");
    if(a=="observe" && (busy("travel") || sim["observed"].value(text(save_["map"],"here"),false))) return reject("Stop at a location not already recorded.");
    if(a=="sensors" && (number(sim,"insight")<1 || number(sim,"sensor")>0 || busy("scan"))) return reject("Needs one insight and idle uncalibrated sensors.");
    if(a=="decode" && (text(sim,"signal")!="intercepted" || number(sim,"insight")<1)) return reject("Needs the intercepted signal and one insight.");
    if(a=="dream") {
      Json memories=Json::array();for(auto& m:sim["memories"]) if(!m.value("processed",false) && memories.size()<2) {m["processed"]=true;memories.push_back(m);}
      if(memories.size()!=2 || number(sim,"insight")>=5) return reject("Needs two unprocessed memories and room for an insight.");
      payload=memories;
    }
    if(a=="sensors" || a=="decode") add(sim,"insight",-1);
    if(a=="decode") sim["signal"]="decoding";
    if(a=="observe") {target=text(save_["map"],"here");sim["observed"][target]=true;}
    if(p.contains("lot")) {
      auto& goods=save_["goods"];auto it=std::find_if(goods.begin(),goods.end(),[&](const Json& g){return text(g,"kind")==text(p,"lot") && !g.value("risk",false) && !g.contains("contractId");});
      if(it==goods.end()) return reject("Needs an uncommitted "+text(p,"lot")+" lot.");
      goods.erase(it);
    }
    change("energy",-number(p,"energy"));pending="project-"+a;work=number(p,"work");
  }
  else if(c=="transfer") {
    const bool shield=a=="shield";if(stat(shield?"energy":"shield")<15 || stat(shield?"shield":"energy")>=100) return reject("Needs 15 reserve points and room at the destination.");
    change(shield?"energy":"shield",-15);change(shield?"shield":"energy",10);
  }
  else if(c=="rest" || c=="medical" || c=="talk" || c=="train" || c=="job") {
    Json* crew=nullptr;for(auto& person:save_["crew"]) if(text(person,"id")==a) crew=&person;
    if(!crew || busy(a) || number(*crew,"health")==0) return reject("Choose an available living crew member.");
    if(c=="medical") {
      if(number(s,"medkits")<1 || number(*crew,"health")==100 || stat("energy")<3) return reject("Needs injury, a medkit and 3 power.");
      add(s,"medkits",-1);change("energy",-3);pending="medical-"+a;target=a;work=4;
    } else if(c=="rest") {pending="rest-"+a;target=a;work=4;}
    else if(c=="talk") {(*crew)["morale"]=std::min(100,number(*crew,"morale")+3);remember("Care","The captain listened to "+text(*crew,"name")+".");}
    else if(c=="train") {
      if(!docked || number(s,"shiplets")<50 || text(*crew,"job").empty()) return reject("Training requires a job, a station and 50 shiplets.");
      add(s,"shiplets",-50);(*crew)["skill"]="Trained";(*crew)["jobAt"]=text(*crew,"job")=="maintenance"?std::min(75,number(*crew,"jobAt")+15):std::min(5,number(*crew,"jobAt")+1);
    } else {return reject("Use a specific assignment command.");}
  }
  else if(c.rfind("assign-",0)==0) {
    std::string job=c.substr(7);const auto* spec=find(catalog()["jobs"],job);if(!spec) return reject("Unknown assignment.");
    Json* crew=nullptr;for(auto& p:save_["crew"]) if(text(p,"id")==a) crew=&p;
    if(!crew || busy(a)) return reject("Crew member is busy.");
    if(spec->value("unique",false)) for(auto& p:save_["crew"]) if(text(p,"job")==job) p["job"]="";
    (*crew)["job"]=job;(*crew)["skill"]="Untrained";(*crew)["jobAt"]=job=="maintenance"?30:1;
  }
  else if(c=="social") {
    if(save_["crew"].size()<2 || busy("social") || number(save_["mess"],"rack")<2) return reject("Needs two crew, two rations and an idle recreation room.");
    Json pair=Json::array();for(const auto& p:save_["crew"]) if(!busy(text(p,"id")) && number(p,"health")>0 && pair.size()<2) pair.push_back(text(p,"id"));
    if(pair.size()<2) return reject("Two available crew needed.");
    add(save_["mess"],"rack",-2);pending="social";payload=pair;work=3;
  }
  else if(c=="hire") {
    const auto* p=find(catalog()["candidates"],a);
    if(!docked || !p || find(save_["crew"],a) || (p->contains("port") && text(*p,"port")!=text(save_["map"],"here")) || save_["crew"].size()>=static_cast<size_t>(std::stoi(text(save_["ship"],"berths"))) || number(s,"shiplets")<number(*p,"fee")) return reject("Candidate unavailable, no berth, or insufficient money.");
    add(s,"shiplets",-number(*p,"fee"));save_["crew"].push_back(*p);
  }
  else if(c=="ship" || c=="upgrade") {
    const int cost=c=="ship"?4000:a=="drive"?600:a=="bay"?500:800;
    if(!docked || number(s,"shiplets")<cost) return reject("Requires a station and "+std::to_string(cost)+" shiplets.");
    if(c=="ship") {
      if(text(s,"shipClass")=="kestrel") return reject("Already aboard the larger ship.");
      s["shipClass"]="kestrel";save_["ship"]=catalog()["largeShip"];save_["rooms"]=catalog()["largeRooms"];
    } else {
      if((a!="bay" && a!="bay2" && a!="drive") || s["upgrades"].value(a,false) || (a=="bay2" && !s["upgrades"].value("bay",false))) return reject("Refit unavailable or already fitted.");
      s["upgrades"][a]=true;
    }
    add(s,"shiplets",-cost);int level=s["upgrades"].value("bay2",false)?2:s["upgrades"].value("bay",false)?1:0;
    save_["cargo"]["capacity"]=text(s,"shipClass")=="wren"?(level==0?40:level==1?60:90):(level==0?60:level==1?90:120);
  }
  else if(c=="contract") {
    const auto* contract=find(catalog()["contracts"],a);if(!docked || !contract) return reject("Use a station mission board.");
    auto status=s["contracts"].value(a,std::string{});
    if(status=="ready") {add(s,"shiplets",number(*contract,"reward"));s["contracts"][a]="complete";}
    else if(status.empty()) {
      if(contract->contains("origin") && text(*contract,"origin")!=text(save_["map"],"here")) return reject("Accept at the contract's origin port.");
      if(text(*contract,"goal")=="freight") {
        if(cargoUsed()+reservedCargo+number(*contract,"vol")>number(save_["cargo"],"capacity")) return reject("Not enough cargo space.");
        save_["goods"].push_back({{"id","contract-"+a},{"name",text(*contract,"title")},{"vol",number(*contract,"vol")},{"value",0},{"kind","Contract freight"},{"contractId",a}});
      }
      if(text(*contract,"goal")=="passenger") {
        if(save_["crew"].size()+save_["state"]["passengers"].size()>=static_cast<size_t>(std::stoi(text(save_["ship"],"berths")))) return reject("No free passenger berth.");
        s["passengers"].push_back({{"id",a},{"name","Station passenger"}});
      }
      s["contracts"][a]="active";
    } else return reject("Contract is already active or collected.");
  }
  else if(c=="commission") {
    if(!docked || !s["commissions"]["active"].is_null()) return reject("Only one commission can be active; accept at a station.");
    if(a!="visit" && a!="scan" && a!="salvage") return reject("Unknown commission.");
    std::string destination;for(const auto& b:catalog()["bodies"]) if((a=="visit"?catalog()["ports"].contains(text(b,"id")):true) && text(b,"id")!=text(save_["map"],"here")) {destination=text(b,"id");break;}
    s["commissions"]["active"]={{"kind",a},{"target",destination},{"done",0},{"goal",a=="salvage"?2:1},{"reward",a=="visit"?450:a=="scan"?550:240}};
  }
  else if(c=="collect" || c=="abandon") {
    auto& active=s["commissions"]["active"];if(active.is_null()) return reject("No active commission.");
    if(c=="collect") {if(!docked || number(active,"done")<number(active,"goal")) return reject("Finish the commission and collect at a station.");add(s,"shiplets",number(active,"reward"));add(s["commissions"],"completed",1);}active=nullptr;
  }
  else if(c=="resolve") {
    auto& incidents=s["nativeIncidents"];auto it=std::find_if(incidents.begin(),incidents.end(),[&](const Json& i){return text(i,"id")==a;});
    if(it==incidents.end() || busy("incident-"+a) || stat("energy")<2) return reject("Choose an unresolved incident; needs two energy.");
    change("energy",-2);pending="incident-"+a;target=a;work=4;
  }
  else if(c=="broadcast") {
    auto here=text(save_["map"],"here");if(sim["broadcasts"].value(here,false)) return reject("Local report already collected.");
    sim["broadcasts"][here]=true;
    const auto* body=find(catalog()["bodies"],here);
    const auto* system=body?find(catalog()["systems"],bodySystem(*body)):nullptr;
    const std::string region=system?text(*system,"region"):"gal-inner";
    remember("History",catalog()["history"].at(region)[0].get<std::string>());
  }
  else if(c=="preserve" || c=="sell-signal") {
    if(text(sim,"signal")!="request") return reject("Follow the Silicon Marrow signal first.");
    if(c=="preserve") {
      if(text(save_["map"],"here")!="palimpsest-site" || number(s,"repairKits")<1 || stat("energy")<=5) return reject("Reach the Archive Hull with a repair kit and more than five energy.");
      add(s,"repairKits",-1);change("energy",-5);sim["signal"]="preserved";for(auto& p:save_["crew"]) p["morale"]=std::min(100,number(p,"morale")+5);
    } else {if(!docked) return reject("Sell at a station.");add(s,"shiplets",900);sim["signal"]="sold";for(auto& p:save_["crew"]) p["morale"]=std::max(0,number(p,"morale")-8);}
    remember("Choice",c=="preserve"?"The crew sheltered Silicon Marrow's archive.":"The signal was sold despite its request.");
  }
  else if(c=="claim") {
    const auto* award=find(catalog()["awards"],a);if(!award || !s["chronicle"]["earned"].value(a,false) || s["chronicle"]["claimed"].value(a,false)) return reject("Reward not earned or already claimed.");
    const auto& r=(*award)["reward"];add(s,"shiplets",r.value("cash",0));add(s,"fuelCells",r.value("fuel",0));add(s,"repairKits",r.value("kits",0));add(s,"medkits",r.value("med",0));s["chronicle"]["claimed"][a]=true;message_="Reward claimed.";return true;
  }
  else return reject("Unknown native command.");
  add(save_,"actions",1);
  auto& cds=s["actionCooldowns"];for(auto it=cds.begin();it!=cds.end();) {add(it.value(),"remaining",-1);if(number(it.value(),"remaining")<=0) it=cds.erase(it);else ++it;}
  tick();
  if(text(s,"gameOver").empty() && pending=="start-filters") {
    s["filterLife"]=number(save_["life"],"lifeMax");
    for(auto& slot:save_["life"]["slots"]) slot["state"]="active";
    add(s["chronicle"],"banks",1);
  } else if(text(s,"gameOver").empty() && !pending.empty()) task(pending,target,work,payload);
  if(tier(c)) cds[key]={{"remaining",tier(c)},{"tier",tier(c)}};
  if(c=="filters" && text(s,"gameOver").empty()) advanceWork(10);
  progress();message_="Committed: "+c+(a.empty()?"":" / "+a);return true;
}

void Voyage::commissionProgress(const std::string& kind,const std::string& target) {
  auto& c=save_["state"]["commissions"]["active"];
  if(!c.is_null() && text(c,"kind")==kind && (kind=="salvage" || text(c,"target")==target)) c["done"]=std::min(number(c,"goal"),number(c,"done")+1);
}
void Voyage::arrive(const std::string& target) {
  auto& s=save_["state"];save_["map"]["here"]=target;s["nativeVisited"][target]=true;
  s["docked"]=catalog()["ports"].contains(target);
  const auto* body=find(catalog()["bodies"],target);if(body) {
    const auto sys=bodySystem(*body);s["chronicle"]["systems"][sys]=true;
    const auto* system=find(catalog()["systems"],sys);if(system) s["chronicle"]["regions"][text(*system,"region")]=true;
  }
  commissionProgress("visit",target);remember("Arrival","Arrived at "+location()+".");
}
void Voyage::finish(const Json& t) {
  auto& s=save_["state"];auto& sim=s["sim"];const auto id=text(t,"id"),target=text(t,"target");
  if(id=="travel") arrive(target);
  else if(id=="scan") {
    s["scanned"][target]=true;const auto* b=find(catalog()["bodies"],target);
    save_["survey"].push_back({{"id","scan-"+target},{"name",b?text(*b,"name"):target},{"value",180},{"state","packaged"}});
    commissionProgress("scan",target);remember("Discovery","Scanned "+(b?text(*b,"name"):target)+".");
    if(target=="halvan" && text(sim,"signal")=="dormant") sim["signal"]="intercepted";
    if(target=="echo-site" && text(sim,"signal")=="coordinates") sim["signal"]="request";
  }
  else if(id=="repair") change("hull",25);
  else if(id=="maintain") s["engineCondition"]=100;
  else if(id=="cook") save_["mess"]["rack"]=std::min(number(save_["mess"],"capacity"),number(save_["mess"],"rack")+6);
  else if(id.rfind("install-",0)==0) {for(auto& slot:save_["life"]["slots"]) if(text(slot,"id")==target) slot["state"]="seated";}
  else if(id=="salvage") {
    if(cargoUsed()+5<=number(save_["cargo"],"capacity")) {
      add(s,"lotSerial",1);save_["goods"].push_back({{"id","salvage-"+std::to_string(number(s,"lotSerial"))},{"name","Recovered salvage"},{"vol",5},{"value",120},{"kind","Salvage"},{"risk",false}});commissionProgress("salvage","");
    }
  }
  else if(id.rfind("project-",0)==0) {
    const auto p=id.substr(8);add(s["chronicle"],"projects",1);
    if(p=="kit") add(s,"repairKits",1);
    if(p=="oxygen") save_["life"]["canisters"]=std::min(number(save_["life"],"capacity"),number(save_["life"],"canisters")+1);
    if(p=="fuel") add(s,"fuelCells",1);
    if(p=="food") save_["mess"]["rack"]=std::min(number(save_["mess"],"capacity"),number(save_["mess"],"rack")+8);
    if(p=="cooling") s["engineCondition"]=std::min(100,number(s,"engineCondition")+15);
    if(p=="brace") change("hull",15);
    if(p=="sensors") sim["sensor"]=2;
    if(p=="decode") sim["signal"]="coordinates";
    if(p=="observe") save_["survey"].push_back({{"id","observation-"+target},{"name","Observation: "+target},{"value",100},{"state","packaged"}});
    if(p=="dream") {sim["insight"]=std::min(5,number(sim,"insight")+1);add(s["chronicle"],"dreams",1);sim["dreams"].push_back(t["payload"]);}
    remember("Work","Completed "+p+".");
  }
  else if(id.rfind("rest-",0)==0 || id.rfind("medical-",0)==0) {
    for(auto& p:save_["crew"]) if(text(p,"id")==target) {
      if(id.rfind("rest-",0)==0) p["fatigue"]=std::max(0,number(p,"fatigue")-35);
      else p["health"]=std::min(100,number(p,"health")+30);
    }
    add(s["chronicle"],"projects",1);remember("Care","Completed care for "+target+".");
  }
  else if(id=="social") {
    for(auto& p:save_["crew"]) for(const auto& member:t["payload"]) if(text(p,"id")==member.get<std::string>()) p["morale"]=std::min(100,number(p,"morale")+8);
    auto first=t["payload"][0].get<std::string>(),second=t["payload"][1].get<std::string>();if(first>second) std::swap(first,second);
    auto key=first+"~"+second;sim["bonds"][key]=std::min(100,sim["bonds"].value(key,0)+5);add(s["chronicle"],"meals",1);add(s["chronicle"],"projects",1);remember("Bond","The crew shared a meal.");
  }
  else if(id.rfind("incident-",0)==0) {
    auto& incidents=s["nativeIncidents"];incidents.erase(std::remove_if(incidents.begin(),incidents.end(),[&](const Json& i){return text(i,"id")==target;}),incidents.end());add(s["chronicle"],"incidents",1);remember("Care","Incident resolved: "+target);
  }
}
void Voyage::advanceWork(int amount) {
  if(stat("energy")<=0 || !text(save_["state"],"gameOver").empty()) return;
  auto& tasks=save_["state"]["nativeTasks"];Json completed=Json::array();
  for(auto it=tasks.begin();it!=tasks.end();) {add(*it,"left",-amount);if(number(*it,"left")<=0) {completed.push_back(*it);it=tasks.erase(it);}else ++it;}
  for(const auto& t:completed) finish(t);
}
void Voyage::tick() {
  auto& s=save_["state"];add(s,"wearTick",1);if(number(s,"wearTick")>=4) {s["wearTick"]=0;s["engineCondition"]=std::max(0,number(s,"engineCondition")-1);}
  if(actions()%20==0) change("energy",-1);
  if(number(s,"filterLife")>0) {s["filterLife"]=std::max(0,number(s,"filterLife")-number(save_["life"],"decay"));if(number(s,"filterLife")==0) {for(auto& slot:save_["life"]["slots"]) slot["state"]="empty";s["airHold"]=20;}}
  else s["airHold"]=std::max(0,number(s,"airHold")-1);
  add(s,"crewTick",1);bool living=false;
  for(auto& p:save_["crew"]) {
    if(number(s,"airHold")==0 && number(s,"filterLife")==0 && actions()%5==0) p["health"]=std::max(0,number(p,"health")-1);
    add(p,"hungerTick",1);if(number(p,"hungerTick")>=25) {p["hungerTick"]=0;if(number(save_["mess"],"rack")>0) {add(save_["mess"],"rack",-1);p["hunger"]=std::max(0,number(p,"hunger")-40);}else p["hunger"]=std::min(100,number(p,"hunger")+15);}
    if(number(p,"hunger")>=90 && actions()%5==0) p["health"]=std::max(0,number(p,"health")-1);
    if(actions()%10==0) {p["fatigue"]=std::clamp(number(p,"fatigue")+(text(p,"job").empty()?-2:1),0,100);if(!text(p,"job").empty() && text(p,"skill")=="Untrained") p["morale"]=std::max(0,number(p,"morale")-1);}
    living=living || number(p,"health")>0;
    if(actions()%100==0 && !text(p,"job").empty()) {
      const auto* job=find(catalog()["jobs"],text(p,"job"));int wage=job?number(*job,"wage"):0;
      if(number(s,"shiplets")>=wage) {add(s,"shiplets",-wage);add(p,"wallet",wage);}else {p["job"]="";p["morale"]=std::max(0,number(p,"morale")-12);}
    }
    if(text(p,"job")=="cleaner" && !busy(text(p,"id")) && actions()%std::max(20,40-4*number(p,"jobAt"))==0) for(auto& other:save_["crew"]) other["morale"]=std::min(100,number(other,"morale")+1);
  }
  if(stat("hull")==0 || !living) {s["gameOver"]=stat("hull")==0?"The hull failed.":"All crew were lost.";s["nativeTasks"]=Json::array();return;}
  advanceWork(1);
  for(const auto& spec:catalog()["incidents"]) {
    const auto id=text(spec,"id");const int first=number(spec,"first"),every=number(spec,"every");
    if(actions()>=first && (actions()-first)%every==0 && !find(s["nativeIncidents"],id)) {s["nativeIncidents"].push_back({{"id",id},{"stage",0},{"left",number(spec["stages"][0],"ticks")}});remember("Incident",text(spec,"title"));}
  }
  auto& incidents=s["nativeIncidents"];
  for(auto it=incidents.begin();it!=incidents.end();) {
    const auto* spec=find(catalog()["incidents"],text(*it,"id"));add(*it,"left",-1);
    if(number(*it,"left")<=0 && spec) {
      add(*it,"stage",1);int stage=number(*it,"stage");
      if(stage>=static_cast<int>((*spec)["stages"].size())) {it=incidents.erase(it);continue;}
      const auto& effect=(*spec)["stages"][stage];for(auto name:{"hull","shield","energy"}) change(name,effect.value(name,0));
      for(auto& p:save_["crew"]) {p["morale"]=std::clamp(number(p,"morale")+effect.value("morale",0),0,100);p["health"]=std::clamp(number(p,"health")+effect.value("health",0),0,100);}
      (*it)["left"]=std::max(1,number(effect,"ticks"));
    } ++it;
  }
  // Autonomous jobs start AFTER existing work advances, matching the action boundary.
  for(const auto& p:save_["crew"]) if(number(p,"health")>0 && !busy(text(p,"id"))) {
    const auto job=text(p,"job");
    if(job=="maintenance" && number(s,"engineCondition")<=number(p,"jobAt") && !busy("maintain") && number(s,"repairKits")>0) {add(s,"repairKits",-1);task("maintain",text(p,"id"),4);}
    if(job=="kitchen" && number(save_["mess"],"rack")<=6 && !busy("cook") && !busy("project-food") && stat("energy")>3) {change("energy",-3);task("cook",text(p,"id"),4);}
  }
  if(stat("hull")==0) {s["gameOver"]="The hull failed.";s["nativeTasks"]=Json::array();}
}
int Voyage::metric(const std::string& name) const {
  const auto& s=save_["state"];const auto& j=s["chronicle"];
  if(name=="systems" || name=="regions") return static_cast<int>(j[name].size());
  if(name=="actions") return actions();
  if(name=="scans") return static_cast<int>(s["scanned"].size());
  if(name=="commissions") return number(s["commissions"],"completed");
  if(name=="contracts") {int n=0;for(const auto& c:s["contracts"]) if(c=="complete") ++n;return n;}
  if(name=="berth") return s["nativeVisited"].value("tolme",false)?1:0;
  if(name=="signal") return text(s["sim"],"signal")=="preserved" || text(s["sim"],"signal")=="sold"?1:0;
  return j.value(name,0);
}
void Voyage::progress() {
  auto& s=save_["state"];
  for(const auto& c:catalog()["contracts"]) {
    auto id=text(c,"id");if(s["contracts"].value(id,std::string{})!="active") continue;
    bool done=text(c,"goal")=="scan"?s["scanned"].value(text(c,"target"),false):s["nativeVisited"].value(text(c,"target"),false);
    if(done) {
      s["contracts"][id]="ready";
      auto& goods=save_["goods"];goods.erase(std::remove_if(goods.begin(),goods.end(),[&](const Json& g){return g.value("contractId",std::string{})==id;}),goods.end());
      auto& passengers=s["passengers"];passengers.erase(std::remove_if(passengers.begin(),passengers.end(),[&](const Json& p){return text(p,"id")==id;}),passengers.end());
    }
  }
  for(const auto& award:catalog()["awards"]) if(metric(text(award,"metric"))>=number(award,"goal")) s["chronicle"]["earned"][text(award,"id")]=true;
}
std::string Voyage::summary() const {
  const auto& s=save_["state"];std::ostringstream o;
  o<<text(save_["ship"],"name")<<"  |  "<<location()<<(s.value("docked",false)?" [DOCKED]":" [IN SPACE]")<<"\nAction "<<actions()<<"   Shiplets "<<number(s,"shiplets")<<"\n";
  for(const auto& st:save_["stats"]) o<<text(st,"label")<<" "<<number(st,"value")<<"%   ";
  o<<"\nDrive "<<number(s,"engineCondition")<<"%   Air "<<(number(s,"filterLife")>0?number(s,"filterLife")/4:number(s,"airHold"))<<" actions\nFuel cells "<<number(s,"fuelCells")<<"   Kits "<<number(s,"repairKits")<<"   Medkits "<<number(s,"medkits")<<"\nRations "<<number(save_["mess"],"rack")<<"   O2 canisters "<<number(save_["life"],"canisters")<<"   Cargo "<<cargoUsed()<<"/"<<number(save_["cargo"],"capacity")<<" m3\n";
  for(const auto& t:s["nativeTasks"]) o<<"\n"<<text(t,"id")<<" / "<<text(t,"target")<<": "<<number(t,"left")<<" actions left";
  if(!text(s,"gameOver").empty()) o<<"\nVOYAGE LOST: "<<text(s,"gameOver");
  return o.str();
}

void Voyage::validate(const Json& d) const {
  const auto shape=[](const auto& self,const Json& actual,const Json& model)->void {
    if(model.is_object()) {
      if(!actual.is_object()) throw std::runtime_error("Invalid save object.");
      for(auto it=model.begin();it!=model.end();++it) self(self,actual.at(it.key()),it.value());
    } else if(model.is_array()) {
      if(!actual.is_array()) throw std::runtime_error("Invalid save array.");
    } else if(model.is_number_integer()) {
      if(!actual.is_number_integer()) throw std::runtime_error("Invalid integer field.");
    } else if(model.is_number()) {
      if(!actual.is_number()) throw std::runtime_error("Invalid numeric field.");
    } else if(model.is_string() && !actual.is_string()) throw std::runtime_error("Invalid text field.");
    else if(model.is_boolean() && !actual.is_boolean()) throw std::runtime_error("Invalid boolean field.");
  };
  shape(shape,d,catalog()["initial"]);
  if(d.value("format",std::string{})!="spaceship-native-1") throw std::runtime_error("Not a native save. HTA saves are kept separate and are not overwritten.");
  if(!d.at("actions").is_number_integer() || d.at("actions").get<int>()<0 || d.at("actions").get<int>()>10000000) throw std::runtime_error("Invalid action counter.");
  for(auto key:{"crew","stats","goods","survey","rooms"}) if(!d.at(key).is_array()) throw std::runtime_error("Invalid save array.");
  if(!find(catalog()["bodies"],text(d.at("map"),"here"))) throw std::runtime_error("Unknown location.");
  const auto& s=d.at("state");
  if(text(s,"shipClass")!="wren" && text(s,"shipClass")!="kestrel") throw std::runtime_error("Unknown ship class.");
  for(auto key:{"shiplets","fuelCells","repairKits","medkits","engineCondition","filterLife","airHold","crewTick","wearTick","lotSerial"}) if(!s.at(key).is_number_integer() || s.at(key).get<int>()<0 || s.at(key).get<int>()>100000000) throw std::runtime_error("Invalid resource.");
  if(d["crew"].empty() || d["crew"].size()>14) throw std::runtime_error("Invalid crew count.");
  for(const auto& p:d["crew"]) for(auto key:{"health","morale","fatigue","hunger"}) if(!p.at(key).is_number_integer() || number(p,key)<0 || number(p,key)>100) throw std::runtime_error("Invalid crew condition.");
  for(auto id:{"hull","shield","energy","fuel"}) {const auto* st=find(d["stats"],id);if(!st || !st->at("value").is_number_integer() || number(*st,"value")<0 || number(*st,"value")>100) throw std::runtime_error("Invalid ship condition.");}
  if(!s.at("nativeTasks").is_array() || !s.at("nativeIncidents").is_array() || !s.at("nativeVisited").is_object() || !s.at("actionCooldowns").is_object()) throw std::runtime_error("Invalid task state.");
  if(s["nativeTasks"].size()>128 || s["nativeIncidents"].size()>catalog()["incidents"].size()) throw std::runtime_error("Too many tasks or incidents.");
  auto unique=[](const Json& list) {
    std::vector<std::string> seen;
    for(const auto& item:list) {auto id=item.at("id").get<std::string>();if(id.empty() || std::find(seen.begin(),seen.end(),id)!=seen.end()) throw std::runtime_error("Duplicate or empty ID.");seen.push_back(id);}
  };
  for(auto key:{"crew","stats","goods","rooms"}) unique(d.at(key));
  unique(s["nativeTasks"]);unique(s["nativeIncidents"]);unique(d["life"]["slots"]);
  for(const auto& ev:s["nativeIncidents"]) {
    const auto* spec=find(catalog()["incidents"],text(ev,"id"));
    if(!spec || number(ev,"stage")<0 || number(ev,"stage")>=static_cast<int>((*spec)["stages"].size()) || number(ev,"left")<1 || number(ev,"left")>1000) throw std::runtime_error("Invalid incident stage.");
  }
  if(d["rooms"].size()!=21 || d["stats"].size()!=4) throw std::runtime_error("Invalid ship structure.");
  const auto& rooms=text(s,"shipClass")=="wren"?catalog()["initial"]["rooms"]:catalog()["largeRooms"];
  for(const auto& r:d["rooms"]) {
    const auto* authored=find(rooms,text(r,"id"));if(!authored) throw std::runtime_error("Unknown compartment.");
    for(auto field:{"x","y","w","h"}) if(r.at(field)!=authored->at(field)) throw std::runtime_error("Invalid compartment geometry.");
  }
  if(number(s,"engineCondition")>100 || number(s,"airHold")>20 || number(s,"filterLife")>200 || number(s,"wearTick")>3) throw std::runtime_error("Invalid ship clock/condition.");
  if(number(d["life"],"capacity")!=10 || number(d["life"],"canisters")>10 || number(d["life"],"lifeMax")!=200 || number(d["life"],"decay")!=4) throw std::runtime_error("Invalid life support capacity.");
  if(number(d["mess"],"capacity")!=24 || number(d["mess"],"rack")<0 || number(d["mess"],"rack")>24) throw std::runtime_error("Invalid ration rack.");
  const auto& sim=s["sim"];
  if(number(sim,"insight")<0 || number(sim,"insight")>5 || number(sim,"sensor")<0 || number(sim,"sensor")>2) throw std::runtime_error("Invalid insight or calibration.");
  for(auto key:{"scanned","nativeVisited"}) for(const auto& value:s[key]) if(!value.is_boolean()) throw std::runtime_error("Invalid progress flag.");
  for(auto key:{"observed","broadcasts"}) for(const auto& value:sim[key]) if(!value.is_boolean()) throw std::runtime_error("Invalid simulation flag.");
  for(auto key:{"systems","regions","earned","claimed"}) for(const auto& value:s["chronicle"][key]) if(!value.is_boolean()) throw std::runtime_error("Invalid chronicle flag.");
  for(auto key:{"banks","projects","dreams","meals","incidents"}) if(number(s["chronicle"],key)<0) throw std::runtime_error("Invalid chronicle count.");
  for(const auto& item:s["contracts"]) if(!item.is_string() || (item!="active" && item!="ready" && item!="complete")) throw std::runtime_error("Invalid contract state.");
  for(const auto& m:sim["memories"]) {m.at("id").get<int>();m.at("processed").get<bool>();m.at("text").get<std::string>();m.at("kind").get<std::string>();}
  if(!s["commissions"]["active"].is_null()) {
    const auto& c=s["commissions"]["active"];auto kind=text(c,"kind");
    if((kind!="visit" && kind!="scan" && kind!="salvage") || number(c,"done")<0 || number(c,"done")>number(c,"goal") || number(c,"goal")!=(kind=="salvage"?2:1) || !find(catalog()["bodies"],text(c,"target"))) throw std::runtime_error("Invalid commission.");
  }
  for(const auto& t:s["nativeTasks"]) {
    const auto id=text(t,"id");bool known=id=="travel" || id=="scan" || id=="repair" || id=="maintain" || id=="cook" || id=="salvage" || id=="social";
    if(id.rfind("project-",0)==0) known=catalog()["projects"].contains(id.substr(8));
    if(id.rfind("install-",0)==0) known=find(d["life"]["slots"],text(t,"target"))!=nullptr;
    if(id.rfind("medical-",0)==0 || id.rfind("rest-",0)==0) known=find(d["crew"],text(t,"target"))!=nullptr;
    if(id.rfind("incident-",0)==0) known=find(catalog()["incidents"],text(t,"target"))!=nullptr;
    if(id=="social" && (!t.at("payload").is_array() || t["payload"].size()!=2 || t["payload"][0]==t["payload"][1])) throw std::runtime_error("Invalid social assignment.");
    if(id=="social") for(const auto& member:t["payload"]) if(!member.is_string() || !find(d["crew"],member.get<std::string>())) throw std::runtime_error("Unknown social participant.");
    if(!known || !t.at("left").is_number_integer() || number(t,"left")<1 || number(t,"left")>10000 || number(t,"total")<number(t,"left")) throw std::runtime_error("Invalid ongoing work.");
    if((id=="travel" || id=="scan") && !find(catalog()["bodies"],text(t,"target"))) throw std::runtime_error("Invalid work destination.");
  }
  for(const auto& cd:s["actionCooldowns"]) {int t=number(cd,"tier");if((t!=5 && t!=10 && t!=15 && t!=20) || number(cd,"remaining")<1 || number(cd,"remaining")>t) throw std::runtime_error("Invalid cooldown.");}
  if(!s.at("sim").is_object() || !s["sim"].at("memories").is_array() || s["sim"]["memories"].size()>32) throw std::runtime_error("Invalid memory archive.");
  for(auto key:{"canisters","capacity","lifeMax","decay"}) if(number(d.at("life"),key)<0) throw std::runtime_error("Invalid life support.");
  if(!d.at("life").at("slots").is_array() || d["life"]["slots"].size()!=3) throw std::runtime_error("Invalid filter rack.");
  for(const auto& slot:d["life"]["slots"]) {auto state=text(slot,"state");if(state!="empty" && state!="installing" && state!="seated" && state!="active") throw std::runtime_error("Invalid filter slot.");}
  for(const auto& g:d["goods"]) if(number(g,"vol")<0 || number(g,"value")<0) throw std::runtime_error("Invalid cargo.");
}
bool Voyage::save(const std::filesystem::path& path) const {
  try {
    validate(save_);auto temporary=path;temporary+=".tmp";
    {std::ofstream out(temporary,std::ios::binary|std::ios::trunc);out<<save_.dump(2);out.flush();if(!out) return false;}
    // Preserve an existing slot until the replacement has been fully written.
    auto backup=path;backup+=".bak";std::error_code ec;
    if(std::filesystem::exists(path)) {std::filesystem::copy_file(path,backup,std::filesystem::copy_options::overwrite_existing,ec);if(ec) return false;}
    std::filesystem::rename(temporary,path,ec);
    if(ec) {std::filesystem::remove(path,ec);ec.clear();std::filesystem::rename(temporary,path,ec);}
    return !ec;
  } catch(...) {return false;}
}
bool Voyage::load(const std::filesystem::path& path) {
  try {
    if(std::filesystem::file_size(path)>8*1024*1024) return reject("Save is too large.");
    std::ifstream in(path,std::ios::binary);Json value;in>>value;validate(value);
    // Exercise read-only views before replacement to reject incomplete state.
    Voyage candidate;candidate.save_=value;candidate.summary();candidate.controls("station","");
    save_=std::move(value);message_="Voyage loaded.";return true;
  } catch(const std::exception& e) {return reject(std::string("Load failed: ")+e.what());}
}
}
