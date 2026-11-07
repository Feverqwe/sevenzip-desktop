#pragma once
#include "filemanager/panels/Panel.h"

inline void MarkSelectionForTest(CPanel &panel, const UString &name) {
    CSelectedState state;
    panel.SaveSelectedState(state);
    state.SelectedNames.AddToUniqueSorted(name);
    panel.RestoreLoadedSelection(state);
}
