#ifndef slic3r_SpoolManagerDialog_hpp_
#define slic3r_SpoolManagerDialog_hpp_

#include "GUI_Utils.hpp"
#include "libslic3r/Spool.hpp"

#include <string>
#include <vector>

class wxButton;
class wxColourPickerCtrl;
class wxComboBox;
class wxDataViewListCtrl;
class wxStaticText;
class TextInput;

namespace Slic3r {
namespace GUI {

// Add/edit form for a single spool record.
class SpoolEditDialog : public DPIDialog
{
public:
    SpoolEditDialog(wxWindow *parent, const SpoolRecord &record, bool is_new);
    ~SpoolEditDialog() override = default;

    // Valid after ShowModal() returns wxID_OK.
    const SpoolRecord &record() const { return m_record; }

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override {}

private:
    void on_ok(wxCommandEvent &event);
    bool apply_fields();

    SpoolRecord m_record;
    bool m_is_new{false};

    TextInput *m_name{nullptr};
    TextInput *m_vendor{nullptr};
    wxComboBox *m_type{nullptr};
    wxColourPickerCtrl *m_color{nullptr};
    TextInput *m_diameter{nullptr};
    TextInput *m_density{nullptr};
    TextInput *m_initial_weight{nullptr};
    TextInput *m_used{nullptr};
    TextInput *m_tag_uid{nullptr};
    TextInput *m_notes{nullptr};
};

// Small prompt for manually recording filament usage against a spool.
class SpoolUsageDialog : public DPIDialog
{
public:
    SpoolUsageDialog(wxWindow *parent, const SpoolRecord &record);

    float grams() const { return m_grams; }
    const std::string &project_name() const { return m_project_name; }

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override {}

private:
    void on_ok(wxCommandEvent &event);

    SpoolRecord m_record;
    float m_grams{0.0f};
    std::string m_project_name;

    TextInput *m_grams_input{nullptr};
    TextInput *m_project_input{nullptr};
};

// Main spool inventory manager: list, CRUD, usage history and manual settle.
class SpoolManagerDialog : public DPIDialog
{
public:
    SpoolManagerDialog(wxWindow *parent);
    ~SpoolManagerDialog() override;

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override {}

private:
    void build();
    void reload();
    void reload_history();
    void update_buttons();

    long selected_row() const;
    std::string selected_spool_id() const;
    long selected_history_row() const;
    std::string selected_entry_id() const;

    void on_add(wxCommandEvent &event);
    void on_edit(wxCommandEvent &event);
    void on_delete(wxCommandEvent &event);
    void on_record_usage(wxCommandEvent &event);
    void on_confirm_entry(wxCommandEvent &event);
    void on_refund_entry(wxCommandEvent &event);
    void on_selection_changed(wxCommandEvent &event);
    void on_spool_ledger_changed(wxCommandEvent &event);

    wxDataViewListCtrl *m_spool_list{nullptr};
    wxDataViewListCtrl *m_history_list{nullptr};
    wxButton *m_btn_add{nullptr};
    wxButton *m_btn_edit{nullptr};
    wxButton *m_btn_delete{nullptr};
    wxButton *m_btn_record{nullptr};
    wxButton *m_btn_confirm{nullptr};
    wxButton *m_btn_refund{nullptr};
    wxStaticText *m_assignments_label{nullptr};

    std::vector<SpoolRecord> m_records;
    std::vector<SpoolUsageEntry> m_entries;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_SpoolManagerDialog_hpp_
