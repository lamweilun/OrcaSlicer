#include "Spool.hpp"

#include "Utils.hpp"

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <ctime>

#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
#endif

namespace Slic3r {

static const char *spool_usage_status_name(SpoolUsageStatus status)
{
    switch (status) {
    case SpoolUsageStatus::Pending:   return "pending";
    case SpoolUsageStatus::Confirmed: return "confirmed";
    case SpoolUsageStatus::Refunded:  return "refunded";
    }
    return "pending";
}

static SpoolUsageStatus spool_usage_status_from_name(const std::string &name)
{
    if (name == "confirmed") return SpoolUsageStatus::Confirmed;
    if (name == "refunded")  return SpoolUsageStatus::Refunded;
    return SpoolUsageStatus::Pending;
}

static std::int64_t spool_now()
{
    return static_cast<std::int64_t>(std::time(nullptr));
}

static std::string spool_generate_id()
{
    boost::uuids::uuid uuid = boost::uuids::random_generator()();
    return boost::uuids::to_string(uuid);
}

float SpoolRecord::remaining_m() const
{
    float grams = remaining_g();
    if (grams <= 0.0f || density <= 0.0f || diameter_mm <= 0.0f)
        return 0.0f;
    // volume in mm^3, cross-section in mm^2, result in meters
    float volume_mm3 = grams / density * 1000.0f;
    float area_mm2   = float(M_PI) * 0.25f * diameter_mm * diameter_mm;
    return volume_mm3 / area_mm2 / 1000.0f;
}

float SpoolRecord::remaining_pct() const
{
    if (initial_weight_g <= 0.0f)
        return -1.0f;
    float pct = remaining_g() / initial_weight_g * 100.0f;
    return std::max(0.0f, std::min(100.0f, pct));
}

std::string spool_slot_key(const std::string &dev_id, const std::string &ams_id, const std::string &slot_id)
{
    return dev_id + "|" + ams_id + "|" + slot_id;
}

const SpoolRecord *SpoolLedger::get_record(const std::string &id) const
{
    for (const SpoolRecord &record : records)
        if (record.id == id)
            return &record;
    return nullptr;
}

SpoolRecord *SpoolLedger::get_record(const std::string &id)
{
    for (SpoolRecord &record : records)
        if (record.id == id)
            return &record;
    return nullptr;
}

SpoolRecord &SpoolLedger::add_record(SpoolRecord record)
{
    if (record.id.empty())
        record.id = spool_generate_id();
    std::int64_t now = spool_now();
    if (record.created_at == 0)
        record.created_at = now;
    record.updated_at = now;
    records.push_back(std::move(record));
    return records.back();
}

bool SpoolLedger::update_record(const SpoolRecord &record)
{
    SpoolRecord *existing = get_record(record.id);
    if (!existing)
        return false;
    *existing = record;
    existing->updated_at = spool_now();
    return true;
}

bool SpoolLedger::remove_record(const std::string &id)
{
    bool removed = false;
    for (auto it = records.begin(); it != records.end(); ++it) {
        if (it->id == id) {
            records.erase(it);
            removed = true;
            break;
        }
    }
    if (!removed)
        return false;
    for (auto it = usage_log.begin(); it != usage_log.end();) {
        if (it->spool_id == id)
            it = usage_log.erase(it);
        else
            ++it;
    }
    for (auto it = slot_assignments.begin(); it != slot_assignments.end();) {
        if (it->second == id)
            it = slot_assignments.erase(it);
        else
            ++it;
    }
    return true;
}

void SpoolLedger::assign_slot(const std::string &dev_id, const std::string &ams_id, const std::string &slot_id, const std::string &spool_id)
{
    if (spool_id.empty()) {
        unassign_slot(dev_id, ams_id, slot_id);
        return;
    }
    if (!get_record(spool_id))
        return;
    slot_assignments[spool_slot_key(dev_id, ams_id, slot_id)] = spool_id;
}

void SpoolLedger::unassign_slot(const std::string &dev_id, const std::string &ams_id, const std::string &slot_id)
{
    slot_assignments.erase(spool_slot_key(dev_id, ams_id, slot_id));
}

std::string SpoolLedger::spool_for_slot(const std::string &dev_id, const std::string &ams_id, const std::string &slot_id) const
{
    auto it = slot_assignments.find(spool_slot_key(dev_id, ams_id, slot_id));
    return it == slot_assignments.end() ? std::string() : it->second;
}

std::vector<std::pair<std::string, std::string>> SpoolLedger::assignments_for_spool(const std::string &spool_id) const
{
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto &assignment : slot_assignments)
        if (assignment.second == spool_id)
            out.emplace_back(assignment.first, assignment.second);
    return out;
}

