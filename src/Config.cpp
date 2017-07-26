#include "Config.h"

#include <stdlib.h>

#include "Winjump.h"
#include "dqn.h"

const char *const CONFIG_GLOBAL_STRING_INVALID_HOTKEY =
    "Winjump | Hotkey is being used by another process";

// Returns NULL if property not found, or if arguments are invalid
FILE_SCOPE const char *GetIniPropertyValueAsString(DqnIni *const ini, const char *const property)
{
	if (!ini || !property) return NULL;

	i32 index =
	    DqnIni_FindProperty(ini, DQN_INI_GLOBAL_SECTION, property, 0);

	if (index == DQN_INI_NOT_FOUND)
	{
		DqnWin32_OutputDebugString("GetIniPropertyValueAsString() failed: Could not find '%s' property", property);
		return NULL;
	}

	const char *value = DqnIni_PropertyValue(ini, DQN_INI_GLOBAL_SECTION, index);
	return value;
}

// If value is unable to be found, value remains unchanged
FILE_SCOPE bool GetIniPropertyValueAsInt(DqnIni *const ini, const char *const property, i32 *const value)
{
	if (!ini || !property || !value) return false;

	const char *propertyValue = GetIniPropertyValueAsString(ini, property);
	if (!propertyValue) return false;

	*value = (i32)Dqn_StrToI64(propertyValue, DqnStr_Len(propertyValue));

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

HFONT Config_ReadFromDisk(WinjumpState *state)
{
	////////////////////////////////////////////////////////////////////////////
	// Create ini intermediate representation
	////////////////////////////////////////////////////////////////////////////
	u8 *data    = DqnFile_ReadEntireFileSimple(GLOBAL_STRING_INI_CONFIG_PATH);
	DqnIni *ini = NULL;

	if (data)
	{
		ini = DqnIni_Load((char *)data, NULL);
		free(data);
	}
	else
	{
		// File doesn't exist
		state->configIsStale = true;
	}

	////////////////////////////////////////////////////////////////////////////
	// Start parsing to recreate font from config file
	////////////////////////////////////////////////////////////////////////////
	HFONT font = NULL;
	{
		// NOTE: If font name is not found, this will return NULL and CreateFont
		// will just choose the best matching font fitting the criteria we've
		// found.
		i32 fontHeight         = -13;
		i32 fontWeight         = FW_DONTCARE;
		i32 fontItalic         = 0;
		i32 fontPitchAndFamily = DEFAULT_PITCH | (FF_DONTCARE << 3);
		i32 fontCharSet        = 0;
		i32 fontOutPrecision   = 0;
		i32 fontClipPrecision  = 0;
		i32 fontQuality        = 0;

		const char *const DEFAULT_FONT_NAME = "Tahoma";
		const char *fontName                = NULL;
		if (ini)
		{
			GetIniPropertyValueAsInt(ini, GLOBAL_STRING_INI_FONT_HEIGHT,           &fontHeight);
			GetIniPropertyValueAsInt(ini, GLOBAL_STRING_INI_FONT_WEIGHT,           &fontWeight);
			GetIniPropertyValueAsInt(ini, GLOBAL_STRING_INI_FONT_ITALIC,           &fontItalic);
			GetIniPropertyValueAsInt(ini, GLOBAL_STRING_INI_FONT_PITCH_AND_FAMILY, &fontPitchAndFamily);
			GetIniPropertyValueAsInt(ini, GLOBAL_STRING_INI_FONT_CHAR_SET,         &fontCharSet);
			GetIniPropertyValueAsInt(ini, GLOBAL_STRING_INI_FONT_OUT_PRECISION,    &fontOutPrecision);
			GetIniPropertyValueAsInt(ini, GLOBAL_STRING_INI_FONT_CLIP_PRECISION,   &fontClipPrecision);
			GetIniPropertyValueAsInt(ini, GLOBAL_STRING_INI_FONT_QUALITY,          &fontQuality);
			fontName = GetIniPropertyValueAsString(ini, GLOBAL_STRING_INI_FONT_NAME);
		}

		if (fontHeight > 0) fontHeight = -fontHeight;
		i32 isItalic                   = (fontItalic > 0) ? 255 : 0;
		i32 isCharSet                  = (fontCharSet > 0) ? 255 : 0;

		if (!fontName) fontName = DEFAULT_FONT_NAME;
		font = CreateFont(fontHeight, 0, 0, 0, fontWeight, isItalic, 0, 0, isCharSet, fontOutPrecision,
		                  fontClipPrecision, fontQuality, fontPitchAndFamily, fontName);
	}

	////////////////////////////////////////////////////////////////////////////
	// Parse the hotkey out
	////////////////////////////////////////////////////////////////////////////
	{
		if (ini)
		{
			AppHotkey loadedHotkey = {};
			GetIniPropertyValueAsInt(ini, GLOBAL_STRING_INI_HOTKEY_VIRTUAL_KEY, (i32 *)&loadedHotkey.win32VirtualKey);
			GetIniPropertyValueAsInt(ini, GLOBAL_STRING_INI_HOTKEY_MODIFIER,    (i32 *)&loadedHotkey.win32ModifierKey);

			// Validate the loaded values
			{
				loadedHotkey.win32VirtualKey = DqnChar_ToUpper(loadedHotkey.win32VirtualKey);
				if (Winjump_HotkeyIsValid(loadedHotkey)) state->appHotkey = loadedHotkey;
			}
		}

		// Try registering the hotkey
		AppHotkey hotkey = state->appHotkey;
		HWND client      = state->window[WinjumpWindow_MainClient].handle;
		if (RegisterHotKey(client, WIN32_GUID_HOTKEY_ACTIVATE_APP, hotkey.win32ModifierKey,
		                   hotkey.win32VirtualKey))
		{
			char hotkeyString[32] = {};
			Winjump_HotkeyToString(hotkey, hotkeyString, DQN_ARRAY_COUNT(hotkeyString));

			char newWindowTitle[256] = {};
			Dqn_sprintf(newWindowTitle, "Winjump | Press %s to activate Winjump", hotkeyString);
			SetWindowText(state->window[WinjumpWindow_MainClient].handle, newWindowTitle);
		}
		else
		{
			SetWindowText(client, CONFIG_GLOBAL_STRING_INVALID_HOTKEY);
		}
	}

	if (ini) DqnIni_Destroy(ini);
	DQN_ASSERT(font);
	return font;
}

FILE_SCOPE bool WriteToIniString(DqnIni *ini, const char *const property, const char *const value)
{
	if (!ini || !property || !value) return false;

	i32 index = DqnIni_FindProperty(ini, DQN_INI_GLOBAL_SECTION, property, 0);
	if (index == DQN_INI_NOT_FOUND)
	{
		DqnIni_PropertyAdd(ini, DQN_INI_GLOBAL_SECTION, property, 0,
		                             value, 0);
	}
	else
	{
		DqnIni_PropertyValueSet(ini, DQN_INI_GLOBAL_SECTION, index, value,
		                           0);
	}

	return true;
}

FILE_SCOPE bool WriteToIniInt(DqnIni *ini, const char *const property, i32 value)
{
	if (!ini || !property) return false;

	i32 index = DqnIni_FindProperty(ini, DQN_INI_GLOBAL_SECTION, property, 0);
	char buf[DQN_64BIT_NUM_MAX_STR_SIZE] = {};
	Dqn_I64ToStr(value, buf, DQN_ARRAY_COUNT(buf));
	if (index == DQN_INI_NOT_FOUND)
	{
		DqnIni_PropertyAdd(ini, DQN_INI_GLOBAL_SECTION, property, 0, buf, 0);
	}
	else
	{
		DqnIni_PropertyValueSet(ini, DQN_INI_GLOBAL_SECTION, index, buf, 0);
	}

	return true;
}

void Config_WriteToDisk(WinjumpState *state)
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

	if (!DqnWin32_WCharToUTF8(logFont.lfFaceName, fontName, fontNameMaxLen))
	{
		OutputDebugString(
		    "DqnWin32_wchar_to_utf8() failed: FontName will not be stored "
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
	if (!DqnFile_Open(
	        GLOBAL_STRING_INI_CONFIG_PATH, &config,
	        (DqnFilePermissionFlag_Read | DqnFilePermissionFlag_Write),
	        DqnFileAction_ClearIfExist))
	{
		// Then file does not exist
		if (!DqnFile_Open(
		        GLOBAL_STRING_INI_CONFIG_PATH, &config,
		        (DqnFilePermissionFlag_Read | DqnFilePermissionFlag_Write),
		        DqnFileAction_CreateIfNotExist))
		{
			OutputDebugString(
			    "DqnFile_Open() failed: Platform was unable to create "
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
		ini = DqnIni_Create(NULL);
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
		WriteToIniString(ini, GLOBAL_STRING_INI_FONT_NAME, fontName);
	}

	WriteToIniInt(ini, GLOBAL_STRING_INI_FONT_HEIGHT,           fontHeight);
	WriteToIniInt(ini, GLOBAL_STRING_INI_FONT_WEIGHT,           fontWeight);
	WriteToIniInt(ini, GLOBAL_STRING_INI_FONT_ITALIC,           fontItalic);
	WriteToIniInt(ini, GLOBAL_STRING_INI_FONT_PITCH_AND_FAMILY, fontPitchAndFamily);
	WriteToIniInt(ini, GLOBAL_STRING_INI_FONT_CHAR_SET,         fontCharSet);
	WriteToIniInt(ini, GLOBAL_STRING_INI_FONT_OUT_PRECISION,    fontOutPrecision);
	WriteToIniInt(ini, GLOBAL_STRING_INI_FONT_CLIP_PRECISION,   fontClipPrecision);
	WriteToIniInt(ini, GLOBAL_STRING_INI_FONT_QUALITY,          fontQuality);

	////////////////////////////////////////////////////////////////////////////
	// Write Global Hotkey Data
	////////////////////////////////////////////////////////////////////////////
	AppHotkey hotkey = state->appHotkey;
	WriteToIniInt(ini, GLOBAL_STRING_INI_HOTKEY_VIRTUAL_KEY, hotkey.win32VirtualKey);
	WriteToIniInt(ini, GLOBAL_STRING_INI_HOTKEY_MODIFIER,    hotkey.win32ModifierKey);

	////////////////////////////////////////////////////////////////////////
	// Write ini to disk
	////////////////////////////////////////////////////////////////////////
	i32 requiredSize   = DqnIni_Save(ini, NULL, 0);
	u8 *dataToWriteOut = (u8 *)calloc(1, requiredSize);
	DqnIni_Save(ini, (char *)dataToWriteOut, requiredSize);
	DqnFile_Write(&config, dataToWriteOut, requiredSize, 0);
}

