/**
 * @file    shortcuts.h
 * @brief   The modifier key of the shortcuts, as the labels of menus and tooltips name it.
 */

#ifndef SIGGEN_SHORTCUTS_H
#define SIGGEN_SHORTCUTS_H

// ImGui reads Cmd as ImGuiMod_Ctrl on macOS (io.ConfigMacOSXBehaviors), so the same shortcuts are labeled with the
// key pressed there. A macro, so labels stay string literals: SIGGEN_SHORTCUT "N" is "Ctrl+N" or "Cmd+N"
#ifdef __APPLE__
#define SIGGEN_SHORTCUT "Cmd+"
#else
#define SIGGEN_SHORTCUT "Ctrl+"
#endif

#endif // SIGGEN_SHORTCUTS_H