SpoolUsageEntry SpoolLedger::record_usage(const std::string &spool_id, float used_g, int extruder_id,
    const std::string &project_name, const std::string &dev_id, const std::string &dev_name, SpoolUsageStatus status)
{
    SpoolUsageEntry entry;
    entry.id           = spool_generate_id();
    entry.spool_id     = spool_id;
    entry.timestamp    = spool_now();
    entry.used_g       = used_g;
    entry.extruder_id  = extruder_id;
    entry.project_name = project_name;
    entry.dev_id       = dev_id;
    entry.dev_name     = dev_name;
    entry.status       = status;
    usage_log.push_back(entry);
    touch_record(spool_id);
    SpoolRecord *record = get_record(spool_id);
    if (record)
        record->used_g += used_g;
    return entry;
}

bool SpoolLedger::set_entry_status(const std::string &entry_id, SpoolUsageStatus status)
{
    for (SpoolUsageEntry &entry : usage_log) {
        if (entry.id != entry_id || entry.status == status)
            continue;
        bool was_refunded = entry.status == SpoolUsageStatus::Refunded;
        bool is_refunded  = status == SpoolUsageStatus::Refunded;
        entry.status = status;
        if (was_refunded == is_refunded)
            return true; // pending <-> confirmed keeps the deduction
        SpoolRecord *record = get_record(entry.spool_id);
        if (record) {
            record->used_g += is_refunded ? -entry.used_g : entry.used_g;
            if (record->used_g < 0.0f)
                record->used_g = 0.0f;
        }
        return true;
    }
    return false;
}

std::vector<SpoolUsageEntry> SpoolLedger::usage_for_spool(const std::string &spool_id) const
{
    std::vector<SpoolUsageEntry> out;
    for (const SpoolUsageEntry &entry : usage_log)
        if (entry.spool_id == spool_id)
            out.push_back(entry);
    return out;
}

size_t SpoolLedger::confirm_pending_for_device(const std::string &dev_id)
{
    size_t count = 0;
    for (SpoolUsageEntry &entry : usage_log)
        if (entry.dev_id == dev_id && entry.status == SpoolUsageStatus::Pending) {
            entry.status = SpoolUsageStatus::Confirmed;
            ++count;
        }
    return count;
}

size_t SpoolLedger::refund_pending_for_device(const std::string &dev_id)
{
    size_t count = 0;
    for (SpoolUsageEntry &entry : usage_log) {
        if (entry.dev_id != dev_id || entry.status != SpoolUsageStatus::Pending)
            continue;
        entry.status = SpoolUsageStatus::Refunded;
        SpoolRecord *record = get_record(entry.spool_id);
        if (record) {
            record->used_g -= entry.used_g;
            if (record->used_g < 0.0f)
                record->used_g = 0.0f;
        }
        ++count;
    }
    return count;
}

const SpoolRecord *SpoolLedger::find_by_tag(const std::string &tag_uid) const
{
    if (tag_uid.empty())
        return nullptr;
    for (const SpoolRecord &record : records)
        if (!record.tag_uid.empty() && record.tag_uid == tag_uid)
            return &record;
    return nullptr;
}

const SpoolRecord *SpoolLedger::find_candidate(const std::string &filament_settings_id, const std::string &filament_type, const std::string &color) const
{
    const SpoolRecord *type_color_match = nullptr;
    const SpoolRecord *type_match       = nullptr;
    for (const SpoolRecord &record : records) {
        if (!filament_settings_id.empty() && record.filament_settings_id == filament_settings_id)
            return &record;
        if (!filament_type.empty() && record.filament_type == filament_type) {
            bool color_match = !color.empty() && !record.color.empty() && record.color == color;
            if (color_match && !type_color_match)
                type_color_match = &record;
            if (!type_match)
                type_match = &record;
        }
    }
    return type_color_match ? type_color_match : type_match;
}

void SpoolLedger::touch_record(const std::string &spool_id)
{
    SpoolRecord *record = get_record(spool_id);
    if (record)
        record->updated_at = spool_now();
}

std::string SpoolLedger::default_path()
{
    return (boost::filesystem::path(data_dir()) / "spool_ledger.json").string();
}

