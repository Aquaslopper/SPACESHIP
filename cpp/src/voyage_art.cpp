#include <algorithm>
#include <cmath>

#include "voyage.hpp"

namespace spaceship {
Json Voyage::roomIllustration(const std::string& room, const std::string& selection) const {
  const auto key = room == "crew" ? "quarters" : room;
  const auto& art = catalog()["machines"];
  if (!art.contains(key)) return nullptr;
  Json result = art[key];
  const auto& state = save_["state"];
  const int condition = state["engineCondition"];
  const int mealEvery = catalog()["constants"]["messEvery"];
  const auto tone = condition >= 75 ? "#7f9161" : condition >= 25 ? "#c08a3e" : "#b05a42";
  const auto word = condition >= 75   ? "NOMINAL"
                    : condition >= 50 ? "SERVICEABLE"
                    : condition >= 25 ? "DEGRADED"
                                      : "CRITICAL";
  for (auto& part : result["parts"]) {
    const auto id = part.value("id", std::string{});
    if (id == "eng-meter") {
      part["w"] = static_cast<int>(std::lround(420.0 * condition / 100));
      part["style"]["background"] = tone;
    }
    if (id == "eng-pct") part["text"] = std::to_string(condition) + "%";
    if (id == "eng-word") {
      part["text"] = word;
      part["style"]["color"] = tone;
    }
    if (part.value("classes", std::string{}).find("mach-plate") != std::string::npos)
      part["text"] = "DRIVE UNIT K-4 . SER " + save_["ship"].value("registry", std::string{}) +
                     "-E . BANK 1-16";
    for (int i = 0; i < 3; ++i) {
      const auto suffix = std::to_string(i);
      const auto status = save_["life"]["slots"][i].value("state", std::string{});
      if (id == "o2-can" + suffix || id == "o2-neck" + suffix) {
        part["style"]["display"] = status == "empty" || status == "installing" ? "none" : "block";
        if (status == "active" && id == "o2-can" + suffix) {
          part["style"]["background"] = "#dfeccd";
          part["style"]["border-color"] = "#7f9161";
        }
      }
      if (id == "o2-well" + suffix && (status == "empty" || status == "installing")) {
        part["style"]["background"] = "#fdfbf7";
        part["style"]["border"] = "1px dashed #cbb89c";
      }
      if (id == "o2-state" + suffix) {
        part["text"] = status == "empty" || status == "installing" ? "EMPTY"
                       : status == "seated"                        ? "INSTALLED"
                       : stat("energy") > 0                        ? "ACTIVE"
                                                                   : "NO POWER";
        part["style"]["color"] = status == "active"   ? "#7f9161"
                                 : status == "seated" ? "#8c6f4e"
                                                      : "#b05a42";
      }
      if (id == "o2-bar" + suffix)
        part["w"] = status == "active"
                        ? static_cast<int>(std::lround(120.0 * state["filterLife"].get<int>() /
                                                       save_["life"]["lifeMax"].get<int>()))
                        : 0;
    }
    const int rack = save_["mess"]["rack"], capacity = save_["mess"]["capacity"];
    if (id == "mess-bar") {
      part["w"] = static_cast<int>(std::lround(520.0 * rack / capacity));
      part["style"]["background"] = rack * 100 >= capacity * 60   ? "#7f9161"
                                    : rack * 100 >= capacity * 25 ? "#c08a3e"
                                                                  : "#b05a42";
    }
    if (id == "mess-readout")
      part["text"] =
          std::to_string(rack) + " of " + std::to_string(capacity) + " rations on the rack";
    for (size_t i = 0; i < save_["crew"].size(); ++i) {
      const auto& person = save_["crew"][i];
      const auto suffix = std::to_string(i);
      if (id == "mess-pname" + suffix) part["text"] = person.value("name", std::string{});
      if (id == "mess-ptick" + suffix) {
        part["w"] =
            static_cast<int>(std::lround(118.0 * person.value("hungerTick", 0) / mealEvery));
        part["style"]["background"] = person.value("hunger", 0) >= 70 ? "#b05a42" : "#7f9161";
      }
      if (id == "mess-psub" + suffix)
        part["text"] =
            std::to_string(mealEvery - person.value("hungerTick", 0)) + " actions to next meal";
    }
  }
  if (key == "o2") {
    const int stage = airStage();
    const auto& air = catalog()["air"][stage];
    const auto color = air.value("tone", std::string("#7f9161"));
    auto part = [&](double x, double y, double w, double h, const std::string& background,
                    const std::string& text = "", int font = 8) {
      result["parts"].push_back({{"id", ""},
                                 {"classes", "o2-air-detail"},
                                 {"x", x},
                                 {"y", y},
                                 {"w", w},
                                 {"h", h},
                                 {"text", text},
                                 {"runs", Json::array()},
                                 {"style",
                                  {{"background", background},
                                   {"color", color},
                                   {"font-size", std::to_string(font) + "px"}}}});
    };
    const bool running = state["filterLife"].get<int>() > 0 && stat("energy") > 0;
    const int holdMax = catalog()["constants"]["airHoldMax"];
    const int percent =
        running ? 100
                : static_cast<int>(std::lround(100.0 * state["airHold"].get<int>() / holdMax));
    part(28, 60, 110, 12, "", "CABIN AIR");
    part(420, 71, 150, 12, "",
         running ? std::to_string(state["filterLife"].get<int>()) + " filtered"
                 : std::to_string(state["airHold"].get<int>()) + " of " + std::to_string(holdMax) +
                       " left");
    part(420, 58, 150, 14, "", air.value("word", std::string{}), 11);
    part(28, 84, 544, 14, "#e6ddcd");
    part(29, 85, std::lround(544.0 * percent / 100), 12, color);
    for (int i = 1; i < 4; ++i) part(28 + std::lround(544.0 * i / 4), 84, 1, 14, "#fdfbf7");
    if (running)
      for (int i = 0; i < 4; ++i) part(80 + i * 130, 104, 72, 2, "#cfdcc0");
    else
      for (int i = 0; i < (4 - stage) * 4; ++i)
        part(34 + i * 33, 100 + (i % 3) * 5, 3, 3, "#b9a893");
  }
  if (key == "quarters") {
    const Json* person = nullptr;
    for (const auto& p : save_["crew"])
      if (p.value("id", std::string{}) == selection) person = &p;
    if (!person && !save_["crew"].empty()) person = &save_["crew"][0];
    if (person && art["figures"].contains(person->at("id").get<std::string>())) {
      const auto id = person->at("id").get<std::string>();
      const auto orders = state["pendingOrders"].value(id, Json::object());
      std::string mode = "job:" + orders.value("job", person->value("job", std::string{}));
      if (orders.value("rest", false)) mode = "rest";
      if (orders.value("learnOn", false) || orders.value("learnSt", false)) mode = "study";
      if (orders.value("doc", false) || busy("doctor-" + id) || busy("medical-" + id)) mode = "doc";
      const auto& figures = art["figures"][id];
      if (!figures.contains(mode)) mode = "default";
      for (const auto& pixel : figures[mode])
        result["parts"].push_back({{"id", ""},
                                   {"classes", "cq-px"},
                                   {"x", pixel[0]},
                                   {"y", pixel[1]},
                                   {"w", art["pixelSize"]},
                                   {"h", art["pixelSize"]},
                                   {"style", {{"background", pixel[2]}}},
                                   {"text", ""},
                                   {"runs", Json::array()}});
      result["figureMode"] = mode;
    }
  }
  return result;
}
}  // namespace spaceship
