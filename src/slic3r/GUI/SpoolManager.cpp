#include "SpoolManager.hpp"

#include <wx/app.h>

#include <boost/log/trivial.hpp>

wxDEFINE_EVENT(EVT_SPOOL_LEDGER_CHANGED, wxCommandEvent);

namespace Slic3r {
namespace GUI {

SpoolManager &SpoolManager::instance()
{
    static SpoolManager manager;
    return manager;
}

SpoolManager::SpoolManager()
{
    // Load failures (missing or corrupt file) leave a usable empty ledger.
    m_ledger.load();
    BOOST_LOG_TRIVIAL(info) << "SpoolManager: loaded " << m_ledger.records.size() << " spool records";
}

void SpoolManager::changed()
{
    m_ledger.save();
    wxQueueEvent(this, new wxCommandEvent(EVT_SPOOL_LEDGER_CHANGED));
}

std::vector<SpoolRecord> SpoolManager::records() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_ledger.records;
}

bool SpoolManager::get_record(const std::string &id, SpoolRecord &out) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const SpoolRecord *record = m_ledger.get_record(id);
    if (!record)
        return false;
    out = *record;
    return true;
}

bool SpoolManager::add_record(SpoolRecord record)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_ledger.add_record(std::move(record));
    changed();
    return true;
}

bool SpoolManager::update_record(const SpoolRecord &record)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    bool updated = m_ledger.update_record(record);
    if (updated)
        changed();
    return updated;
}

bool SpoolManager::remove_record(const std::string &id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    bool removed = m_ledger.remove_record(id);
    if (removed)
        changed();
    return removed;
}

void SpoolManager::assign_slot(const std::string &dev_id, const std::string &ams_id, const std::string &slot_id, const std::string &spool_id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_ledger.assign_slot(dev_id, ams_id, slot_id, spool_id);
    changed();
}

void SpoolManager::unassign_slot(const std::string &dev_id, const std::string &ams_id, const std::string &slot_id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_ledger.unassign_slot(dev_id, ams_id, slot_id);
    changed();
}

std::string SpoolManager::spool_for_slot(const std::string &dev_id, const std::string &ams_id, const std::string &slot_id) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_ledger.spool_for_slot(dev_id, ams_id, slot_id);
}

void SpoolManager::record_print_usage(const std::string &dev_id, const std::string &dev_name,
    const std::string &project_name, const std::vector<FilamentInfo> &filaments)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t recorded = 0;
    for (const FilamentInfo &filament : filaments) {
        if (filament.used_g <= 0.0f)
            continue;
        // Only trays with a persisted assignment are deducted; the ams/slot ids
        // are empty for virtual trays, which therefore never deduct here.
        std::string spool_id = m_ledger.spool_for_slot(dev_id, filament.ams_id, filament.slot_id);
        if (spool_id.empty())
            continue;
        m_ledger.record_usage(spool_id, filament.used_g, filament.id, project_name, dev_id, dev_name, SpoolUsageStatus::Pending);
        BOOST_LOG_TRIVIAL(info) << "SpoolManager: deducted " << filament.used_g << " g of usage on spool " << spool_id
                                << " (dev " << dev_id << ", extruder " << filament.id << ")";
        ++recorded;
    }
    if (recorded > 0)
        changed();
}

size_t SpoolManager::confirm_device(const std::string &dev_id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t count = m_ledger.confirm_pending_for_device(dev_id);
    if (count > 0)
        changed();
    return count;
}

size_t SpoolManager::refund_device(const std::string &dev_id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t count = m_ledger.refund_pending_for_device(dev_id);
    if (count > 0) {
        BOOST_LOG_TRIVIAL(info) << "SpoolManager: refunded " << count << " pending usage entries for dev " << dev_id;
        changed();
    }
    return count;
}

bool SpoolManager::set_entry_status(const std::string &entry_id, SpoolUsageStatus status)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    bool updated = m_ledger.set_entry_status(entry_id, status);
    if (updated)
        changed();
    return updated;
}

void SpoolManager::record_manual_usage(const std::string &spool_id, float used_g, const std::string &project_name)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (used_g <= 0.0f || !m_ledger.get_record(spool_id))
        return;
    m_ledger.record_usage(spool_id, used_g, -1, project_name, std::string(), std::string(), SpoolUsageStatus::Confirmed);
    changed();
}

std::vector<SpoolUsageEntry> SpoolManager::usage_for_spool(const std::string &spool_id) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_ledger.usage_for_spool(spool_id);
}

bool SpoolManager::find_by_tag(const std::string &tag_uid, SpoolRecord &out) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const SpoolRecord *record = m_ledger.find_by_tag(tag_uid);
    if (!record)
        return false;
    out = *record;
    return true;
}

bool SpoolManager::find_candidate(const std::string &filament_settings_id, const std::string &filament_type,
    const std::string &color, SpoolRecord &out) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const SpoolRecord *record = m_ledger.find_candidate(filament_settings_id, filament_type, color);
    if (!record)
        return false;
    out = *record;
    return true;
}

SpoolLedger SpoolManager::snapshot() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_ledger;
}

bool SpoolManager::save()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_ledger.save();
}

} // namespace GUI
} // namespace Slic3r
