#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include "json.hpp"

namespace spaceship {
using Json = nlohmann::json;
struct Choice {
  std::string id, label;
};
struct Control {
  std::string command, argument, label, reason;
  bool enabled;
};

// Native simulation. No browser, JavaScript interpreter or operating-system UI
// calls belong here. Both the Win32 client and regression tests use this API.
class Voyage {
 public:
  Voyage();
  static const Json& catalog();
  const Json& snapshot() const { return save_; }
  int stat(const std::string& id) const;
  int actions() const;
  int airStage() const;
  int crewWork(int base) const;
  Json navigationMetrics(const std::string& destination) const;
  Json commissionOffer(const std::string& kind) const;
  Json orderQuote() const;
  static std::string anchor(const Json& crew);
  std::string desire(const Json& crew) const;
  int cargoUsed() const;
  int cooldown(const std::string& key) const;
  std::string location() const;
  std::string summary() const;
  std::string describe(const std::string& page, const std::string& selection) const;
  std::vector<Choice> choices(const std::string& page) const;
  std::vector<Control> controls(const std::string& page, const std::string& selection) const;
  bool act(const std::string& command, const std::string& argument = "");
  const std::string& message() const { return message_; }
  bool save(const std::filesystem::path& path) const;
  bool load(const std::filesystem::path& path);

 private:
  Json save_;
  std::string message_;
  bool execute(const std::string& command, const std::string& argument);
  bool reject(const std::string& reason);
  std::vector<std::string> claims_;
  void tick(const std::string& initiating);
  void automate(const std::string& initiating);
  void tickRelationships();
  bool checkLoss();
  void applyOrders(const std::string& person, const Json& orders);
  void train(Json& person, int steps);
  int sheetCost(const Json& person, const Json& orders) const;
  void startBank();
  void contractProgress(const std::string& kind, const std::string& target);
  Json importLegacy(const Json& value) const;
  void advanceWork(int amount);
  void finish(const Json& task);
  void task(const std::string& id, const std::string& target, int work,
            const Json& payload = nullptr);
  bool busy(const std::string& id) const;
  void change(const std::string& stat, int amount);
  void remember(const std::string& kind, const std::string& text, const std::string& who = "",
                const std::string& other = "");
  void progress();
  void commissionProgress(const std::string& kind, const std::string& target);
  int metric(const std::string& name) const;
  void arrive(const std::string& target);
  void validate(const Json& value) const;
};
}  // namespace spaceship
