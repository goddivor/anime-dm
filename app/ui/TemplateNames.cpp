#include "ui/TemplateNames.h"

namespace {

struct Entry {
    const char* id;
    StringId name;
};

constexpr Entry kNames[] = {
    {"none", STR_TPL_NONE},
    {"shadow", STR_TPL_SHADOW},
    {"dvdcase-bluray", STR_TPL_BLURAY},
    {"folder-horizontal", STR_TPL_FOLDER_H},
    {"folder-vertical", STR_TPL_FOLDER_V},
    {"dvdcase-transparent", STR_TPL_PLASTIC},
    {"dvdbox-dark", STR_TPL_DVD_DARK},
    {"dvdbox-light", STR_TPL_DVD_LIGHT},
    {"windows-11-a", STR_TPL_WIN11},
    {"beorigin", STR_TPL_BEORIGIN},
    {"discart", STR_TPL_DISC},
    {"dualtab-vertical", STR_TPL_DUALTAB},
};

}  // namespace

// The caption of a folder-icon recipe, by its id.
StringId TemplateName(const std::string& templateId) {
    for (const Entry& entry : kNames) {
        if (templateId == entry.id) {
            return entry.name;
        }
    }
    return STR_TPL_NONE;
}
