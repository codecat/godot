/*************************************************************************/
/*  editor_command_palette.h                                             */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2021 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2021 Godot Engine contributors (cf. AUTHORS.md).   */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#ifndef EDITOR_COMMAND_PALETTE_H
#define EDITOR_COMMAND_PALETTE_H

#include "core/os/thread_safe.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/shortcut.h"
#include "scene/gui/tree.h"

class EditorCommandPalette : public ConfirmationDialog {
	GDCLASS(EditorCommandPalette, ConfirmationDialog);

	static EditorCommandPalette *singleton;
	LineEdit *command_search_box;
	Tree *search_options;

	struct Command {
		Callable callable;
		String name;
		String shortcut;
	};

	struct CommandEntry {
		String key_name;
		String display_name;
		String shortcut_text;
		float score;
	};

	struct CommandEntryComparator {
		_FORCE_INLINE_ bool operator()(const CommandEntry &A, const CommandEntry &B) const {
			return A.score > B.score;
		}
	};

	HashMap<String, Command> commands;
	HashMap<String, Pair<String, Ref<Shortcut>>> unregistered_shortcuts;

	List<String> command_keys;

	void _update_command_search(const String &search_text);
	float _score_path(const String &p_search, const String &p_path);
	void _sbox_input(const Ref<InputEvent> &p_ie);
	void _confirmed();
	void _update_command_keys();
	void _add_command(String p_command_name, String p_key_name, Callable p_binded_action, String p_shortcut_text = "None");
	void _theme_changed();
	EditorCommandPalette();

protected:
	static void _bind_methods();

public:
	void open_popup();
	void get_actions_list(List<String> *p_list) const;
	void add_command(String p_command_name, String p_key_name, Callable p_action, Vector<Variant> arguments, String p_shortcut_text = "None");
	void execute_command(String &p_command_name);
	void register_shortcuts_as_command();
	Ref<Shortcut> add_shortcut_command(const String &p_command, const String &p_key, Ref<Shortcut> p_shortcut);
	void remove_command(String p_key_name);
	static EditorCommandPalette *get_singleton();
};

Ref<Shortcut> ED_SHORTCUT_AND_COMMAND(const String &p_path, const String &p_name, uint32_t p_keycode = 0, String p_command = "");

#endif //EDITOR_COMMAND_PALETTE_H
