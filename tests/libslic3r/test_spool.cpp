#include <catch2/catch_all.hpp>

#include "libslic3r/Spool.hpp"
#include "test_utils.hpp"

#include <boost/nowide/fstream.hpp>

#include <cmath>

using namespace Slic3r;

namespace {

SpoolRecord make_spool(const std::string &name, const std::string &type = "PLA", const std::string &color = "#FF0000")
{
    SpoolRecord record;
    record.name             = name;
    record.vendor           = "Generic";
    record.filament_type    = type;
    record.color            = color;
    record.diameter_mm      = 1.75f;
    record.density          = 1.24f;
    record.initial_weight_g = 1000.0f;
    return record;
}

} // namespace

TEST_CASE("Remaining grams subtract cumulative usage", "[Spool]") {
    SpoolRecord record = make_spool("test");
    REQUIRE(record.used_g == 0.0f);
    record.used_g = 250.0f;
    REQUIRE_THAT(record.remaining_g(), Catch::Matchers::WithinAbs(750.0f, 1e-4));
}

TEST_CASE("Remaining meters derive from weight density and diameter", "[Spool]") {
    SpoolRecord record = make_spool("test");
    // 1000 g / 1.24 g/cm3 = 806451.6 mm3; cross-section = pi * 0.875^2 mm2
    float expected_m = 1000.0f / 1.24f * 1000.0f / (float(M_PI) * 0.875f * 0.875f) / 1000.0f;
    REQUIRE_THAT(record.remaining_m(), Catch::Matchers::WithinRel(expected_m, 1e-3));

    SECTION("an empty spool has no remaining meters") {
        record.used_g = record.initial_weight_g;
        REQUIRE(record.remaining_m() == 0.0f);
    }
}

TEST_CASE("Remaining percent clamps to the full range and reports unset weight", "[Spool]") {
    SpoolRecord record = make_spool("test");
    REQUIRE_THAT(record.remaining_pct(), Catch::Matchers::WithinAbs(100.0f, 1e-4));
    record.used_g = 500.0f;
    REQUIRE_THAT(record.remaining_pct(), Catch::Matchers::WithinAbs(50.0f, 1e-4));
    record.used_g = 2000.0f;
    REQUIRE(record.remaining_pct() == 0.0f);
    record.initial_weight_g = 0.0f;
    REQUIRE(record.remaining_pct() == -1.0f);
}

TEST_CASE("Adding a spool assigns identity and timestamps", "[Spool]") {
    SpoolLedger ledger;
    SpoolRecord &added = ledger.add_record(make_spool("red"));
    REQUIRE_FALSE(added.id.empty());
    REQUIRE(added.created_at != 0);
    REQUIRE(added.updated_at >= added.created_at);
    REQUIRE(ledger.records.size() == 1);

    SECTION("a provided id is preserved") {
        SpoolRecord named = make_spool("named");
        named.id          = "my-id";
        REQUIRE(ledger.add_record(named).id == "my-id");
        REQUIRE(ledger.records.size() == 2);
    }
}

TEST_CASE("Updating a spool refreshes it in place", "[Spool]") {
    SpoolLedger ledger;
    SpoolRecord &added = ledger.add_record(make_spool("red"));

    SECTION("matching id updates and restamps") {
        SpoolRecord edited = added;
        edited.name        = "red (open)";
        std::int64_t before = added.updated_at;
        REQUIRE(ledger.update_record(edited));
        REQUIRE(ledger.get_record(added.id)->name == "red (open)");
        REQUIRE(ledger.get_record(added.id)->updated_at >= before);
    }

    SECTION("an unknown id fails") {
        SpoolRecord ghost = make_spool("ghost");
        ghost.id          = "no-such-id";
        REQUIRE_FALSE(ledger.update_record(ghost));
    }
}

TEST_CASE("Removing a spool drops its usage entries and assignments", "[Spool]") {
    SpoolLedger ledger;
    SpoolRecord &spool = ledger.add_record(make_spool("red"));
    std::string id     = spool.id;
    ledger.assign_slot("dev1", "0", "1", id);
    ledger.record_usage(id, 100.0f, 0, "proj", "dev1", "Dev One");

    REQUIRE(ledger.remove_record(id));
    REQUIRE(ledger.get_record(id) == nullptr);
    REQUIRE(ledger.usage_log.empty());
    REQUIRE(ledger.slot_assignments.empty());
    REQUIRE_FALSE(ledger.remove_record(id));
}

