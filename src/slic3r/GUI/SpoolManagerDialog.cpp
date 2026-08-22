#include "SpoolManagerDialog.hpp"

#include "SpoolManager.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MsgDialog.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/TextInput.hpp"

#include <wx/button.h>
#include <wx/clrpicker.h>
#include <wx/dataview.h>
#include <wx/datetime.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/variant.h>

#include <boost/log/trivial.hpp>

#include <ctime>

namespace Slic3r {
namespace GUI {

namespace {

wxString spool_status_text(SpoolUsageStatus status)
{
    switch (status) {
    case SpoolUsageStatus::Pending:   return _L("Pending");
    case SpoolUsageStatus::Confirmed: return _L("Confirmed");
    case SpoolUsageStatus::Refunded:  return _L("Refunded");
    }
    return _L("Pending");
}

wxString spool_date_text(std::int64_t unix_seconds)
{
    wxDateTime date((time_t) unix_seconds);
    if (!date.IsValid())
        return "-";
    return date.Format("%Y-%m-%d %H:%M");
}

wxString spool_color_text(const std::string &color)
{
    if (color.empty())
        return "-";
    return wxString::FromUTF8(color);
}

} // namespace

// ---------------------------------------------------------------- SpoolEditDialog

SpoolEditDialog::SpoolEditDialog(wxWindow *parent, const SpoolRecord &record, bool is_new)
    : DPIDialog(parent, wxID_ANY, is_new ? _L("Add Spool") : _L("Edit Spool"), wxDefaultPosition,
                wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
    , m_record(record)
    , m_is_new(is_new)
{
    auto *sizer = new wxBoxSizer(wxVERTICAL);
    auto *grid  = new wxFlexGridSizer(0, 2, FromDIP(6), FromDIP(10));
    grid->AddGrowableCol(1, 1);

    auto add_row = [&](const wxString &label, wxWindow *control) {
        auto *text = new wxStaticText(this, wxID_ANY, label, wxDefaultPosition, wxDefaultSize);
        text->SetFont(::Label::Body_13);
        grid->Add(text, 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(control, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);
    };

    auto make_input = [&](const wxString &value, const wxString &tooltip = wxString()) {
        auto *input = new TextInput(this, value, wxEmptyString, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
        input->GetTextCtrl()->SetMinSize(wxSize(FromDIP(220), -1));
        if (!tooltip.empty())
            input->SetToolTip(tooltip);
        return input;
    };

    m_name    = make_input(GUI::from_u8(m_record.name));
    add_row(_L("Name"), m_name);

    m_vendor  = make_input(GUI::from_u8(m_record.vendor));
    add_row(_L("Vendor"), m_vendor);

    m_type = new wxComboBox(this, wxID_ANY, GUI::from_u8(m_record.filament_type), wxDefaultPosition, wxDefaultSize, 0, nullptr, wxCB_DROPDOWN);
    for (const wxString &type : {wxString("PLA"), wxString("PETG"), wxString("ABS"), wxString("ASA"), wxString("TPU"), wxString("PA"), wxString("PC"), wxString("PLA-AERO"), wxString("PETG-AERO"), wxString("HIPS"), wxString("PVA"), wxString("BVOH")})
        m_type->Append(type);
    m_type->SetValue(GUI::from_u8(m_record.filament_type));
    add_row(_L("Filament Type"), m_type);

    m_color = new wxColourPickerCtrl(this, wxID_ANY,
        m_record.color.empty() ? wxColour(255, 255, 255) : wxColour(GUI::from_u8(m_record.color)));
    add_row(_L("Color"), m_color);

    m_diameter = make_input(wxString::Format("%.2f", (double) m_record.diameter_mm), _L("Filament diameter in mm"));
    add_row(_L("Diameter (mm)"), m_diameter);

    m_density = make_input(wxString::Format("%.2f", (double) m_record.density), _L("Filament density in g/cm³"));
    add_row(_L("Density (g/cm³)"), m_density);

    m_initial_weight = make_input(wxString::Format("%.0f", (double) m_record.initial_weight_g), _L("Net filament weight of a full spool"));
    add_row(_L("Initial Weight (g)"), m_initial_weight);

    m_used = make_input(wxString::Format("%.0f", (double) m_record.used_g), _L("Already used filament weight"));
    add_row(_L("Used (g)"), m_used);

    m_tag_uid = make_input(GUI::from_u8(m_record.tag_uid), _L("RFID/NFC tag identity for auto-matching"));
    add_row(_L("Tag UID"), m_tag_uid);

    m_notes = make_input(GUI::from_u8(m_record.notes));
    add_row(_L("Notes"), m_notes);

    m_diameter->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    m_density->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    m_initial_weight->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    m_used->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));

    sizer->Add(grid, 0, wxEXPAND | wxALL, FromDIP(12));

    auto *buttons = new wxBoxSizer(wxHORIZONTAL);
    auto *btn_ok     = new wxButton(this, wxID_OK, _L("OK"));
    auto *btn_cancel = new wxButton(this, wxID_CANCEL, _L("Cancel"));
    buttons->Add(btn_ok, 0, wxRIGHT, FromDIP(8));
    buttons->Add(btn_cancel, 0);
    buttons->AddStretchSpacer();
    sizer->Add(buttons, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, FromDIP(12));

    btn_ok->Bind(wxEVT_BUTTON, &SpoolEditDialog::on_ok, this);

    SetSizerAndFit(sizer);
    CentreOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

void SpoolEditDialog::on_ok(wxCommandEvent &event)
{
    if (apply_fields())
        EndModal(wxID_OK);
}

bool SpoolEditDialog::apply_fields()
{
    auto value_of = [](TextInput *input) -> wxString {
        wxString value = input->GetTextCtrl()->GetValue();
        return value.Trim().Trim(false);
    };

    wxString name = value_of(m_name);
    if (name.IsEmpty())
        name = _L("Unnamed spool");
    m_record.name = name.utf8_string();

    wxString filament_type = m_type->GetValue().Trim().Trim(false);

    m_record.vendor             = value_of(m_vendor).utf8_string();
    m_record.filament_type      = filament_type.utf8_string();
    m_record.color              = m_color->GetColour().GetAsString(wxC2S_HTML_SYNTAX).utf8_string();
    m_record.tag_uid            = value_of(m_tag_uid).utf8_string();
    m_record.notes              = value_of(m_notes).utf8_string();

    double diameter = 0.0, density = 0.0, initial = 0.0, used = 0.0;
    if (!value_of(m_diameter).ToDouble(&diameter) || diameter <= 0.0 || diameter > 10.0) {
        MessageDialog(this, _L("Please enter a valid filament diameter."), wxString("Spool"), wxICON_WARNING | wxOK).ShowModal();
        return false;
    }
    if (!value_of(m_density).ToDouble(&density) || density <= 0.0 || density > 10.0) {
        MessageDialog(this, _L("Please enter a valid filament density."), wxString("Spool"), wxICON_WARNING | wxOK).ShowModal();
        return false;
    }
    if (!value_of(m_initial_weight).ToDouble(&initial) || initial < 0.0) {
        MessageDialog(this, _L("Please enter a valid initial weight."), wxString("Spool"), wxICON_WARNING | wxOK).ShowModal();
        return false;
    }
    if (!value_of(m_used).ToDouble(&used) || used < 0.0) {
        MessageDialog(this, _L("Please enter a valid used weight."), wxString("Spool"), wxICON_WARNING | wxOK).ShowModal();
        return false;
    }

    m_record.diameter_mm      = (float) diameter;
    m_record.density          = (float) density;
    m_record.initial_weight_g = (float) initial;
    m_record.used_g           = (float) used;
    return true;
}

// ---------------------------------------------------------------- SpoolUsageDialog

SpoolUsageDialog::SpoolUsageDialog(wxWindow *parent, const SpoolRecord &record)
    : DPIDialog(parent, wxID_ANY, _L("Record Usage"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
    , m_record(record)
{
    auto *sizer = new wxBoxSizer(wxVERTICAL);

    auto *hint = new wxStaticText(this, wxID_ANY,
        wxString::Format(_L("Record filament usage for \"%s\""), GUI::from_u8(record.name)));
    hint->SetFont(::Label::Body_13);
    sizer->Add(hint, 0, wxALL, FromDIP(12));

    auto *grid = new wxFlexGridSizer(0, 2, FromDIP(6), FromDIP(10));
    grid->AddGrowableCol(1, 1);

    auto *grams_label = new wxStaticText(this, wxID_ANY, _L("Used (g)"));
    grams_label->SetFont(::Label::Body_13);
    m_grams_input = new TextInput(this, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
    m_grams_input->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    grid->Add(grams_label, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(m_grams_input, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);

    auto *project_label = new wxStaticText(this, wxID_ANY, _L("Project"));
    project_label->SetFont(::Label::Body_13);
    m_project_input = new TextInput(this, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
    grid->Add(project_label, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(m_project_input, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);

    sizer->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(12));

    auto *buttons = new wxBoxSizer(wxHORIZONTAL);
    auto *btn_ok     = new wxButton(this, wxID_OK, _L("OK"));
    auto *btn_cancel = new wxButton(this, wxID_CANCEL, _L("Cancel"));
    buttons->Add(btn_ok, 0, wxRIGHT, FromDIP(8));
    buttons->Add(btn_cancel, 0);
    buttons->AddStretchSpacer();
    sizer->Add(buttons, 0, wxALL | wxEXPAND, FromDIP(12));

    btn_ok->Bind(wxEVT_BUTTON, &SpoolUsageDialog::on_ok, this);

    SetSizerAndFit(sizer);
    CentreOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

void SpoolUsageDialog::on_ok(wxCommandEvent &event)
{
    double grams = 0.0;
    if (!m_grams_input->GetTextCtrl()->GetValue().Trim().ToDouble(&grams) || grams <= 0.0) {
        MessageDialog(this, _L("Please enter a positive usage amount in grams."), wxString("Spool"), wxICON_WARNING | wxOK).ShowModal();
        return;
    }
    m_grams        = (float) grams;
    m_project_name = m_project_input->GetTextCtrl()->GetValue().utf8_string();
    EndModal(wxID_OK);
}

// ---------------------------------------------------------------- SpoolManagerDialog

SpoolManagerDialog::SpoolManagerDialog(wxWindow *parent)
    : DPIDialog(parent, wxID_ANY, _L("Spool Manager"), wxDefaultPosition, wxSize(FromDIP(720), FromDIP(520)),
                wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    build();
    reload();

    SpoolManager::instance().Bind(EVT_SPOOL_LEDGER_CHANGED, &SpoolManagerDialog::on_spool_ledger_changed, this);
    wxGetApp().UpdateDlgDarkUI(this);
}

SpoolManagerDialog::~SpoolManagerDialog()
{
    SpoolManager::instance().Unbind(EVT_SPOOL_LEDGER_CHANGED, &SpoolManagerDialog::on_spool_ledger_changed, this);
}

void SpoolManagerDialog::build()
{
    auto *sizer = new wxBoxSizer(wxVERTICAL);

    auto make_button = [&](const wxString &label) {
        auto *button = new wxButton(this, wxID_ANY, label);
        button->SetFont(::Label::Body_13);
        return button;
    };

    auto *spool_buttons = new wxBoxSizer(wxHORIZONTAL);
    m_btn_add    = make_button(_L("Add"));
    m_btn_edit   = make_button(_L("Edit"));
    m_btn_delete = make_button(_L("Delete"));
    spool_buttons->Add(m_btn_add, 0, wxRIGHT, FromDIP(6));
    spool_buttons->Add(m_btn_edit, 0, wxRIGHT, FromDIP(6));
    spool_buttons->Add(m_btn_delete, 0);
    spool_buttons->AddStretchSpacer();

    m_spool_list = new wxDataViewListCtrl(this, wxID_ANY);
    m_spool_list->AppendTextColumn(_L("Name"), wxDATAVIEW_CELL_INERT, FromDIP(150), wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE);
    m_spool_list->AppendTextColumn(_L("Filament Type"), wxDATAVIEW_CELL_INERT, FromDIP(90), wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE);
    m_spool_list->AppendTextColumn(_L("Color"), wxDATAVIEW_CELL_INERT, FromDIP(80), wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE);
    m_spool_list->AppendTextColumn(_L("Remaining"), wxDATAVIEW_CELL_INERT, FromDIP(70), wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE);
    m_spool_list->AppendTextColumn(_L("Remaining (g)"), wxDATAVIEW_CELL_INERT, FromDIP(100), wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE);
    m_spool_list->AppendTextColumn(_L("Remaining (m)"), wxDATAVIEW_CELL_INERT, FromDIP(100), wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE);
    m_spool_list->AppendTextColumn(_L("Used (g)"), wxDATAVIEW_CELL_INERT, FromDIP(80), wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE);
    m_spool_list->AppendTextColumn(_L("Vendor"), wxDATAVIEW_CELL_INERT, FromDIP(90), wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE);

    auto *history_title = new wxStaticText(this, wxID_ANY, _L("Usage History"));
    history_title->SetFont(::Label::Head_13);

    m_history_list = new wxDataViewListCtrl(this, wxID_ANY);
    m_history_list->AppendTextColumn(_L("Date"), wxDATAVIEW_CELL_INERT, FromDIP(120), wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE);
    m_history_list->AppendTextColumn(_L("Used (g)"), wxDATAVIEW_CELL_INERT, FromDIP(70), wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE);
    m_history_list->AppendTextColumn(_L("Extruder"), wxDATAVIEW_CELL_INERT, FromDIP(70), wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE);
    m_history_list->AppendTextColumn(_L("Project"), wxDATAVIEW_CELL_INERT, FromDIP(180), wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE);
    m_history_list->AppendTextColumn(_L("Printer"), wxDATAVIEW_CELL_INERT, FromDIP(120), wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE);
    m_history_list->AppendTextColumn(_L("Status"), wxDATAVIEW_CELL_INERT, FromDIP(80), wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE);

    m_assignments_label = new wxStaticText(this, wxID_ANY, wxEmptyString);
    m_assignments_label->SetFont(::Label::Body_11);

    auto *usage_buttons = new wxBoxSizer(wxHORIZONTAL);
    m_btn_record  = make_button(_L("Record Usage"));
    m_btn_confirm = make_button(_L("Mark Confirmed"));
    m_btn_refund  = make_button(_L("Refund Entry"));
    usage_buttons->Add(m_btn_record, 0, wxRIGHT, FromDIP(6));
    usage_buttons->Add(m_btn_confirm, 0, wxRIGHT, FromDIP(6));
    usage_buttons->Add(m_btn_refund, 0);
    usage_buttons->AddStretchSpacer();

    auto *close_buttons = new wxBoxSizer(wxHORIZONTAL);
    auto *btn_close = new wxButton(this, wxID_CANCEL, _L("Close"));
    close_buttons->AddStretchSpacer();
    close_buttons->Add(btn_close, 0);

    sizer->Add(spool_buttons, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, FromDIP(12));
    sizer->Add(m_spool_list, 1, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, FromDIP(12));
    sizer->Add(m_assignments_label, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(4));
    sizer->Add(history_title, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(10));
    sizer->Add(m_history_list, 1, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, FromDIP(12));
    sizer->Add(usage_buttons, 0, wxLEFT | wxRIGHT | wxEXPAND, FromDIP(12));
    sizer->Add(close_buttons, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM | wxEXPAND, FromDIP(12));

    m_btn_add->Bind(wxEVT_BUTTON, &SpoolManagerDialog::on_add, this);
    m_btn_edit->Bind(wxEVT_BUTTON, &SpoolManagerDialog::on_edit, this);
    m_btn_delete->Bind(wxEVT_BUTTON, &SpoolManagerDialog::on_delete, this);
    m_btn_record->Bind(wxEVT_BUTTON, &SpoolManagerDialog::on_record_usage, this);
    m_btn_confirm->Bind(wxEVT_BUTTON, &SpoolManagerDialog::on_confirm_entry, this);
    m_btn_refund->Bind(wxEVT_BUTTON, &SpoolManagerDialog::on_refund_entry, this);
    m_spool_list->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &SpoolManagerDialog::on_selection_changed, this);

    SetSizerAndFit(sizer);
    CentreOnParent();
}

void SpoolManagerDialog::reload()
{
    std::string keep_id = selected_spool_id();

    m_records = SpoolManager::instance().records();
    m_spool_list->DeleteAllItems();
    for (const SpoolRecord &record : m_records) {
        wxVector<wxVariant> row;
        float pct = record.remaining_pct();
        wxString remain_pct_str = pct < 0.0f ? _L("unknown") : wxString::Format("%d%%", (int) (pct + 0.5f));
        row.push_back(wxVariant(GUI::from_u8(record.name)));
        row.push_back(wxVariant(GUI::from_u8(record.filament_type)));
        row.push_back(wxVariant(spool_color_text(record.color)));
        row.push_back(wxVariant(remain_pct_str));
        row.push_back(wxVariant(wxString::Format("%.0f", (double) record.remaining_g())));
        row.push_back(wxVariant(wxString::Format("%.1f", (double) record.remaining_m())));
        row.push_back(wxVariant(wxString::Format("%.0f", (double) record.used_g)));
        row.push_back(wxVariant(GUI::from_u8(record.vendor)));
        m_spool_list->AppendItem(row);
    }

    // restore selection by id when the record still exists
    if (!keep_id.empty()) {
        for (size_t i = 0; i < m_records.size(); ++i)
            if (m_records[i].id == keep_id) {
                m_spool_list->SelectRow((long) i);
                break;
            }
    }
    update_buttons();
    reload_history();
}

void SpoolManagerDialog::reload_history()
{
    m_entries.clear();
    m_history_list->DeleteAllItems();
    std::string spool_id = selected_spool_id();
    if (!spool_id.empty())
        m_entries = SpoolManager::instance().usage_for_spool(spool_id);
    for (const SpoolUsageEntry &entry : m_entries) {
        wxVector<wxVariant> row;
        row.push_back(wxVariant(spool_date_text(entry.timestamp)));
        row.push_back(wxVariant(wxString::Format("%.1f", (double) entry.used_g)));
        row.push_back(wxVariant(entry.extruder_id >= 0 ? wxString::Format("%d", entry.extruder_id) : wxString("-")));
        row.push_back(wxVariant(entry.project_name.empty() ? wxString("-") : GUI::from_u8(entry.project_name)));
        row.push_back(wxVariant(entry.dev_name.empty() ? GUI::from_u8(entry.dev_id) : GUI::from_u8(entry.dev_name)));
        row.push_back(wxVariant(spool_status_text(entry.status)));
        m_history_list->AppendItem(row);
    }

    // assignments overview for the selected spool
    wxString assignments;
    if (!spool_id.empty()) {
        SpoolLedger ledger = SpoolManager::instance().snapshot();
        auto bound = ledger.assignments_for_spool(spool_id);
        for (const auto &assignment : bound) {
            if (!assignments.empty())
                assignments += ", ";
            assignments += GUI::from_u8(assignment.first);
        }
    }
    m_assignments_label->SetLabelText(assignments.empty() ? wxString() : _L("Assigned to: ") + assignments);
    Layout();
}

long SpoolManagerDialog::selected_row() const
{
    if (!m_spool_list)
        return -1;
    return m_spool_list->GetSelectedRow();
}

std::string SpoolManagerDialog::selected_spool_id() const
{
    long row = selected_row();
    if (row < 0 || row >= (long) m_records.size())
        return std::string();
    return m_records[(size_t) row].id;
}

long SpoolManagerDialog::selected_history_row() const
{
    if (!m_history_list)
        return -1;
    return m_history_list->GetSelectedRow();
}

std::string SpoolManagerDialog::selected_entry_id() const
{
    long row = selected_history_row();
    if (row < 0 || row >= (long) m_entries.size())
        return std::string();
    return m_entries[(size_t) row].id;
}

void SpoolManagerDialog::update_buttons()
{
    bool has_spool  = selected_row() >= 0;
    bool has_entry  = selected_history_row() >= 0;
    m_btn_edit->Enable(has_spool);
    m_btn_delete->Enable(has_spool);
    m_btn_record->Enable(has_spool);
    m_btn_confirm->Enable(has_entry);
    m_btn_refund->Enable(has_entry);
}

void SpoolManagerDialog::on_add(wxCommandEvent &event)
{
    SpoolRecord record;
    record.name = _L("New spool").utf8_string();
    SpoolEditDialog dialog(this, record, true);
    if (dialog.ShowModal() == wxID_OK)
        SpoolManager::instance().add_record(dialog.record());
}

void SpoolManagerDialog::on_edit(wxCommandEvent &event)
{
    std::string spool_id = selected_spool_id();
    SpoolRecord record;
    if (!SpoolManager::instance().get_record(spool_id, record))
        return;
    SpoolEditDialog dialog(this, record, false);
    if (dialog.ShowModal() == wxID_OK)
        SpoolManager::instance().update_record(dialog.record());
}

void SpoolManagerDialog::on_delete(wxCommandEvent &event)
{
    std::string spool_id = selected_spool_id();
    if (spool_id.empty())
        return;
    MessageDialog confirm(this, _L("Delete the selected spool together with its usage history and slot assignments?"),
        _L("Delete Spool"), wxYES_NO | wxICON_WARNING);
    if (confirm.ShowModal() != wxID_YES)
        return;
    SpoolManager::instance().remove_record(spool_id);
}

void SpoolManagerDialog::on_record_usage(wxCommandEvent &event)
{
    std::string spool_id = selected_spool_id();
    SpoolRecord record;
    if (!SpoolManager::instance().get_record(spool_id, record))
        return;
    SpoolUsageDialog dialog(this, record);
    if (dialog.ShowModal() != wxID_OK)
        return;
    SpoolManager::instance().record_manual_usage(spool_id, dialog.grams(), dialog.project_name());
}

void SpoolManagerDialog::on_confirm_entry(wxCommandEvent &event)
{
    std::string entry_id = selected_entry_id();
    if (!entry_id.empty())
        SpoolManager::instance().set_entry_status(entry_id, SpoolUsageStatus::Confirmed);
}

void SpoolManagerDialog::on_refund_entry(wxCommandEvent &event)
{
    std::string entry_id = selected_entry_id();
    if (!entry_id.empty())
        SpoolManager::instance().set_entry_status(entry_id, SpoolUsageStatus::Refunded);
}

void SpoolManagerDialog::on_selection_changed(wxCommandEvent &event)
{
    event.Skip();
    update_buttons();
    reload_history();
}

void SpoolManagerDialog::on_spool_ledger_changed(wxCommandEvent &event)
{
    event.Skip();
    reload();
}

} // namespace GUI
} // namespace Slic3r
