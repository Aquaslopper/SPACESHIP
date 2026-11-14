#include "core/Simulation.h"
#include "ui/NavScreen.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <nlohmann/json.hpp>
int main() {
 auto catalog=ship::Catalog::LoadFromAssets("assets");
 auto sim=ship::Simulation::NewVoyage(catalog);
 sim.MutableMap().here=sim.World().bodies.front().id;
 int courseControls=0;
 for (const auto& row : ship::BuildNavScreen(sim).screen.commands) {
   if (row.label=="HOLD" || row.label=="Change Course") {
     assert(row.enabled && !row.actionCost);
     ++courseControls;
   }
 }
 assert(courseControls==2);
 const ship::Body* destination=nullptr;
 for(const auto& b:sim.World().bodies) if(sim.InRange(b)&&!sim.AtTarget(b)&&sim.MetricsFor(b).dist>0) {destination=&b;break;}
 assert(destination);
 const auto home=sim.Map().here;
 const auto* origin=sim.FindBodyById(home);
 sim.SelectTarget(destination->id); assert(sim.StartTravel());
 sim.ProAction(0.5);
 auto legacy=nlohmann::ordered_json::parse(sim.SaveJson());
 legacy["map"].erase("inSpace");
 legacy["map"].erase("flightOrigin");
 auto legacyLoaded=ship::Simulation::NewVoyage(catalog);
 std::string legacyError;
 assert(legacyLoaded.LoadJson(legacy.dump(), &legacyError));
 assert(legacyLoaded.Map().inSpace);
 assert(legacyLoaded.FindOngoing("travel")->left==sim.FindOngoing("travel")->left);
 const auto legacyWave=ship::ComputeRadiationTelemetry(legacyLoaded);
 (void)legacyWave;
 for (const auto& layer : {"system", "sector", "galaxy"}) {
   legacyLoaded.SetLayer(layer);
   const auto legacyScreen=ship::BuildNavScreen(legacyLoaded);
   (void)legacyScreen;
 }
 assert(legacyLoaded.HoldTravel());
 auto sameFlight=sim;
 assert(sameFlight.HoldTravel());
 assert(legacyLoaded.Map().flightOrigin==sameFlight.Map().flightOrigin);
 std::cout<<"PASS: legacy in-flight save startup telemetry, all navigation layers, retained progress and HOLD position\n";
 const auto* trip=sim.FindOngoing("travel");
 const double part=(trip->total-trip->left)/trip->total;
 const double x=origin->x+(destination->x-origin->x)*part;
 const double y=origin->y+(destination->y-origin->y)*part;
 assert(ship::PressNavAction(sim,"hold"));
 assert(!sim.FindOngoing("travel")); assert(sim.Map().inSpace);
 assert(std::abs(sim.Map().flightOrigin.at("system").first-x)<1e-9);
 assert(std::abs(sim.Map().flightOrigin.at("system").second-y)<1e-9);
 auto position=sim.Map().flightOrigin;
 std::string error;
 assert(sim.SaveToFile("build/hold-save.json", false, &error));
 auto loaded=ship::Simulation::NewVoyage(catalog);
 if (!loaded.LoadFromFile("build/hold-save.json", &error)) { std::cerr<<error<<"\n"; return 1; }
 assert(loaded.Map().inSpace);
 assert(loaded.Map().flightOrigin==position);
 assert(!loaded.FindOngoing("travel"));
 loaded.SelectTarget(home);
 assert(loaded.StartTravel());
 assert(loaded.Map().flightOrigin==position);
 assert(loaded.SaveToFile("build/flight-save.json", true, &error));
 auto flying=ship::Simulation::NewVoyage(catalog);
 if (!flying.LoadFromFile("build/flight-save.json", &error)) { std::cerr<<error<<"\n"; return 1; }
 assert(flying.Map().inSpace);
 assert(flying.Map().flightOrigin==position);
 assert(flying.FindOngoing("travel")->left==loaded.FindOngoing("travel")->left);
 loaded.ProAction(0.25); flying.ProAction(0.25);
 assert(loaded.HoldTravel()); assert(flying.HoldTravel());
 assert(loaded.Map().flightOrigin==flying.Map().flightOrigin);
 std::cout<<"PASS: HOLD save/load, departure after load, UTF-16 flight save/load and identical subsequent HOLD position\n";
 sim.PerformAction("wait"); assert(position==sim.Map().flightOrigin);
 sim.SelectTarget(home); assert(!sim.AtTarget(*origin));
 assert(ship::PressNavAction(sim,"change-course"));
 assert(sim.FindOngoing("travel")->target==home);
 assert(position==sim.Map().flightOrigin);
 sim.PerformAction("return flight");
 assert(ship::PressNavAction(sim,"hold"));
 assert(position!=sim.Map().flightOrigin);
 sim.SelectTarget(destination->id); assert(sim.StartTravel());
 sim.SelectTarget(home); assert(ship::PressNavAction(sim,"change-course"));
 assert(sim.FindOngoing("travel")->target==home);
 sim.FindStat("fuel")->value=1;
 sim.PerformAction("last fuel");
 assert(sim.State().gameOver=="Adrift with no fuel.");
 std::cout<<"PASS: exact HOLD position, stationary wait, return course, repeated HOLD, in-flight course change, Lost in Space\n";
 auto jump=ship::Simulation::NewVoyage(catalog);
 jump.MutableMap().here=jump.World().bodies.front().id;
 for(const auto& b:jump.World().systems) if(jump.InRange(b)&&!jump.AtTarget(b)) {jump.SelectTarget(b.id);break;}
 assert(jump.StartTravel()); jump.PerformAction("jump");
 assert(jump.HoldTravel());
 auto at=jump.Map().flightOrigin;
 assert(jump.SaveToFile("build/jump-hold-save.json", false, &error));
 auto jumpLoaded=ship::Simulation::NewVoyage(catalog);
 assert(jumpLoaded.LoadFromFile("build/jump-hold-save.json", &error));
 assert(jumpLoaded.Map().inSpace);
 assert(jumpLoaded.Map().flightOrigin==at);
 assert(jumpLoaded.StartTravel());
 assert(jumpLoaded.Map().flightOrigin==at);
 jump.PerformAction("wait"); assert(at==jump.Map().flightOrigin);
 assert(jump.StartTravel());
 std::cout<<"PASS: inter-system HOLD and restart\n";
}
