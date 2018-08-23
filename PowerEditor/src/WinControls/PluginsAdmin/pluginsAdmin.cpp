// This file is part of Notepad++ project
// Copyright (C)2017 Don HO <don.h@free.fr>
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
//
// Note that the GPL places important restrictions on "derived works", yet
// it does not provide a detailed definition of that term.  To avoid
// misunderstandings, we consider an application to constitute a
// "derivative work" for the purpose of this license if it does any of the
// following:
// 1. Integrates source code from Notepad++.
// 2. Integrates/includes/aggregates Notepad++ into a proprietary executable
//    installer, such as those produced by InstallShield.
// 3. Links to a library or executes a program that does any of the above.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include "json.hpp"
#include <algorithm>
#include <iostream>
#include <fstream>
#include <string>
#include <cctype>
#include <shlobj.h>
#include <shlwapi.h>
#include <uxtheme.h>
#include "pluginsAdmin.h"
#include "ScintillaEditView.h"
#include "localization.h"
#include "Processus.h"
#include "PluginsManager.h"
#include "md5.h"
#include "verifySignedFile.h"

using namespace std;
using nlohmann::json;

void Version::setVersionFrom(generic_string filePath)
{
	if (not filePath.empty() && ::PathFileExists(filePath.c_str()))
	{
		DWORD handle;
		DWORD bufferSize = ::GetFileVersionInfoSize(filePath.c_str(), &handle);

		if (bufferSize <= 0)
			return;

		unsigned char* buffer = new unsigned char[bufferSize];
		::GetFileVersionInfo(filePath.c_str(), handle, bufferSize, buffer);

		VS_FIXEDFILEINFO* lpFileInfo;
		UINT cbFileInfo = 0;
		VerQueryValue(buffer, TEXT("\\"), (LPVOID*)&lpFileInfo, &cbFileInfo);
		if (cbFileInfo)
		{
			_major = (lpFileInfo->dwFileVersionMS & 0xFFFF0000) >> 16;
			_minor = lpFileInfo->dwFileVersionMS & 0x0000FFFF;
			_patch = (lpFileInfo->dwFileVersionLS & 0xFFFF0000) >> 16;
			_build = lpFileInfo->dwFileVersionLS & 0x0000FFFF;
		}
	}
}

generic_string Version::toString()
{
	if (_build == 0 && _patch == 0 && _minor == 0 && _major == 0) // ""
	{
		return TEXT("");
	}	
	else if (_build == 0 && _patch == 0 && _minor == 0) // "major"
	{
		return std::to_wstring(_major);
	}
	else if (_build == 0 && _patch == 0) // "major.minor"
	{
		std::wstring v = std::to_wstring(_major);
		v += TEXT(".");
		v += std::to_wstring(_minor);
		return v;
	}
	else if (_build == 0) // "major.minor.patch"
	{
		std::wstring v = std::to_wstring(_major);
		v += TEXT(".");
		v += std::to_wstring(_minor);
		v += TEXT(".");
		v += std::to_wstring(_patch);
		return v;
	}

	// "major.minor.patch.build"
	std::wstring ver = std::to_wstring(_major);
	ver += TEXT(".");
	ver += std::to_wstring(_minor);
	ver += TEXT(".");
	ver += std::to_wstring(_patch);
	ver += TEXT(".");
	ver += std::to_wstring(_build);

	return ver;
}

generic_string PluginUpdateInfo::describe()
{
	generic_string desc;
	const TCHAR *EOL = TEXT("\r\n");
	if (not _description.empty())
	{
		desc = _description;
		desc += EOL;
	}

	if (not _author.empty())
	{
		desc += TEXT("Author: ");
		desc += _author;
		desc += EOL;
	}

	if (not _homepage.empty())
	{
		desc += TEXT("Homepage: ");
		desc += _homepage;
		desc += EOL;
	}

	return desc;
}

/// Try to find in the Haystack the Needle - ignore case
bool findStrNoCase(const generic_string & strHaystack, const generic_string & strNeedle)
{
	auto it = std::search(
		strHaystack.begin(), strHaystack.end(),
		strNeedle.begin(), strNeedle.end(),
		[](TCHAR ch1, TCHAR ch2){return _totupper(ch1) == _totupper(ch2); }
	);
	return (it != strHaystack.end());
}

bool PluginsAdminDlg::isFoundInAvailableListFromIndex(int index, generic_string str2search, bool inWhichPart) const
{
	PluginUpdateInfo* pui = _availableList.getPluginInfoFromUiIndex(index);
	generic_string searchIn;
	if (inWhichPart == _inNames)
		searchIn = pui->_displayName;
	else //(inWhichPart == inDescs)
		searchIn = pui->_description;

	return (findStrNoCase(searchIn, str2search));
}