TEST_CASE("Slot assignments bind one spool per printer slot", "[Spool]") {
    SpoolLedger ledger;
    SpoolRecord &spool = ledger.add_record(make_spool("red"));
    SpoolRecord &other = ledger.add_record(make_spool("blue", "PETG", "#0000FF"));
    REQUIRE(ledger.spool_for_slot("dev1", "0", "1").empty());

    SECTION("assigning stores the binding") {
        ledger.assign_slot("dev1", "0", "1", spool.id);
        REQUIRE(ledger.spool_for_slot("dev1", "0", "1") == spool.id);
    }

    SECTION("re-assigning overwrites") {
        ledger.assign_slot("dev1", "0", "1", spool.id);
        ledger.assign_slot("dev1", "0", "1", other.id);
        REQUIRE(ledger.spool_for_slot("dev1", "0", "1") == other.id);
    }

    SECTION("unknown spools are not assigned") {
        ledger.assign_slot("dev1", "0", "2", "missing-id");
        REQUIRE(ledger.spool_for_slot("dev1", "0", "2").empty());
    }

    SECTION("an empty spool id unassigns") {
        ledger.assign_slot("dev1", "0", "1", spool.id);
        ledger.assign_slot("dev1", "0", "1", "");
        REQUIRE(ledger.spool_for_slot("dev1", "0", "1").empty());
    }

    SECTION("bindings are per device") {
        ledger.assign_slot("dev1", "0", "1", spool.id);
        ledger.assign_slot("dev2", "0", "1", other.id);
        REQUIRE(ledger.spool_for_slot("dev2", "0", "1") == other.id);
        REQUIRE(ledger.spool_for_slot("dev1", "0", "1") == spool.id);
    }

    SECTION("assignments for a spool list every bound slot") {
        ledger.assign_slot("dev1", "0", "1", spool.id);
        ledger.assign_slot("dev1", "0", "2", spool.id);
        ledger.assign_slot("dev1", "0", "3", other.id);
        REQUIRE(ledger.assignments_for_spool(spool.id).size() == 2);
        REQUIRE(ledger.assignments_for_spool(other.id).size() == 1);
    }
}

TEST_CASE("Recording usage deducts from the spool and logs an entry", "[Spool]") {
    SpoolLedger ledger;
    SpoolRecord &spool = ledger.add_record(make_spool("red"));

    SpoolUsageEntry entry = ledger.record_usage(spool.id, 120.0f, 0, "benchy", "dev1", "Dev One");
    REQUIRE(entry.status == SpoolUsageStatus::Pending);
    REQUIRE(entry.spool_id == spool.id);
    REQUIRE_THAT(entry.used_g, Catch::Matchers::WithinAbs(120.0f, 1e-4));
    REQUIRE(ledger.get_record(spool.id)->used_g != 0.0f);
    REQUIRE_THAT(ledger.get_record(spool.id)->used_g, Catch::Matchers::WithinAbs(120.0f, 1e-4));
    REQUIRE(ledger.usage_log.size() == 1);
    REQUIRE(ledger.usage_for_spool(spool.id).size() == 1);
}

TEST_CASE("Recording usage for an unknown spool logs but does not crash", "[Spool]") {
    SpoolLedger ledger;
    ledger.add_record(make_spool("red"));
    ledger.record_usage("missing-id", 50.0f, 0, "proj", "dev1", "Dev One");
    REQUIRE(ledger.usage_log.size() == 1);
    REQUIRE_THAT(ledger.get_record(ledger.records[0].id)->used_g, Catch::Matchers::WithinAbs(0.0f, 1e-6));
}

