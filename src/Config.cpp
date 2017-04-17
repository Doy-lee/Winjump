#include "Config.h"

#include <stdlib.h>

#include "Winjump.h"
#include "dqn.h"

const char *const CONFIG_GLOBAL_STRING_INVALID_HOTKEY =
    "Winjump | Hotkey is being used by another process";

// Returns NULL if property not found, or if arguments are invalid
FILE_SCOPE const char *
config_ini_load_property_value_string(DqnIni *ini, const char *const property)
{
	if (!ini || !property) return NULL;

	i32 index =
	    dqn_ini_find_property(ini, DQN_INI_GLOBAL_SECTION, property, 0);

	if (index == DQN_INI_NOT_FOUND)
	{
		char errorMsg[256] = {};

		i32 numWritten = dqn_sprintf( errorMsg,
		    "dqn_ini_find_property() failed: Could not find '%s' property",
		    property);

		DQN_ASSERT(numWritten != DQN_ARRAY_COUNT(errorMsg));
		OutputDebugString(errorMsg);
		return NULL;
	}

	const char *value =
	    dqn_ini_property_value(ini, DQN_INI_GLOBAL_SECTION, index);
	return value;
}

// If value is unable to be found, value remains unchanged
FILE_SCOPE bool config_ini_load_property_value_int(DqnIni *ini,
                                                   const char *const property,
                                                   i32 *value)
{
	if (!ini || !property || !value) return false;

	const char *propertyValue =
	    config_ini_load_property_value_string(ini, property);
	if (!propertyValue) return false;

	*value = dqn_str_to_i32(propertyValue, dqn_strlen(propertyValue));

	return true;
}

FILE_SCOPE const char *const GLOBAL_STRING_INI_CONFIG_PATH = "winjump.ini";

FILE_SCOPE const char *const GLOBAL_STRING_INI_FONT_NAME             = "FontName";
FILE_SCOPE const char *const GLOBAL_STRING_INI_FONT_WEIGHT           = "FontWeight";
FILE_SCOPE const char *const GLOBAL_STRING_INI_FONT_HEIGHT           = "FontHeight";
FILE_SCOPE const char *const GLOBAL_STRING_INI_FONT_ITALIC           = "FontItalic";
FILE_SCOPE const char *const GLOBAL_STRING_INI_FONT_PITCH_AND_FAMILY = "FontPitchAndFamily";
FILE_SCOPE const char *const GLOBAL_STRING_INI_FONT_CHAR_SET         = "FontCharSet";
FILE_SCOPE const char *const GLOBAL_STRING_INI_FONT_OUT_PRECISION    = "FontOutPrecision";
FILE_SCOPE const char *const GLOBAL_STRING_INI_FONT_CLIP_PRECISION   = "FontClipPrecision";
FILE_SCOPE const char *const GLOBAL_STRING_INI_FONT_QUALITY          = "FontQuality";

FILE_SCOPE const char *const GLOBAL_STRING_INI_HOTKEY_VIRTUAL_KEY = "HotkeyVirtualKey";
FILE_SCOPE const char *const GLOBAL_STRING_INI_HOTKEY_MODIFIER    = "HotkeyModifier";