long PluginsAdminDlg::searchFromCurrentSel(generic_string str2search, bool inWhichPart, bool isNextMode) const
{
	// search from curent selected item or from the beginning
	long currentIndex = _availableList.getSelectedIndex();
	int nbItem = static_cast<int>(_availableList.nbItem());
	if (currentIndex == -1)
	{
		// no selection, let's search from 0 to the end
		for (int i = 0; i < nbItem; ++i)
		{
			if (isFoundInAvailableListFromIndex(i, str2search, inWhichPart))
				return i;
		}
	}
	else // with selection, let's search from currentIndex
	{
		// from current position to the end
		for (int i = currentIndex + (isNextMode ? 1 : 0); i < nbItem; ++i)
		{
			if (isFoundInAvailableListFromIndex(i, str2search, inWhichPart))
				return i;
		}

		// from to begining to current position
		for (int i = 0; i < currentIndex + (isNextMode ? 1 : 0); ++i)
		{
			if (isFoundInAvailableListFromIndex(i, str2search, inWhichPart))
				return i;
		}
	}
	return -1;
}

void PluginsAdminDlg::create(int dialogID, bool isRTL, bool msgDestParent)
{
	// get plugin installation path and launch mode (Admin or normal)
	collectNppCurrentStatusInfos();

	StaticDialog::create(dialogID, isRTL, msgDestParent);

	RECT rect;
	getClientRect(rect);
	_tab.init(_hInst, _hSelf, false, true);
	int tabDpiDynamicalHeight = NppParameters::getInstance()->_dpiManager.scaleY(13);
	_tab.setFont(TEXT("Tahoma"), tabDpiDynamicalHeight);

	const TCHAR *available = TEXT("Available");
	const TCHAR *updates = TEXT("Updates");
	const TCHAR *installed = TEXT("Installed");

	_tab.insertAtEnd(available);
	_tab.insertAtEnd(updates);
	_tab.insertAtEnd(installed);

	rect.bottom -= 100;;
	_tab.reSizeTo(rect);
	_tab.display();

	const long marge = 10;

	const int topMarge = 42;

	HWND hResearchLabel = ::GetDlgItem(_hSelf, IDC_PLUGINADM_RESEARCH_STATIC);
	RECT researchLabelRect;
	::GetClientRect(hResearchLabel, &researchLabelRect);
	researchLabelRect.left = rect.left;
	researchLabelRect.top = topMarge + 2;
	::MoveWindow(hResearchLabel, researchLabelRect.left, researchLabelRect.top, researchLabelRect.right, researchLabelRect.bottom, TRUE);
	::InvalidateRect(hResearchLabel, nullptr, TRUE);

	HWND hResearchEdit = ::GetDlgItem(_hSelf, IDC_PLUGINADM_RESEARCH_EDIT);
	RECT researchEditRect;
	::GetClientRect(hResearchEdit, &researchEditRect);
	researchEditRect.left = researchLabelRect.right + marge;
	researchEditRect.top = topMarge;
	::MoveWindow(hResearchEdit, researchEditRect.left, researchEditRect.top, researchEditRect.right, researchEditRect.bottom, TRUE);
	::InvalidateRect(hResearchEdit, nullptr, TRUE);

	HWND hNextButton = ::GetDlgItem(_hSelf, IDC_PLUGINADM_RESEARCH_NEXT);
	RECT researchNextRect;
	::GetClientRect(hNextButton, &researchNextRect);
	researchNextRect.left = researchEditRect.left + researchEditRect.right + marge;
	researchNextRect.top = topMarge;
	::MoveWindow(hNextButton, researchNextRect.left, researchNextRect.top, researchNextRect.right, researchNextRect.bottom, TRUE);
	::InvalidateRect(hNextButton, nullptr, TRUE);

	HWND hActionButton = ::GetDlgItem(_hSelf, IDC_PLUGINADM_INSTALL);
	RECT actionRect;
	::GetClientRect(hActionButton, &actionRect);
	long w = actionRect.right - actionRect.left;
	actionRect.left = rect.right - w - marge;
	actionRect.top = topMarge;
	::MoveWindow(hActionButton, actionRect.left, actionRect.top, actionRect.right, actionRect.bottom, TRUE);
	::InvalidateRect(hActionButton, nullptr, TRUE);

	hActionButton = ::GetDlgItem(_hSelf, IDC_PLUGINADM_UPDATE);
	::MoveWindow(hActionButton, actionRect.left, actionRect.top, actionRect.right, actionRect.bottom, TRUE);
	::InvalidateRect(hActionButton, nullptr, TRUE);

	hActionButton = ::GetDlgItem(_hSelf, IDC_PLUGINADM_REMOVE);
	::MoveWindow(hActionButton, actionRect.left, actionRect.top, actionRect.right, actionRect.bottom, TRUE);
	::InvalidateRect(hActionButton, nullptr, TRUE);

	long actionZoneHeight = 50;
	rect.top += actionZoneHeight;
	rect.bottom -= actionZoneHeight;

	RECT listRect = rect;
	RECT descRect = rect;

	long descHeight = rect.bottom / 3 - marge * 2;
	long listHeight = (rect.bottom / 3) * 2 - marge * 3;

	listRect.top += marge;
	listRect.bottom = listHeight;
	listRect.left += marge;
	listRect.right -= marge * 2;

	descRect.top += listHeight + marge * 3;
	descRect.bottom = descHeight;
	descRect.left += marge;
	descRect.right -= marge * 2;

	NppParameters *nppParam = NppParameters::getInstance();
	NativeLangSpeaker *pNativeSpeaker = nppParam->getNativeLangSpeaker();
	generic_string pluginStr = pNativeSpeaker->getAttrNameStr(TEXT("Plugin"), "PluginAdmin", "Plugin");
	generic_string vesionStr = pNativeSpeaker->getAttrNameStr(TEXT("Version"), "PluginAdmin", "Version");
	generic_string stabilityStr = pNativeSpeaker->getAttrNameStr(TEXT("Stability"), "PluginAdmin", "Stability");

	_availableList.addColumn(columnInfo(pluginStr, nppParam->_dpiManager.scaleX(200)));
	_availableList.addColumn(columnInfo(vesionStr, nppParam->_dpiManager.scaleX(100)));
	_availableList.addColumn(columnInfo(stabilityStr, nppParam->_dpiManager.scaleX(70)));
	_availableList.setViewStyleOption(LVS_EX_CHECKBOXES);

	_availableList.initView(_hInst, _hSelf);
	_availableList.reSizeView(listRect);
	
	_updateList.addColumn(columnInfo(pluginStr, nppParam->_dpiManager.scaleX(200)));
	_updateList.addColumn(columnInfo(vesionStr, nppParam->_dpiManager.scaleX(100)));
	_updateList.addColumn(columnInfo(stabilityStr, nppParam->_dpiManager.scaleX(70)));
	_updateList.setViewStyleOption(LVS_EX_CHECKBOXES);

	_updateList.initView(_hInst, _hSelf);
	_updateList.reSizeView(listRect);

	_installedList.addColumn(columnInfo(pluginStr, nppParam->_dpiManager.scaleX(200)));
	_installedList.addColumn(columnInfo(vesionStr, nppParam->_dpiManager.scaleX(100)));
	_installedList.addColumn(columnInfo(stabilityStr, nppParam->_dpiManager.scaleX(70)));
	_installedList.setViewStyleOption(LVS_EX_CHECKBOXES);

	_installedList.initView(_hInst, _hSelf);
	_installedList.reSizeView(listRect);

	HWND hDesc = ::GetDlgItem(_hSelf, IDC_PLUGINADM_EDIT);
	::MoveWindow(hDesc, descRect.left, descRect.top, descRect.right, descRect.bottom, TRUE);
	::InvalidateRect(hDesc, nullptr, TRUE);

	switchDialog(0);

	ETDTProc enableDlgTheme = (ETDTProc)::SendMessage(_hParent, NPPM_GETENABLETHEMETEXTUREFUNC, 0, 0);
	if (enableDlgTheme)
		enableDlgTheme(_hSelf, ETDT_ENABLETAB);

	goToCenter();
}

