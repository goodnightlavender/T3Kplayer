// PresetStore.h — CRUD facade over the `presets` table.
//
// Shares the SQLite connection with LibraryDb (we run on the same
// library.db file). State serialization is nlohmann/json; the column
// holds a TEXT blob.
//
// Active preset id is persisted in meta_kv('active_preset_id') so the
// preset pill restores its label on next launch.
//
// Singleton. On first call we ensure a "Default Setting" row exists
// (id 1, empty PresetState, active by default).

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "PresetState.h"

namespace t3k::library {

class PresetStore {
 public:
  struct PresetRow {
    int64_t     id;
    std::string name;
    bool        active;
  };

  static PresetStore& instance();

  // Ordered alphabetically (case-insensitive) by name.
  std::vector<PresetRow> list();

  // Read a preset's serialized state. Returns nullopt for unknown ids
  // or malformed blobs.
  std::optional<PresetState> load(int64_t presetId);

  // Overwrite the currently-active preset's state. Returns its id, or
  // 0 if no active preset exists.
  int64_t saveCurrent(const PresetState& state);

  // Create a new preset row. If `name` collides with an existing row
  // (case-insensitive), updates that row in place and returns its id.
  int64_t saveAs(const std::string& name, const PresetState& state);

  void rename(int64_t presetId, const std::string& newName);
  void remove(int64_t presetId);

  int64_t activeId() const;
  void    setActiveId(int64_t id);

 private:
  PresetStore();
  ~PresetStore() = default;
  PresetStore(const PresetStore&) = delete;
  PresetStore& operator=(const PresetStore&) = delete;

  // Make sure a "Default Setting" row + the active_preset_id meta_kv
  // entry exist. Idempotent.
  void ensureDefaults();
};

}  // namespace t3k::library