TEST_CASE("Refunding an entry undoes its deduction while confirming keeps it", "[Spool]") {
    SpoolLedger ledger;
    SpoolRecord &spool = ledger.add_record(make_spool("red"));

    SpoolUsageEntry confirmed = ledger.record_usage(spool.id, 120.0f, 0, "benchy", "dev1", "Dev One");
    REQUIRE(ledger.set_entry_status(confirmed.id, SpoolUsageStatus::Confirmed));
    REQUIRE_THAT(ledger.get_record(spool.id)->used_g, Catch::Matchers::WithinAbs(120.0f, 1e-4));

    SpoolUsageEntry refunded = ledger.record_usage(spool.id, 80.0f, 1, "benchy", "dev1", "Dev One");
    REQUIRE(ledger.set_entry_status(refunded.id, SpoolUsageStatus::Refunded));
    REQUIRE_THAT(ledger.get_record(spool.id)->used_g, Catch::Matchers::WithinAbs(120.0f, 1e-4));

    SECTION("refunding twice is a no-op") {
        REQUIRE(ledger.set_entry_status(refunded.id, SpoolUsageStatus::Refunded));
        REQUIRE_THAT(ledger.get_record(spool.id)->used_g, Catch::Matchers::WithinAbs(120.0f, 1e-4));
    }

    SECTION("un-refunding re-applies the deduction") {
        REQUIRE(ledger.set_entry_status(refunded.id, SpoolUsageStatus::Pending));
        REQUIRE_THAT(ledger.get_record(spool.id)->used_g, Catch::Matchers::WithinAbs(200.0f, 1e-4));
    }

    SECTION("an unknown entry fails") {
        REQUIRE_FALSE(ledger.set_entry_status("no-such-entry", SpoolUsageStatus::Refunded));
    }
}

TEST_CASE("A corrected balance never goes negative through refunds", "[Spool]") {
    SpoolLedger ledger;
    SpoolRecord &spool = ledger.add_record(make_spool("red"));
    SpoolUsageEntry entry = ledger.record_usage(spool.id, 120.0f, 0, "benchy", "dev1", "Dev One");
    ledger.get_record(spool.id)->used_g = 10.0f; // simulate a manual correction
    REQUIRE(ledger.set_entry_status(entry.id, SpoolUsageStatus::Refunded));
    REQUIRE(ledger.get_record(spool.id)->used_g == 0.0f);
}

TEST_CASE("Confirming by device only touches that device's pending entries", "[Spool]") {
    SpoolLedger ledger;
    SpoolRecord &spool = ledger.add_record(make_spool("red"));
    ledger.record_usage(spool.id, 10.0f, 0, "a", "dev1", "Dev One");
    ledger.record_usage(spool.id, 20.0f, 0, "b", "dev1", "Dev One");
    ledger.record_usage(spool.id, 30.0f, 0, "c", "dev2", "Dev Two");

    REQUIRE(ledger.confirm_pending_for_device("dev1") == 2);
    REQUIRE(ledger.usage_log[0].status == SpoolUsageStatus::Confirmed);
    REQUIRE(ledger.usage_log[1].status == SpoolUsageStatus::Confirmed);
    REQUIRE(ledger.usage_log[2].status == SpoolUsageStatus::Pending);
    REQUIRE_THAT(ledger.get_record(spool.id)->used_g, Catch::Matchers::WithinAbs(60.0f, 1e-4));

    SECTION("confirming again finds nothing pending") {
        REQUIRE(ledger.confirm_pending_for_device("dev1") == 0);
    }

    SECTION("refunding the other device undoes only its deduction") {
        REQUIRE(ledger.refund_pending_for_device("dev2") == 1);
        REQUIRE_THAT(ledger.get_record(spool.id)->used_g, Catch::Matchers::WithinAbs(30.0f, 1e-4));
    }

    SECTION("refunding a confirmed device is a no-op") {
        REQUIRE(ledger.refund_pending_for_device("dev1") == 0);
        REQUIRE_THAT(ledger.get_record(spool.id)->used_g, Catch::Matchers::WithinAbs(60.0f, 1e-4));
    }
}

TEST_CASE("Tag matching finds the spool with the same RFID identity", "[Spool]") {
    SpoolLedger ledger;
    SpoolRecord &tagged = ledger.add_record(make_spool("red"));
    tagged.tag_uid      = "AA:BB:CC";
    ledger.add_record(make_spool("blue", "PETG", "#0000FF"));

    REQUIRE(ledger.find_by_tag("AA:BB:CC") == &tagged);
    REQUIRE(ledger.find_by_tag("nope") == nullptr);
    REQUIRE(ledger.find_by_tag("") == nullptr);
}