HFONT config_read_from_disk(WinjumpState *state)
{
	// Get a handle to config file
	DqnFile config = {};

	// If size is 0, also return .. it's an empty file.
	if (!dqn_file_open(GLOBAL_STRING_INI_CONFIG_PATH, &config,
	                   dqnfilepermissionflag_read,
	                   dqnfileaction_open_only) || config.size == 0)
	{
		dqn_file_close(&config);
		state->configIsStale = true;
		return NULL;
	}

	////////////////////////////////////////////////////////////////////////////
	// Create ini intermediate representation
	////////////////////////////////////////////////////////////////////////////
	DqnIni *ini = NULL;
	{
		u8 *data = (u8 *)calloc(1, (size_t)config.size);

		if (!data)
		{
			dqn_file_close(&config);
			OutputDebugString(
			    "calloc() failed: Not enough memory. Continuing without "
			    "configuration file.");
			return NULL;
		}

		// Read data to intermediate format
		dqn_file_read(config, data, (u32)config.size);
		dqn_file_close(&config);
		ini = dqn_ini_load((char *)data, NULL);
		free(data);
	}

	////////////////////////////////////////////////////////////////////////////
	// Start parsing to recreate font from config file
	////////////////////////////////////////////////////////////////////////////
	HFONT font = NULL;
	{
		// NOTE: If font name is not found, this will return NULL and CreateFont
		// will just choose the best matching font fitting the criteria we've
		// found.
		const char *fontName = config_ini_load_property_value_string(
		    ini, GLOBAL_STRING_INI_FONT_NAME);

		i32 fontHeight         = -11;
		i32 fontWeight         = FW_DONTCARE;
		i32 fontItalic         = 0;
		i32 fontPitchAndFamily = DEFAULT_PITCH | (FF_DONTCARE << 3);
		i32 fontCharSet        = 0;
		i32 fontOutPrecision   = 0;
		i32 fontClipPrecision  = 0;
		i32 fontQuality        = 0;

		config_ini_load_property_value_int(ini, GLOBAL_STRING_INI_FONT_HEIGHT, &fontHeight);
		config_ini_load_property_value_int(ini, GLOBAL_STRING_INI_FONT_WEIGHT, &fontWeight);
		config_ini_load_property_value_int(ini, GLOBAL_STRING_INI_FONT_ITALIC, &fontItalic);
		config_ini_load_property_value_int(ini, GLOBAL_STRING_INI_FONT_PITCH_AND_FAMILY, &fontPitchAndFamily);
		config_ini_load_property_value_int(ini, GLOBAL_STRING_INI_FONT_CHAR_SET, &fontCharSet);
		config_ini_load_property_value_int(ini, GLOBAL_STRING_INI_FONT_OUT_PRECISION, &fontOutPrecision);
		config_ini_load_property_value_int(ini, GLOBAL_STRING_INI_FONT_CLIP_PRECISION, &fontClipPrecision);
		config_ini_load_property_value_int(ini, GLOBAL_STRING_INI_FONT_QUALITY, &fontQuality);

		if (fontHeight > 0) fontHeight = -fontHeight;
		i32 isItalic                   = (fontItalic > 0) ? 255 : 0;
		i32 isCharSet                  = (fontCharSet > 0) ? 255 : 0;

		// Load font from configuration
		font = CreateFont(fontHeight, 0, 0, 0, fontWeight, isItalic, 0, 0,
		                  isCharSet, fontOutPrecision, fontClipPrecision,
		                  fontQuality, fontPitchAndFamily, fontName);
	}

	////////////////////////////////////////////////////////////////////////////
	// Parse the hotkey out
	////////////////////////////////////////////////////////////////////////////
	// TODO(doyle): Look at making this function return some platform
	// independent struct and then convert it to Win32 data in the platform.
	{
		AppHotkey newHotkey = {};
		config_ini_load_property_value_int(ini, GLOBAL_STRING_INI_HOTKEY_VIRTUAL_KEY, &(i32)newHotkey.virtualKey);
		config_ini_load_property_value_int(ini, GLOBAL_STRING_INI_HOTKEY_MODIFIER,    &(i32)newHotkey.modifier);

		// Validate the loaded values
		{
			newHotkey.virtualKey = dqn_char_to_upper(newHotkey.virtualKey);
			if ((newHotkey.virtualKey >= 'A' || newHotkey.virtualKey <= 'Z') &&
			    (newHotkey.modifier >= 0 &&
			     newHotkey.modifier < apphotkeymodifier_count))
			{
				// Valid key
			}
			else
			{
				// Out of range, invalid hotkey, revert to default hotkey
				AppHotkey defaultHotkey = {};
				newHotkey               = defaultHotkey;
				DQN_ASSERT(newHotkey.virtualKey == 'K');
			}
		}
		state->appHotkey = newHotkey;

		u32 win32ModKey =
		    winjump_apphotkey_to_win32_mod_hotkey_modifier(newHotkey.modifier);
		u32 win32VKey = newHotkey.virtualKey;

		// Try registering the hotkey
		HWND client = state->window[winjumpwindow_main_client].handle;
		if (RegisterHotKey(client, WIN32_GUID_HOTKEY_ACTIVATE_APP, win32ModKey,
		                   win32VKey))
		{
			char hotkeyString[32] = {};
			winjump_hotkey_to_string(newHotkey, hotkeyString,
			                         DQN_ARRAY_COUNT(hotkeyString));

			char newWindowTitle[256] = {};
			dqn_sprintf(newWindowTitle,
			            "Winjump | Press %s to activate Winjump", hotkeyString);
			SetWindowText(state->window[winjumpwindow_main_client].handle,
			              newWindowTitle);
		}
		else
		{
			SetWindowText(client, CONFIG_GLOBAL_STRING_INVALID_HOTKEY);
		}
	}

	dqn_ini_destroy(ini);

	DQN_ASSERT(font);
	return font;
}