void PluginsAdminDlg::collectNppCurrentStatusInfos()
{
	NppParameters *pNppParam = NppParameters::getInstance();
	_nppCurrentStatus._nppInstallPath = pNppParam->getNppPath();

	_nppCurrentStatus._isAppDataPluginsAllowed = ::SendMessage(_hParent, NPPM_GETAPPDATAPLUGINSALLOWED, 0, 0) == TRUE;
	_nppCurrentStatus._appdataPath = pNppParam->getAppDataNppDir();
	generic_string programFilesPath = NppParameters::getSpecialFolderLocation(CSIDL_PROGRAM_FILES);
	_nppCurrentStatus._isInProgramFiles = (_nppCurrentStatus._nppInstallPath.find(programFilesPath) == 0);

}

vector<PluginUpdateInfo*> PluginViewList::fromUiIndexesToPluginInfos(const std::vector<size_t>& uiIndexes) const
{
	std::vector<PluginUpdateInfo*> r;

	for (auto i : uiIndexes)
	{
		if (i < _ui.nbItem())
		{
			r.push_back(getPluginInfoFromUiIndex(i));
		}
	}
	return r;
}

PluginsAdminDlg::PluginsAdminDlg()
{
	NppParameters *pNppParameters = NppParameters::getInstance();
	_updaterDir = pNppParameters->getNppPath();
	PathAppend(_updaterDir, TEXT("updater"));

	_updaterFullPath = _updaterDir;
	PathAppend(_updaterFullPath, TEXT("gup.exe"));

#ifdef DEBUG // if not debug, then it's release

	// load from nppPluginList.json instead of nppPluginList.dll
	_pluginListFullPath = TEXT("C:\\tmp\\nppPluginList.json");

#else //RELEASE

#ifdef _WIN64
	_pluginListFullPath = TEXT("C:\\sources\\nppPluginList\\vcxproj\\x64\\Debug\\nppPluginList.dll");
#else
	_pluginListFullPath = TEXT("C:\\sources\\nppPluginList\\vcxproj\\Debug\\nppPluginList.dll");
#endif

#endif
	;
}

