#ifndef slic3r_SpoolManager_hpp_
#define slic3r_SpoolManager_hpp_

#include "libslic3r/Spool.hpp"
#include "libslic3r/ProjectTask.hpp"

#include <mutex>
#include <string>
#include <vector>

#include <wx/event.h>

// Raised (queued on the main thread) after every ledger mutation so UI surfaces
// can refresh. Observers bind directly on the manager:
//   wxGetApp().getSpoolManager()->Bind(EVT_SPOOL_LEDGER_CHANGED, ...);
wxDECLARE_EVENT(EVT_SPOOL_LEDGER_CHANGED, wxCommandEvent);

namespace Slic3r {
namespace GUI {

// Process-wide owner of the persistent spool ledger. All access is serialized
// internally, so it is safe to call from the main thread and from network/job
// worker threads alike. Every mutation is persisted immediately and announces
// itself via EVT_SPOOL_LEDGER_CHANGED.
class SpoolManager : public wxEvtHandler
{
public:
    static SpoolManager &instance();

    // Records. Getters return copies so callers never touch shared state.
    std::vector<SpoolRecord> records() const;
    bool get_record(const std::string &id, SpoolRecord &out) const;
    bool add_record(SpoolRecord record); // record.id is filled when empty
    bool update_record(const SpoolRecord &record);
    bool remove_record(const std::string &id);

    // Slot assignments (dev_id + ams_id + slot_id -> spool).
    void assign_slot(const std::string &dev_id, const std::string &ams_id, const std::string &slot_id, const std::string &spool_id);
    void unassign_slot(const std::string &dev_id, const std::string &ams_id, const std::string &slot_id);
    std::string spool_for_slot(const std::string &dev_id, const std::string &ams_id, const std::string &slot_id) const;

    // Deduct the estimated per-extruder usage of a print that was just sent.
    // Only extruders whose mapped tray (ams_id/slot_id) has a persisted spool
    // assignment are deducted; unmapped or virtual trays are skipped.
    void record_print_usage(const std::string &dev_id, const std::string &dev_name,
        const std::string &project_name, const std::vector<FilamentInfo> &filaments);
    size_t confirm_device(const std::string &dev_id); // print finished
    size_t refund_device(const std::string &dev_id);  // print cancelled/failed or upload failed
    bool set_entry_status(const std::string &entry_id, SpoolUsageStatus status);
    // Manual usage entry recorded directly as Confirmed (manager dialog).
    void record_manual_usage(const std::string &spool_id, float used_g, const std::string &project_name);
    std::vector<SpoolUsageEntry> usage_for_spool(const std::string &spool_id) const;

    // Matching for auto-linking a device tray to a ledger spool.
    bool find_by_tag(const std::string &tag_uid, SpoolRecord &out) const;
    bool find_candidate(const std::string &filament_settings_id, const std::string &filament_type,
        const std::string &color, SpoolRecord &out) const;

    // Consistent view for management UIs.
    SpoolLedger snapshot() const;

    bool save();

private:
    SpoolManager();

    void changed(); // persist + announce; call with m_mutex held

    mutable std::mutex m_mutex;
    SpoolLedger m_ledger;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_SpoolManager_hpp_