FILE_SCOPE bool config_write_to_ini_string(DqnIni *ini,
                                           const char *const property,
                                           const char *const value)
{
	if (!ini || !property || !value) return false;

	i32 index = dqn_ini_find_property(ini, DQN_INI_GLOBAL_SECTION, property, 0);
	if (index == DQN_INI_NOT_FOUND)
	{
		dqn_ini_property_add(ini, DQN_INI_GLOBAL_SECTION, property, 0,
		                             value, 0);
	}
	else
	{
		dqn_ini_property_value_set(ini, DQN_INI_GLOBAL_SECTION, index, value,
		                           0);
	}

	return true;
}

FILE_SCOPE bool config_write_to_ini_int(DqnIni *ini, const char *const property,
                                        i32 value)
{
	if (!ini || !property) return false;

	i32 index = dqn_ini_find_property(ini, DQN_INI_GLOBAL_SECTION, property, 0);
	char buf[DQN_I32_TO_STR_MAX_BUF_SIZE] = {};
	dqn_i32_to_str(value, buf, DQN_ARRAY_COUNT(buf));
	if (index == DQN_INI_NOT_FOUND)
	{
		dqn_ini_property_add(ini, DQN_INI_GLOBAL_SECTION, property, 0, buf, 0);
	}
	else
	{
		dqn_ini_property_value_set(ini, DQN_INI_GLOBAL_SECTION, index, buf, 0);
	}

	return true;
}