bool PluginsAdminDlg::installPlugins()
{
	vector<size_t> indexes = _availableList.getCheckedIndexes();
	vector<PluginUpdateInfo*> puis = _availableList.fromUiIndexesToPluginInfos(indexes);

	generic_string updaterParams = TEXT("-unzipTo ");
	NppParameters *pNppParameters = NppParameters::getInstance();
	generic_string nppPluginsDir;

	if (pNppParameters->isLocal())
	{
		nppPluginsDir = pNppParameters->getNppPath();
	}
	else
	{
		nppPluginsDir = pNppParameters->getLocalAppDataNppDir();
	}

	PathAppend(nppPluginsDir, TEXT("plugins"));

	if (!::PathFileExists(nppPluginsDir.c_str()))
	{
		::CreateDirectory(nppPluginsDir.c_str(), NULL);
	}

	generic_string quoted_nppPluginsDir = TEXT("\"");
	quoted_nppPluginsDir += nppPluginsDir;
	quoted_nppPluginsDir += TEXT("\"");

	for (auto i : puis)
	{
		updaterParams += quoted_nppPluginsDir;

		// add zipFile's url
		updaterParams += TEXT(" ");
		updaterParams += i->_repository;

		Process updater(_updaterFullPath.c_str(), updaterParams.c_str(), _updaterDir.c_str());

		int result = updater.runSync();
		if (result == 0) // wingup return 0 -> OK
		{
			// Remove (Hide) installed plugin from available list
			_availableList.hideFromPluginInfoPtr(i);

			// Add installed plugin into insttalled list
			PluginUpdateInfo* installedPui = new PluginUpdateInfo(*i);
			installedPui->_isVisible = true;
			_installedList.pushBack(installedPui);
		}
		else // wingup return non-zero (-1) -> Not OK
		{
			// just move on
		}
	}

	return true;
}

bool PluginsAdminDlg::updatePlugins()
{
	vector<size_t> indexes = _updateList.getCheckedIndexes();
	vector<PluginUpdateInfo*> puis = _updateList.fromUiIndexesToPluginInfos(indexes);

	NppParameters *pNppParameters = NppParameters::getInstance();
	generic_string updaterDir = pNppParameters->getNppPath();
	updaterDir += TEXT("\\updater\\");

	generic_string updaterFullPath = updaterDir + TEXT("gup.exe");
	generic_string updaterParams = TEXT("-unzipTo ");

	for (auto i : puis)
	{
		// add folder to operate
		generic_string destFolder = pNppParameters->getAppDataNppDir();
		PathAppend(destFolder, i->_folderName);

		updaterParams += destFolder;

		// add zipFile's url
		updaterParams += TEXT(" ");
		updaterParams += i->_repository;

		Process updater(updaterFullPath.c_str(), updaterParams.c_str(), updaterDir.c_str());
		int result = updater.runSync();
		if (result == 0) // wingup return 0 -> OK
		{
			// Remove updated plugin from update list
			_updateList.removeFromPluginInfoPtr(i);

		}
		else // wingup return non-zero (-1) -> Not OK
		{
			// just move on
		}
	}

	return true;
}

