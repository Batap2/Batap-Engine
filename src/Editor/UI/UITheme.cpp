#include "UITheme.h"

// The one definition of every mutable global UITheme.h declares. They are
// written by ApplyTheme and read by every panel; keeping them out of the header
// is what guarantees a single copy.
namespace batap::ui
{
ImVec4 bg0, bg1, bg2, bg3, bg4;
ImVec4 hdr, hdrHv, hdrAc;
ImVec4 border;
ImVec4 text, textDim, textBright;

ImFont* smallFont = nullptr;
ImFont* monoFont = nullptr;
}  // namespace batap::ui