void config_write_to_disk(WinjumpState *state)
{
	////////////////////////////////////////////////////////////////////////
	// Get the font data for the current configuration to write to disk
	////////////////////////////////////////////////////////////////////////
	LOGFONTW logFont = {};
	GetObjectW(state->font, sizeof(logFont), &logFont);
	// NOTE: The maximum number of bytes a utf-8 character can be is 4.
	// Since we are using wchar_t with Windows, but utf-8 elsewhere, and in
	// the ini code, we need to convert wchar safely to utf8 before storing.
	i32 fontNameMaxLen    = LF_FACESIZE * 4;
	char *fontName        = (char *)calloc(1, fontNameMaxLen);
	bool fontNameNotValid = false;
	if (!fontName)
	{
		OutputDebugString(
		    "calloc() failed: Not enough memory, FontName will not be "
		    "stored to config file");
		fontNameNotValid = true;
	}

	if (!dqn_win32_wchar_to_utf8(logFont.lfFaceName, fontName, fontNameMaxLen))
	{
		OutputDebugString(
		    "dqn_win32_wchar_to_utf8() failed: FontName will not be stored "
		    "to config file");
		fontNameNotValid = true;
	}

	i32 fontHeight         = (i32)logFont.lfHeight;
	i32 fontWeight         = (i32)logFont.lfWeight;
	i32 fontItalic         = (logFont.lfItalic > 0) ? 1 : 0;
	i32 fontPitchAndFamily = (i32)logFont.lfPitchAndFamily;
	i32 fontCharSet        = (logFont.lfCharSet > 0) ? 1 : 0;
	i32 fontOutPrecision   = logFont.lfOutPrecision;
	i32 fontClipPrecision  = logFont.lfClipPrecision;
	i32 fontQuality        = logFont.lfQuality;

	////////////////////////////////////////////////////////////////////////
	// Get handle to file and prepare for write
	////////////////////////////////////////////////////////////////////////
	DqnFile config = {};

	// TODO(doyle): We should not clear a file until we know that the ini
	// file can be created succesfully and written otherwise we lose state.
	if (!dqn_file_open(
	        GLOBAL_STRING_INI_CONFIG_PATH, &config,
	        (dqnfilepermissionflag_read | dqnfilepermissionflag_write),
	        dqnfileaction_clear_if_exist))
	{
		// Then file does not exist
		if (!dqn_file_open(
		        GLOBAL_STRING_INI_CONFIG_PATH, &config,
		        (dqnfilepermissionflag_read | dqnfilepermissionflag_write),
		        dqnfileaction_create_if_not_exist))
		{
			OutputDebugString(
			    "dqn_file_open() failed: Platform was unable to create "
			    "config file on disk");
			return;
		}
	}

	////////////////////////////////////////////////////////////////////////
	// Create INI intermedia representation
	////////////////////////////////////////////////////////////////////////
	DqnIni *ini = NULL;
	if (config.size == 0)
	{
		// TODO(doyle): We have created an abstraction above the INI layer
		// which will automatically resolve whether or not the property
		// exists in the INI file. But if the config size is 0, then we know
		// that all these properties must be written. So there's a small
		// overhead in this case where our abstraction will check to see if
		// the property exists when it's unecessary.
		ini = dqn_ini_create(NULL);
	}
	else
	{
		u8 *data = (u8 *)calloc(1, (size_t)config.size);
		if (!data)
		{
			OutputDebugString(
			    "calloc() failed: Not enough memory. Exiting without "
			    "saving configuration file.");
			return;
		}
	}
	DQN_ASSERT(ini);

	////////////////////////////////////////////////////////////////////////////
	// Write Font Data
	////////////////////////////////////////////////////////////////////////////
	if (!fontNameNotValid)
	{
		config_write_to_ini_string(
		    ini, GLOBAL_STRING_INI_FONT_NAME, fontName);
	}

	config_write_to_ini_int(ini, GLOBAL_STRING_INI_FONT_HEIGHT,           fontHeight);
	config_write_to_ini_int(ini, GLOBAL_STRING_INI_FONT_WEIGHT,           fontWeight);
	config_write_to_ini_int(ini, GLOBAL_STRING_INI_FONT_ITALIC,           fontItalic);
	config_write_to_ini_int(ini, GLOBAL_STRING_INI_FONT_PITCH_AND_FAMILY, fontPitchAndFamily);
	config_write_to_ini_int(ini, GLOBAL_STRING_INI_FONT_CHAR_SET,         fontCharSet);
	config_write_to_ini_int(ini, GLOBAL_STRING_INI_FONT_OUT_PRECISION,    fontOutPrecision);
	config_write_to_ini_int(ini, GLOBAL_STRING_INI_FONT_CLIP_PRECISION,   fontClipPrecision);
	config_write_to_ini_int(ini, GLOBAL_STRING_INI_FONT_QUALITY,          fontQuality);

	////////////////////////////////////////////////////////////////////////////
	// Write Global Hotkey Data
	////////////////////////////////////////////////////////////////////////////
	AppHotkey hotkey = globalState.appHotkey;
	config_write_to_ini_int(ini, GLOBAL_STRING_INI_HOTKEY_VIRTUAL_KEY, hotkey.virtualKey);
	config_write_to_ini_int(ini, GLOBAL_STRING_INI_HOTKEY_MODIFIER,    hotkey.modifier);

	////////////////////////////////////////////////////////////////////////
	// Write ini to disk
	////////////////////////////////////////////////////////////////////////
	i32 requiredSize   = dqn_ini_save(ini, NULL, 0);
	u8 *dataToWriteOut = (u8 *)calloc(1, requiredSize);
	dqn_ini_save(ini, (char *)dataToWriteOut, requiredSize);
	dqn_file_write(&config, dataToWriteOut, requiredSize, 0);
}