TEST_CASE("Candidate matching prefers preset id then type plus color then type", "[Spool]") {
    SpoolLedger ledger;
    SpoolRecord &red_pla = ledger.add_record(make_spool("red", "PLA", "#FF0000"));
    ledger.add_record(make_spool("blue", "PETG", "#0000FF"));

    SECTION("exact preset id wins over everything") {
        SpoolRecord &with_id   = ledger.add_record(make_spool("preset", "PLA", "#00FF00"));
        with_id.filament_settings_id = "preset-123";
        REQUIRE(ledger.find_candidate("preset-123", "PLA", "#FF0000") == &with_id);
    }

    SECTION("type and color beat type alone") {
        REQUIRE(ledger.find_candidate("", "PLA", "#FF0000") == &red_pla);
    }

    SECTION("type alone is still a candidate") {
        REQUIRE(ledger.find_candidate("", "PLA", "#FFFFFF") == &red_pla);
    }

    SECTION("no plausible match returns null") {
        REQUIRE(ledger.find_candidate("", "TPU", "#FF0000") == nullptr);
        REQUIRE(ledger.find_candidate("", "", "") == nullptr);
    }
}

TEST_CASE("Saving and loading round-trips the whole ledger", "[Spool]") {
    ScopedTemporaryDir temp_dir;
    std::string path = (temp_dir.path() / "ledger.json").string();

    std::string spool_id;
    std::string entry_id;
    {
        SpoolLedger ledger;
        SpoolRecord &spool = ledger.add_record(make_spool("red"));
        spool.tag_uid      = "AA:BB:CC";
        spool_id           = spool.id;
        SpoolUsageEntry entry = ledger.record_usage(spool.id, 42.0f, 0, "benchy", "dev1", "Dev One");
        entry_id              = entry.id;
        ledger.assign_slot("dev1", "0", "1", spool.id);
        REQUIRE(ledger.save(path));
    }

    SpoolLedger loaded;
    REQUIRE(loaded.load(path));
    REQUIRE(loaded.records.size() == 1);
    REQUIRE(loaded.records[0].id == spool_id);
    REQUIRE(loaded.records[0].name == "red");
    REQUIRE(loaded.records[0].tag_uid == "AA:BB:CC");
    REQUIRE_THAT(loaded.records[0].used_g, Catch::Matchers::WithinAbs(42.0f, 1e-4));
    REQUIRE(loaded.usage_log.size() == 1);
    REQUIRE(loaded.usage_log[0].id == entry_id);
    REQUIRE(loaded.usage_log[0].status == SpoolUsageStatus::Pending);
    REQUIRE(loaded.spool_for_slot("dev1", "0", "1") == spool_id);
}

TEST_CASE("Loading a missing file yields an empty ledger", "[Spool]") {
    ScopedTemporaryDir temp_dir;
    std::string path = (temp_dir.path() / "missing.json").string();
    SpoolLedger ledger;
    REQUIRE_FALSE(ledger.load(path));
    REQUIRE(ledger.records.empty());
}

TEST_CASE("Loading a corrupt file yields an empty ledger instead of throwing", "[Spool]") {
    ScopedTemporaryDir temp_dir;
    std::string path = (temp_dir.path() / "ledger.json").string();
    {
        boost::nowide::ofstream out(path.c_str());
        out << "{ this is not json";
    }
    SpoolLedger ledger;
    REQUIRE_FALSE(ledger.load(path));
    REQUIRE(ledger.records.empty());
}

TEST_CASE("Loading tolerates partial entries and cleans dangling assignments", "[Spool]") {
    ScopedTemporaryDir temp_dir;
    std::string path = (temp_dir.path() / "ledger.json").string();
    {
        boost::nowide::ofstream out(path.c_str());
        out << "{"
               "\"version\": 1,"
               "\"records\": [{\"id\": \"abc\", \"name\": \"partial\"}, \"garbage\"],"
               "\"usage_log\": [{\"spool_id\": \"abc\", \"used_g\": 5.0}],"
               "\"slot_assignments\": {\"dev1|0|1\": \"abc\", \"dev1|0|2\": \"ghost\"}"
               "}";
    }
    SpoolLedger ledger;
    REQUIRE(ledger.load(path));
    REQUIRE(ledger.records.size() == 1);
    REQUIRE(ledger.records[0].name == "partial");
    REQUIRE(ledger.usage_log.empty()); // no id -> dropped
    REQUIRE(ledger.spool_for_slot("dev1", "0", "1") == "abc");
    REQUIRE(ledger.spool_for_slot("dev1", "0", "2").empty()); // ghost cleaned up
}
