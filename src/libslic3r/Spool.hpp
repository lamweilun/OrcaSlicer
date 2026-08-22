#ifndef slic3r_Spool_hpp_
#define slic3r_Spool_hpp_

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r {

// A physical filament spool tracked by the local inventory ledger.
// Weights are grams, diameter in mm, density in g/cm^3.
struct SpoolRecord
{
    std::string id;                    // stable uuid
    std::string name;                  // user label
    std::string vendor;
    std::string filament_type;         // e.g. "PLA"
    std::string filament_settings_id;  // filament preset matching key (optional)
    std::string color;                 // "#RRGGBB" or "#RRGGBBAA"
    float diameter_mm{1.75f};
    float density{1.24f};              // g/cm^3
    float initial_weight_g{1000.0f};   // net filament weight of a full spool
    float used_g{0.0f};                // cumulative deducted usage
    std::string tag_uid;               // RFID / NFC tag identity when known
    std::string notes;
    std::int64_t created_at{0};        // unix seconds
    std::int64_t updated_at{0};        // unix seconds

    float remaining_g() const { return initial_weight_g - used_g; }
    // Remaining filament length in meters derived from weight, density and diameter.
    float remaining_m() const;
    // 0..100, or -1 when the initial weight is not set.
    float remaining_pct() const;
};

enum class SpoolUsageStatus {
    Pending,    // estimated deduction, print outcome unknown
    Confirmed,  // print finished
    Refunded,   // print cancelled/failed, usage returned
};

struct SpoolUsageEntry
{
    std::string id;
    std::string spool_id;
    std::int64_t timestamp{0};         // unix seconds
    float used_g{0.0f};
    int extruder_id{-1};
    std::string project_name;
    std::string dev_id;
    std::string dev_name;              // display only
    SpoolUsageStatus status{SpoolUsageStatus::Pending};
};

// Stable key identifying a printer slot: device + multi-material unit + tray.
std::string spool_slot_key(const std::string &dev_id, const std::string &ams_id, const std::string &slot_id);

// Persistent spool inventory: records, per-deduction usage log and slot assignments.
// Not thread-safe by itself; callers must serialize access (see GUI SpoolManager).
class SpoolLedger
{
public:
    static constexpr int kCurrentVersion = 1;

    std::vector<SpoolRecord> records;
    std::vector<SpoolUsageEntry> usage_log;
    // slot key -> spool id
    std::map<std::string, std::string> slot_assignments;

    // Records.
    const SpoolRecord *get_record(const std::string &id) const;
    SpoolRecord *get_record(const std::string &id);
    SpoolRecord &add_record(SpoolRecord record);          // fills id/timestamps when empty
    bool update_record(const SpoolRecord &record);        // match by id, refreshes updated_at
    bool remove_record(const std::string &id);            // drops its usage entries and assignments

    // Slot assignments.
    void assign_slot(const std::string &dev_id, const std::string &ams_id, const std::string &slot_id, const std::string &spool_id);
    void unassign_slot(const std::string &dev_id, const std::string &ams_id, const std::string &slot_id);
    // Returns the assigned spool id or an empty string.
    std::string spool_for_slot(const std::string &dev_id, const std::string &ams_id, const std::string &slot_id) const;
    // All slots currently bound to a spool: pairs of (dev_id, ams_id, slot_id) encoded as slot keys.
    std::vector<std::pair<std::string, std::string>> assignments_for_spool(const std::string &spool_id) const;

    // Usage bookkeeping. record_usage bumps the spool's used_g and appends a log entry.
    SpoolUsageEntry record_usage(const std::string &spool_id, float used_g, int extruder_id,
        const std::string &project_name, const std::string &dev_id, const std::string &dev_name,
        SpoolUsageStatus status = SpoolUsageStatus::Pending);
    // Transitions adjust the spool's used_g so refunds undo their deduction.
    bool set_entry_status(const std::string &entry_id, SpoolUsageStatus status);
    std::vector<SpoolUsageEntry> usage_for_spool(const std::string &spool_id) const;
    // Confirm/refund every Pending entry recorded for a device (print finished/cancelled).
    size_t confirm_pending_for_device(const std::string &dev_id);
    size_t refund_pending_for_device(const std::string &dev_id);

    // Matching helpers for auto-linking device trays to ledger spools.
    const SpoolRecord *find_by_tag(const std::string &tag_uid) const;
    // Best heuristic candidate: exact preset id, then type+color, then type. nullptr when nothing plausible.
    const SpoolRecord *find_candidate(const std::string &filament_settings_id, const std::string &filament_type, const std::string &color) const;

    // Persistence. Empty path defaults to data_dir()/spool_ledger.json.
    static std::string default_path();
    // Tolerant load: missing file yields false and leaves an empty ledger; corrupt content is logged and skipped.
    bool load(const std::string &path = std::string());
    // Atomic write via PID-suffixed temp file + rename.
    bool save(const std::string &path = std::string()) const;

private:
    void touch_record(const std::string &spool_id);
};

} // namespace Slic3r

#endif // slic3r_Spool_hpp_