bool PluginsAdminDlg::removePlugins()
{
	vector<size_t> indexes = _installedList.getCheckedIndexes();
	vector<PluginUpdateInfo*> puis = _updateList.fromUiIndexesToPluginInfos(indexes);

	NppParameters *pNppParameters = NppParameters::getInstance();
	generic_string updaterDir = pNppParameters->getNppPath();
	updaterDir += TEXT("\\updater\\");

	generic_string updaterFullPath = updaterDir + TEXT("gup.exe");
	generic_string updaterParams = TEXT("-clean ");

	for (auto i : puis)
	{
		// add folder to operate
		generic_string destFolder = pNppParameters->getAppDataNppDir();
		PathAppend(destFolder, i->_folderName); 

		updaterParams += destFolder;

		Process updater(updaterFullPath.c_str(), updaterParams.c_str(), updaterDir.c_str());
		int result = updater.runSync();
		if (result == 0) // wingup return 0 -> OK
		{
			// Remove removed plugin from installed list
			_installedList.removeFromPluginInfoPtr(i);

			// Remove removed plugin from update list eventually
			_updateList.removeFromFolderName(i->_folderName);

			// Add removed plugin back to available list (if available)
			_availableList.restore(i->_folderName);

		}
		else // wingup return non-zero (-1) -> Not OK
		{
			// just move on

		}
	}

	return true;
}

bool PluginViewList::removeFromFolderName(const generic_string& folderName)
{

	for (size_t i = 0; i < _ui.nbItem(); ++i)
	{
		PluginUpdateInfo* pi = getPluginInfoFromUiIndex(i);
		if (pi->_folderName == folderName)
		{
			if (!_ui.removeFromIndex(i))
				return false;

			for (size_t j = 0; j < _list.size(); ++j)
			{
				if (_list[j] == pi)
				{
					_list.erase(_list.begin() + j);
					return true;
				}
			}
		}
	}
	return false;
}

void PluginViewList::pushBack(PluginUpdateInfo* pi)
{
	_list.push_back(pi);

	vector<generic_string> values2Add;
	values2Add.push_back(pi->_displayName);
	Version v = pi->_version;
	values2Add.push_back(v.toString());
	values2Add.push_back(TEXT("Yes"));
	_ui.addLine(values2Add, reinterpret_cast<LPARAM>(pi));
}

bool loadFromJson(PluginViewList & pl, const json& j)
{
	if (j.empty())
		return false;

	WcharMbcsConvertor *wmc = WcharMbcsConvertor::getInstance();

	json jArray = j["npp-plugins"];
	if (jArray.empty() || jArray.type() != json::value_t::array)
		return false;
	
	for (const auto& i : jArray)
	{
		try {
			//std::unique_ptr<PluginUpdateInfo*> pi = make_unique<PluginUpdateInfo*>();
			PluginUpdateInfo* pi = new PluginUpdateInfo();

			string valStr = i.at("folder-name").get<std::string>();
			pi->_folderName = wmc->char2wchar(valStr.c_str(), CP_ACP);

			valStr = i.at("display-name").get<std::string>();
			pi->_displayName = wmc->char2wchar(valStr.c_str(), CP_ACP);

			valStr = i.at("author").get<std::string>();
			pi->_author = wmc->char2wchar(valStr.c_str(), CP_ACP);

			valStr = i.at("description").get<std::string>();
			pi->_description = wmc->char2wchar(valStr.c_str(), CP_ACP);

			valStr = i.at("repository").get<std::string>();
			pi->_repository = wmc->char2wchar(valStr.c_str(), CP_ACP);

			valStr = i.at("homepage").get<std::string>();
			pi->_homepage = wmc->char2wchar(valStr.c_str(), CP_ACP);


			pl.pushBack(pi);
		}
		catch (...) // Every field is mandatory. If one of property is missing, an exception is thrown then this plugin will be ignored
		{
			continue; 
		}
	}
	return true;
}

PluginUpdateInfo::PluginUpdateInfo(const generic_string& fullFilePath, const generic_string& filename)
{
	if (!::PathFileExists(fullFilePath.c_str()))
		return;

	_fullFilePath = fullFilePath;
	_displayName = filename;

	WcharMbcsConvertor *wmc = WcharMbcsConvertor::getInstance();
	const char *path = wmc->wchar2char(fullFilePath.c_str(), CP_ACP);
	MD5 md5;
	_id = wmc->char2wchar(md5.digestFile(path), CP_ACP);

	_version.setVersionFrom(fullFilePath);

}

typedef const char * (__cdecl * PFUNCGETPLUGINLIST)();