bool SpoolLedger::load(const std::string &path)
{
    std::string file_path = path.empty() ? default_path() : path;
    boost::nowide::ifstream file(file_path.c_str());
    if (!file.good())
        return false;

    nlohmann::json j = nlohmann::json::parse(file, nullptr, false, true);
    if (j.is_discarded() || !j.is_object()) {
        BOOST_LOG_TRIVIAL(error) << "SpoolLedger: failed to parse " << file_path << ", starting empty";
        return false;
    }

    records.clear();
    usage_log.clear();
    slot_assignments.clear();

    int version = j.value("version", 1);
    if (version > kCurrentVersion)
        BOOST_LOG_TRIVIAL(warning) << "SpoolLedger: file version " << version << " is newer than supported " << kCurrentVersion;
    // Migration hook: convert older layouts here when the version grows.

    if (j.contains("records") && j["records"].is_array()) {
        for (const auto &r : j["records"]) {
            if (!r.is_object())
                continue;
            SpoolRecord record;
            record.id                   = r.value("id", std::string());
            record.name                 = r.value("name", std::string());
            record.vendor               = r.value("vendor", std::string());
            record.filament_type        = r.value("filament_type", std::string());
            record.filament_settings_id = r.value("filament_settings_id", std::string());
            record.color                = r.value("color", std::string());
            record.diameter_mm          = r.value("diameter_mm", 1.75f);
            record.density              = r.value("density", 1.24f);
            record.initial_weight_g     = r.value("initial_weight_g", 0.0f);
            record.used_g               = r.value("used_g", 0.0f);
            record.tag_uid              = r.value("tag_uid", std::string());
            record.notes                = r.value("notes", std::string());
            record.created_at           = r.value("created_at", (std::int64_t) 0);
            record.updated_at           = r.value("updated_at", (std::int64_t) 0);
            if (record.id.empty())
                record.id = spool_generate_id();
            records.push_back(std::move(record));
        }
    }

    if (j.contains("usage_log") && j["usage_log"].is_array()) {
        for (const auto &e : j["usage_log"]) {
            if (!e.is_object())
                continue;
            SpoolUsageEntry entry;
            entry.id           = e.value("id", std::string());
            entry.spool_id     = e.value("spool_id", std::string());
            entry.timestamp    = e.value("timestamp", (std::int64_t) 0);
            entry.used_g       = e.value("used_g", 0.0f);
            entry.extruder_id  = e.value("extruder_id", -1);
            entry.project_name = e.value("project_name", std::string());
            entry.dev_id       = e.value("dev_id", std::string());
            entry.dev_name     = e.value("dev_name", std::string());
            entry.status       = spool_usage_status_from_name(e.value("status", std::string("pending")));
            if (entry.id.empty())
                continue;
            usage_log.push_back(std::move(entry));
        }
    }

    if (j.contains("slot_assignments") && j["slot_assignments"].is_object()) {
        for (auto it = j["slot_assignments"].begin(); it != j["slot_assignments"].end(); ++it)
            if (it.value().is_string() && !it.value().get<std::string>().empty())
                slot_assignments[it.key()] = it.value().get<std::string>();
    }

    // Drop assignments pointing at removed records.
    for (auto it = slot_assignments.begin(); it != slot_assignments.end();) {
        if (!get_record(it->second))
            it = slot_assignments.erase(it);
        else
            ++it;
    }

    return true;
}

bool SpoolLedger::save(const std::string &path) const
{
    std::string file_path = path.empty() ? default_path() : path;

    nlohmann::json j;
    j["version"] = kCurrentVersion;
    j["records"] = nlohmann::json::array();
    for (const SpoolRecord &record : records) {
        nlohmann::json r;
        r["id"]                   = record.id;
        r["name"]                 = record.name;
        r["vendor"]               = record.vendor;
        r["filament_type"]        = record.filament_type;
        r["filament_settings_id"] = record.filament_settings_id;
        r["color"]                = record.color;
        r["diameter_mm"]          = record.diameter_mm;
        r["density"]              = record.density;
        r["initial_weight_g"]     = record.initial_weight_g;
        r["used_g"]               = record.used_g;
        r["tag_uid"]              = record.tag_uid;
        r["notes"]                = record.notes;
        r["created_at"]           = record.created_at;
        r["updated_at"]           = record.updated_at;
        j["records"].push_back(std::move(r));
    }
    j["usage_log"] = nlohmann::json::array();
    for (const SpoolUsageEntry &entry : usage_log) {
        nlohmann::json e;
        e["id"]           = entry.id;
        e["spool_id"]     = entry.spool_id;
        e["timestamp"]    = entry.timestamp;
        e["used_g"]       = entry.used_g;
        e["extruder_id"]  = entry.extruder_id;
        e["project_name"] = entry.project_name;
        e["dev_id"]       = entry.dev_id;
        e["dev_name"]     = entry.dev_name;
        e["status"]       = spool_usage_status_name(entry.status);
        j["usage_log"].push_back(std::move(e));
    }
    j["slot_assignments"] = slot_assignments;

    try {
        std::string path_pid = file_path + "." + std::to_string(get_current_pid());
        {
            boost::nowide::ofstream out(path_pid.c_str(), std::ios::binary | std::ios::trunc);
            if (!out.good()) {
                BOOST_LOG_TRIVIAL(error) << "SpoolLedger: failed to open " << path_pid << " for writing";
                return false;
            }
            out << j.dump(2);
            out.close();
        }
        boost::filesystem::rename(path_pid, file_path);
    } catch (const std::exception &ex) {
        BOOST_LOG_TRIVIAL(error) << "SpoolLedger: failed to save " << file_path << ": " << ex.what();
        return false;
    }
    return true;
}

} // namespace Slic3r
