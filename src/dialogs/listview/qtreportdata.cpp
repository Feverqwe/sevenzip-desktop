#include "dialogs/listview/qtreportdata.h"
#include "archive/diagnostics/archiveerrormessages.h"
#include "filemanager/PropertyName.h"
#include "ui/diagnostics/qtdiagnostictext.h"

void QtReportData::QtBindPropertyName(unsigned row, UInt32 id, const wchar_t *fallback) {
    _rowBindings[row].Property =
        CPropertyNameBinding{id, fallback ? std::make_optional<UString>(fallback) : std::nullopt};
}
void QtReportData::QtBindDiagnosticRow(unsigned row, const char *name, const char *value) {
    _rowBindings[row].Diagnostic = {name, value};
}
void QtReportData::QtBindArchiveDiagnosticValue(unsigned row, const UString &source) {
    _rowBindings[row].ArchiveValue = source;
}
void QtReportData::QtBindArchiveFlagsValue(unsigned row, UInt32 flags) {
    _rowBindings[row].ArchiveValue = flags;
}
std::optional<UInt32> QtReportData::PropertyID(unsigned row) const {
    const auto binding = _rowBindings.value(row);
    return binding.Property ? std::make_optional(binding.Property->ID) : std::nullopt;
}
void QtReportData::RefreshRow(unsigned row, std::optional<unsigned> bindingRow) {
    const auto binding = _rowBindings.value(bindingRow.value_or(row));
    const auto *flags = std::get_if<UInt32>(&binding.ArchiveValue);
    const auto *source = std::get_if<UString>(&binding.ArchiveValue);
    if ((flags || source) && row < Values.Size())
        Values[row] = flags ? GetOpenArcErrorMessage(*flags) : QtArchiveDiagnosticText(*source);
    if (!binding.Diagnostic.first.isEmpty() && row < Strings.Size())
        Strings[row] = QtDiagnosticText(binding.Diagnostic.first.constData());
    if (!binding.Diagnostic.second.isEmpty() && row < Values.Size())
        Values[row] = QtDiagnosticText(binding.Diagnostic.second.constData());
    if (binding.Property && row < Strings.Size()) {
        const auto &property = *binding.Property;
        Strings[row] =
            GetNameOfProperty(property.ID, property.Fallback ? property.Fallback->Ptr() : nullptr);
    }
}