bool PluginsAdminDlg::isValide()
{
	if (!::PathFileExists(_pluginListFullPath.c_str()))
	{
		return false;
	}

	generic_string gupPath;
	if (!::PathFileExists(_updaterFullPath.c_str()))
	{
		return false;
	}

#ifdef DEBUG // if not debug, then it's release
	
	return true;

#else //RELEASE

	// check the signature on default location : %APPDATA%\Notepad++\plugins\config\pl\nppPluginList.dll or NPP_INST_DIR\plugins\config\pl\nppPluginList.dll
	
	bool isOK = VerifySignedLibrary(_pluginListFullPath.c_str(), NPP_COMPONENT_SIGNER_KEY_ID, NPP_COMPONENT_SIGNER_SUBJECT, NPP_COMPONENT_SIGNER_DISPLAY_NAME, false, false, false);
	if (!isOK)
		return isOK;

	isOK = VerifySignedLibrary(_updaterFullPath.c_str(), NPP_COMPONENT_SIGNER_KEY_ID, NPP_COMPONENT_SIGNER_SUBJECT, NPP_COMPONENT_SIGNER_DISPLAY_NAME, false, false, false);
	return isOK;
#endif
}

bool PluginsAdminDlg::updateListAndLoadFromJson()
{
	try {
		if (!isValide())
			return false;

		json j;

#ifdef DEBUG // if not debug, then it's release

		// load from nppPluginList.json instead of nppPluginList.dll
		ifstream nppPluginListJson(_pluginListFullPath);
		nppPluginListJson >> j;

#else //RELEASE

		HINSTANCE hLib = ::LoadLibrary(_pluginListFullPath.c_str());
		if (!hLib)
		{
			// Error treatment
			//printStr(TEXT("LoadLibrary PB!!!"));
			return false;
		}

		PFUNCGETPLUGINLIST pGetListFunc = (PFUNCGETPLUGINLIST)GetProcAddress(hLib, "getList");
		if (!pGetListFunc)
		{
			// Error treatment
			//printStr(TEXT("getList PB!!!"));
			return false;
		}

		const char* pl = pGetListFunc();
		//MessageBoxA(NULL, pl, "", MB_OK);

		j = j.parse(pl);

#endif
		// if absent then download it


		// check the update for nppPluginList.json


		// download update if present


		// load pl.json
		// 

		loadFromJson(_availableList, j);

		// initialize update list view
		checkUpdates();

		// initialize installed list view
		loadFromPluginInfos();

		return true;
	}
	catch (...)
	{
		// whichever exception
		return false;
	}
}


bool PluginsAdminDlg::loadFromPluginInfos()
{
	if (!_pPluginsManager)
		return false;

	for (const auto& i : _pPluginsManager->_loadedDlls)
	{
		if (i._fileName.length() >= MAX_PATH)
			continue;

		// user file name (without ext. to find whole info in available list
		TCHAR fnNoExt[MAX_PATH];
		lstrcpy(fnNoExt, i._fileName.c_str());
		::PathRemoveExtension(fnNoExt);

		int listIndex;
		PluginUpdateInfo* foundInfo = _availableList.findPluginInfoFromFolderName(fnNoExt, listIndex);
		if (!foundInfo)
		{
			PluginUpdateInfo* pui = new PluginUpdateInfo(i._fullFilePath, i._fileName);
			_installedList.pushBack(pui);
		}
		else
		{
			// Add new updated info to installed list
			PluginUpdateInfo* pui = new PluginUpdateInfo(*foundInfo);
			pui->_fullFilePath = i._fullFilePath;
			pui->_version.setVersionFrom(i._fullFilePath);
			_installedList.pushBack(pui);

			// Hide it from the available list
			_availableList.hideFromListIndex(listIndex);
		}
	}

	return true;
}

PluginUpdateInfo* PluginViewList::findPluginInfoFromFolderName(const generic_string& folderName, int& index) const
{
	index = 0;
	for (const auto& i : _list)
	{
		if (lstrcmpi(i->_folderName.c_str(), folderName.c_str()) == 0)
			return i;
		++index;
	}
	index = -1;
	return nullptr;
}

bool PluginViewList::removeFromUiIndex(size_t index2remove)
{
	if (index2remove >= _ui.nbItem())
		return false;
	return _ui.removeFromIndex(index2remove);
}

bool PluginViewList::removeFromListIndex(size_t index2remove)
{
	if (index2remove >= _list.size())
		return false;

	for (size_t i = 0; i < _ui.nbItem(); ++i)
	{
		if (_ui.getLParamFromIndex(static_cast<int>(i)) == reinterpret_cast<LPARAM>(_list[index2remove]))
		{
			if (!_ui.removeFromIndex(i))
				return false;
		}
	}
	
	_list.erase(_list.begin() + index2remove);

	return true;
}

