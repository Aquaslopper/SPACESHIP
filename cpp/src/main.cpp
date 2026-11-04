#include "voyage.hpp"
#include <iostream>
#include <sstream>
#include <string>

// Portable text client for exactly the same simulation used by the native GUI.
int main() {
  spaceship::Voyage game;
  std::cout<<"SPACESHIP COMMAND - native C++\nType help for controls.\n"<<game.summary()<<'\n';
  std::string line;
  while(std::cout<<"> " && std::getline(std::cin,line)) {
    std::istringstream input(line);std::string command,argument;input>>command;std::getline(input,argument);auto first=argument.find_first_not_of(" \t");if(first==std::string::npos) argument.clear();else argument.erase(0,first);
    if(command=="quit" || command=="exit") break;
    if(command=="help") {std::cout<<"status | watch | view <page> | choices <page> | commands <page>\n"
      <<"travel <body-id> | jump <system-id> | scan <body-id> | fuel\n"
      <<"install s0/s1/s2 | filters | cook | repair | maintain\n"
      <<"project <kit/oxygen/fuel/food/cooling/brace/observe/dream/decode/sensors>\n"
      <<"dock | undock | salvage | buy <fuel/kit/med/oxygen/rations/cargo-id>\n"
      <<"sell <lot-id> | contract <id> | commission <visit/scan/salvage> | collect\n"
      <<"rest <crew-id> | medical <crew-id> | talk <crew-id> | train <crew-id>\n"
      <<"assign-<job-id> <crew-id> | social | hire <id> | ship | upgrade <bay/bay2/drive>\n"
      <<"resolve <incident-id> | claim <award-id> | preserve | sell-signal\n"
      <<"save <path> | load <path> | quit\nPages: home navigation crew cargo station science events rewards guide, or a room ID.\n";continue;}
    if(command=="status") {std::cout<<game.summary()<<'\n';continue;}
    if(command=="choices") {for(const auto& c:game.choices(argument)) std::cout<<c.id<<" / "<<c.label<<'\n';continue;}
    if(command=="commands") {auto entries=game.choices(argument);for(const auto& c:game.controls(argument,entries.empty()?"":entries[0].id)) std::cout<<c.command<<' '<<c.argument<<" / "<<c.label<<(c.enabled?"":" ["+c.reason+"]")<<'\n';continue;}
    if(command=="view") {auto entries=game.choices(argument);std::cout<<game.describe(argument,entries.empty()?"":entries[0].id)<<'\n';continue;}
    if(command=="save") {std::cout<<(game.save(argument)?"Saved.":"Save failed.")<<'\n';continue;}
    if(command=="load") {game.load(argument);std::cout<<game.message()<<'\n';continue;}
    if(command.empty()) continue;
    game.act(command,argument);std::cout<<game.message()<<'\n'<<game.summary()<<'\n';
  }
}