bool PluginViewList::removeFromPluginInfoPtr(PluginUpdateInfo* pluginInfo2hide)
{
	for (size_t i = 0; i < _ui.nbItem(); ++i)
	{
		if (_ui.getLParamFromIndex(static_cast<int>(i)) == reinterpret_cast<LPARAM>(pluginInfo2hide))
		{
			if (!_ui.removeFromIndex(static_cast<int>(i)))
			{
				return false;
			}
		}
	}

	for (size_t j = 0; j < _list.size(); ++j)
	{
		if (_list[j] == pluginInfo2hide)
		{
			_list.erase(_list.begin() + j);
			return true;
		}
	}

	return false;
}

bool PluginViewList::hideFromPluginInfoPtr(PluginUpdateInfo* pluginInfo2hide)
{
	for (size_t i = 0; i < _ui.nbItem(); ++i)
	{
		if (_ui.getLParamFromIndex(static_cast<int>(i)) == reinterpret_cast<LPARAM>(pluginInfo2hide))
		{
			if (!_ui.removeFromIndex(static_cast<int>(i)))
			{
				return false;
			}
			else
			{
				pluginInfo2hide->_isVisible = false;
				return true;
			}
		}
	}
	return false;
}

bool PluginViewList::restore(const generic_string& folderName)
{
	for (auto i : _list)
	{
		if (i->_folderName == folderName)
		{
			vector<generic_string> values2Add;
			values2Add.push_back(i->_displayName);
			Version v = i->_version;
			values2Add.push_back(v.toString());
			values2Add.push_back(TEXT("Yes"));
			_ui.addLine(values2Add, reinterpret_cast<LPARAM>(i));

			i->_isVisible = true;

			return true;
		}
	}
	return false;
}

bool PluginViewList::hideFromListIndex(size_t index2hide)
{
	if (index2hide >= _list.size())
		return false;

	for (size_t i = 0; i < _ui.nbItem(); ++i)
	{
		if (_ui.getLParamFromIndex(static_cast<int>(i)) == reinterpret_cast<LPARAM>(_list[index2hide]))
		{
			if (!_ui.removeFromIndex(static_cast<int>(i)))
				return false;
		}
	}

	_list[index2hide]->_isVisible = false;

	return true;
}

bool PluginsAdminDlg::checkUpdates()
{
	return true;
}

// begin insentive-case search from the second key-in character
bool PluginsAdminDlg::searchInPlugins(bool isNextMode) const
{
	const int maxLen = 256;
	TCHAR txt2search[maxLen];
	::GetDlgItemText(_hSelf, IDC_PLUGINADM_RESEARCH_EDIT, txt2search, maxLen);
	if (lstrlen(txt2search) < 2)
		return false;

	long foundIndex = searchInNamesFromCurrentSel(txt2search, isNextMode);
	if (foundIndex == -1)
		foundIndex = searchInDescsFromCurrentSel(txt2search, isNextMode);

	if (foundIndex == -1)
		return false;

	_availableList.setSelection(foundIndex);

	return true;
}

void PluginsAdminDlg::switchDialog(int indexToSwitch)
{
	generic_string desc;
	bool showAvailable, showUpdate, showInstalled;
	switch (indexToSwitch)
	{
		case 0: // available plugins
		{
			showAvailable = true;
			showUpdate = false;
			showInstalled = false;

			long infoIndex = _availableList.getSelectedIndex();
			if (infoIndex != -1 && infoIndex < static_cast<long>(_availableList.nbItem()))
				desc = _availableList.getPluginInfoFromUiIndex(infoIndex)->describe();
		}
		break;

		case 1: // to be updated plugins
		{
			showAvailable = false;
			showUpdate = true;
			showInstalled = false;
			
			long infoIndex = _updateList.getSelectedIndex();
			if (infoIndex != -1 && infoIndex < static_cast<long>(_updateList.nbItem()))
				desc = _updateList.getPluginInfoFromUiIndex(infoIndex)->describe();
		}
		break;

		case 2: // installed plugin
		{
			showAvailable = false;
			showUpdate = false;
			showInstalled = true;

			long infoIndex = _installedList.getSelectedIndex();
			if (infoIndex != -1 && infoIndex < static_cast<long>(_installedList.nbItem()))
				desc = _installedList.getPluginInfoFromUiIndex(infoIndex)->describe();
		}
		break;

		default:
			return;
	}

	_availableList.displayView(showAvailable);
	_updateList.displayView(showUpdate);
	_installedList.displayView(showInstalled);

	::SetDlgItemText(_hSelf, IDC_PLUGINADM_EDIT, desc.c_str());

	HWND hInstallButton = ::GetDlgItem(_hSelf, IDC_PLUGINADM_INSTALL);
	HWND hUpdateButton = ::GetDlgItem(_hSelf, IDC_PLUGINADM_UPDATE);
	HWND hRemoveButton = ::GetDlgItem(_hSelf, IDC_PLUGINADM_REMOVE);

	::ShowWindow(hInstallButton, showAvailable ? SW_SHOW : SW_HIDE);
	if (showAvailable)
	{
		vector<size_t> checkedArray = _availableList.getCheckedIndexes();
		showAvailable = checkedArray.size() > 0;
	}
	::EnableWindow(hInstallButton, showAvailable);

	::ShowWindow(hUpdateButton, showUpdate ? SW_SHOW : SW_HIDE);
	if (showUpdate)
	{
		vector<size_t> checkedArray = _updateList.getCheckedIndexes();
		showUpdate = checkedArray.size() > 0;
	}
	::EnableWindow(hUpdateButton, showUpdate);

	::ShowWindow(hRemoveButton, showInstalled ? SW_SHOW : SW_HIDE);
	if (showInstalled)
	{
		vector<size_t> checkedArray = _installedList.getCheckedIndexes();
		showInstalled = checkedArray.size() > 0;
	}
	::EnableWindow(hRemoveButton, showInstalled);
}

INT_PTR CALLBACK PluginsAdminDlg::run_dlgProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
        case WM_INITDIALOG :
		{

			return TRUE;
		}

		case WM_COMMAND :
		{
			if (HIWORD(wParam) == EN_CHANGE)
			{
				switch (LOWORD(wParam))
				{
					case  IDC_PLUGINADM_RESEARCH_EDIT:
					{
						searchInPlugins(false);
						return TRUE;
					}
				}
			}

			switch (wParam)
			{
				case IDCANCEL :
				case IDOK :
					display(false);
					return TRUE;

				case IDC_PLUGINADM_RESEARCH_NEXT:
					searchInPlugins(true);
					return true;

				case IDC_PLUGINADM_INSTALL:
					installPlugins();
					return true;

				case IDC_PLUGINADM_UPDATE:
					updatePlugins();
					return true;

				case IDC_PLUGINADM_REMOVE:
				{
					removePlugins();
					return true;
				}

				default :
					break;
			}
			return FALSE;
		}

		case WM_NOTIFY :
		{
			LPNMHDR pnmh = reinterpret_cast<LPNMHDR>(lParam);
			if (pnmh->code == TCN_SELCHANGE)
			{
				HWND tabHandle = _tab.getHSelf();
				if (pnmh->hwndFrom == tabHandle)
				{
					int indexClicked = int(::SendMessage(tabHandle, TCM_GETCURSEL, 0, 0));
					switchDialog(indexClicked);
				}
			}
			else if (pnmh->hwndFrom == _availableList.getViewHwnd() || 
                     pnmh->hwndFrom == _updateList.getViewHwnd() ||
                     pnmh->hwndFrom == _installedList.getViewHwnd())
			{
				PluginViewList* pViewList;
				int buttonID;

				if (pnmh->hwndFrom == _availableList.getViewHwnd())
				{
					pViewList = &_availableList;
					buttonID = IDC_PLUGINADM_INSTALL;
				}
				else if (pnmh->hwndFrom == _updateList.getViewHwnd())
				{
					pViewList = &_updateList;
					buttonID = IDC_PLUGINADM_UPDATE;
				}
				else // pnmh->hwndFrom == _installedList.getViewHwnd()
				{
					pViewList = &_installedList;
					buttonID = IDC_PLUGINADM_REMOVE;
				}

				LPNMLISTVIEW pnmv = (LPNMLISTVIEW)lParam;

				if (pnmh->code == LVN_ITEMCHANGED)
				{
					if (pnmv->uChanged & LVIF_STATE)
					{
						if ((pnmv->uNewState & LVIS_STATEIMAGEMASK) == INDEXTOSTATEIMAGEMASK(2) || // checked
							(pnmv->uNewState & LVIS_STATEIMAGEMASK) == INDEXTOSTATEIMAGEMASK(1))   // unchecked
						{
							HWND hButton = ::GetDlgItem(_hSelf, buttonID);
							vector<size_t> checkedArray = pViewList->getCheckedIndexes();
							bool showButton = checkedArray.size() > 0;

							::EnableWindow(hButton, showButton);
						}
						else if (pnmv->uNewState & LVIS_SELECTED)
						{
							PluginUpdateInfo* pui = pViewList->getPluginInfoFromUiIndex(pnmv->iItem);
							generic_string desc = pui->describe();
							::SetDlgItemText(_hSelf, IDC_PLUGINADM_EDIT, desc.c_str());
						}
					}
				}
			}

			return TRUE;
		}

		case WM_DESTROY :
		{
			return TRUE;
		}
	}
	return FALSE;
}

