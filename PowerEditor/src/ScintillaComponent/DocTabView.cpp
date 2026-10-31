// This file is part of Notepad++ project
// Copyright (C)2021 Don HO <don.h@free.fr>

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// at your option any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.


#include "DocTabView.h"

#include <windows.h>

#include <cwchar>
#include <iterator>

#include "Buffer.h"
#include "NppConstants.h"
#include "Parameters.h"
#include "ScintillaEditView.h"
#include "TabBar.h"
#include "resource.h"

enum ImgIdx
{
	SAVED_IMG_INDEX,
	UNSAVED_IMG_INDEX,
	REDONLY_IMG_INDEX,
	REDONLYSYS_IMG_INDEX,
	MONITORING_IMG_INDEX,
};

static constexpr int docTabIconIDs[] = { IDI_SAVED_ICON, IDI_UNSAVED_ICON, IDI_READONLY_ICON, IDI_READONLYSYS_ICON, IDI_MONITORING_ICON };
static constexpr int docTabIconIDs_darkMode[] = { IDI_SAVED_DM_ICON, IDI_UNSAVED_DM_ICON, IDI_READONLY_DM_ICON, IDI_READONLYSYS_DM_ICON, IDI_MONITORING_DM_ICON };
static constexpr int docTabIconIDs_alt[] = { IDI_SAVED_ALT_ICON, IDI_UNSAVED_ALT_ICON, IDI_READONLY_ALT_ICON, IDI_READONLYSYS_ALT_ICON, IDI_MONITORING_ICON };

void DocTabView::init(HINSTANCE hInst, HWND parent, ScintillaEditView* pView, unsigned char indexChoice, unsigned char buttonsStatus)
{
	TabBarPlus::init(hInst, parent, false, false, buttonsStatus);
	_pView = pView;

	createIconSets();

	_pIconListVector.push_back(&_docTabIconList);         // 0
	_pIconListVector.push_back(&_docTabIconListAlt);      // 1
	_pIconListVector.push_back(&_docTabIconListDarkMode); // 2

	if (indexChoice >= _pIconListVector.size())
		_iconListIndexChoice = 0;
	else
		_iconListIndexChoice = indexChoice;

	if (_iconListIndexChoice != -1)
		TabBar::setImageList(_pIconListVector[_iconListIndexChoice]->getHandle());
	return;
}

void DocTabView::createIconSets()
{
	int iconDpiDynamicalSize = _dpiManager.scale(g_TabIconSize);

	_docTabIconList.create(iconDpiDynamicalSize, _hInst, docTabIconIDs, sizeof(docTabIconIDs) / sizeof(int));
	_docTabIconListAlt.create(iconDpiDynamicalSize, _hInst, docTabIconIDs_alt, sizeof(docTabIconIDs_alt) / sizeof(int));
	_docTabIconListDarkMode.create(iconDpiDynamicalSize, _hInst, docTabIconIDs_darkMode, sizeof(docTabIconIDs_darkMode) / sizeof(int));
}

// Duplicates every first ampersand(&): "test & test && test &&&" becomes "test && test &&& test &&&&".
// Tab's caption must be encoded like this because otherwise tab control would make tab too small or too big for the text.
// out must have room for 2 * MAX_PATH characters.
static void encodeTabLabel(const wchar_t* in, wchar_t* out)
{
	while (*in != 0)
	{
		if (*in == '&')
		{
			*out++ = '&';
			*out++ = '&';
			while (*(++in) == '&')
				*out++ = '&';
		}
		else
			*out++ = *in++;
	}
	*out = '\0';
}

void DocTabView::addBuffer(BufferID buffer)
{
	if (buffer == BUFFER_INVALID)	//valid only
		return;
	if (getIndexByBuffer(buffer) != -1)	//no duplicates
		return;
	const Buffer* buf = MainFileManager.getBufferByID(buffer);
	TCITEM tie{};
	tie.mask = TCIF_TEXT | TCIF_IMAGE | TCIF_PARAM;

	int index = -1;
	if (_hasImgLst)
		index = 0;
	tie.iImage = index;
	// Insert with the final (encoded) label, so the bufferUpdated() below usually has nothing left to change.
	wchar_t encodedLabel[2 * MAX_PATH] = { '\0' };
	encodeTabLabel(buf->getCompactFileName(), encodedLabel);
	tie.pszText = encodedLabel;
	tie.lParam = reinterpret_cast<LPARAM>(buffer);
	applyBatchFixedWidth();
	int insertedIndex = static_cast<int>(::SendMessage(_hSelf, TCM_INSERTITEM, _nbItem++, reinterpret_cast<LPARAM>(&tie)));
	addBufferToIndex(buffer);
	if (insertedIndex >= 0)
		_lastFoundIndex = insertedIndex;
	bufferUpdated(buf, BufferChangeMask);

	requestParentResize(_hParent);
}

void DocTabView::closeBuffer(BufferID buffer)
{
	int indexToClose = getIndexByBuffer(buffer);
	if (indexToClose == -1)
		return;
	deletItemAt(static_cast<size_t>(indexToClose));
	requestParentResize(_hParent);
}

void DocTabView::deletItemAt(size_t index)
{
	if (index >= _nbItem)
		return;
	removeBufferFromIndex(getBufferByIndex(index));
	applyBatchFixedWidth();
	TabBar::deletItemAt(index);
	_lastFoundIndex = -1;
}

void DocTabView::deletAllItem()
{
	TabBar::deletAllItem();
	_bufferTabCount.clear();
	_lastFoundIndex = -1;
}

void DocTabView::removeBufferFromIndex(BufferID id)
{
	auto it = _bufferTabCount.find(id);
	if (it == _bufferTabCount.end())
		return;
	if (--(it->second) <= 0)
		_bufferTabCount.erase(it);
}

void DocTabView::applyBatchFixedWidth()
{
	if (_batchUpdateDepth == 0 || _isBatchFixedWidthApplied || !_hSelf)
		return;

	LONG_PTR style = ::GetWindowLongPtr(_hSelf, GWL_STYLE);
	if (style & TCS_FIXEDWIDTH)
		return; // already fixed width, nothing to gain

	::SendMessage(_hSelf, WM_SETREDRAW, FALSE, 0); // nothing to paint until the batch is done
	::SetWindowLongPtr(_hSelf, GWL_STYLE, style | TCS_FIXEDWIDTH);
	_isBatchFixedWidthApplied = true;
	_batchFixedWidthTabs.push_back(this);
}

void DocTabView::restoreBatchFixedWidth()
{
	if (!_isBatchFixedWidthApplied)
		return;

	_isBatchFixedWidthApplied = false;
	LONG_PTR style = ::GetWindowLongPtr(_hSelf, GWL_STYLE);
	::SetWindowLongPtr(_hSelf, GWL_STYLE, style & ~static_cast<LONG_PTR>(TCS_FIXEDWIDTH));
	refresh(); // forces the tab control to re-measure all the tabs with the restored style
	::SendMessage(_hSelf, WM_SETREDRAW, TRUE, 0);
	::InvalidateRect(_hSelf, nullptr, TRUE);
}

void DocTabView::endBatchUpdate()
{
	if (_batchUpdateDepth > 0)
		--_batchUpdateDepth;

	if (_batchUpdateDepth == 0)
	{
		std::vector<DocTabView*> tabs;
		tabs.swap(_batchFixedWidthTabs);
		for (DocTabView* pTab : tabs)
		{
			pTab->restoreBatchFixedWidth();
			_hPendingResizeParent = pTab->_hParent; // the tab widths changed, so a relayout is due
		}
	}

	if (_batchUpdateDepth == 0 && _hPendingResizeParent)
	{
		HWND hParent = _hPendingResizeParent;
		_hPendingResizeParent = nullptr;
		::SendMessage(hParent, WM_SIZE, 0, 0);
	}
}

void DocTabView::requestParentResize(HWND hParent)
{
	if (_batchUpdateDepth > 0)
		_hPendingResizeParent = hParent;
	else
		::SendMessage(hParent, WM_SIZE, 0, 0);
}

void DocTabView::setIndividualTabColour(BufferID bufferId, int colorId)
{
	bufferId->setDocColorId(colorId);
}

int DocTabView::getIndividualTabColourId(int tabIndex)
{
	BufferID bufferId = getBufferByIndex(tabIndex);
	if (!bufferId)
		return -1;

	return bufferId->getDocColorId();
}

bool DocTabView::activateBuffer(BufferID buffer)
{
	int indexToActivate = getIndexByBuffer(buffer);
	if (indexToActivate == -1)
		return false;	//cannot activate

	activateAt(indexToActivate);
	return true;
}


BufferID DocTabView::activeBuffer()
{
	int index = getCurrentTabIndex();
	return getBufferByIndex(index);
}


BufferID DocTabView::findBufferByName(const wchar_t * fullfilename) //-1 if not found, something else otherwise
{
	TCITEM tie{};
	tie.lParam = -1;
	tie.mask = TCIF_PARAM;
	for (size_t i = 0; i < _nbItem; ++i)
	{
		::SendMessage(_hSelf, TCM_GETITEM, i, reinterpret_cast<LPARAM>(&tie));
		BufferID id = reinterpret_cast<BufferID>(tie.lParam);
		const Buffer* buf = MainFileManager.getBufferByID(id);
		if (_wcsicmp(fullfilename, buf->getFullPathName()) == 0)
		{
			return id;
		}
	}
	return BUFFER_INVALID;
}


int DocTabView::getIndexByBuffer(BufferID id)
{
	// Fast path: the buffer is not in this view at all (the common case while opening files,
	// and for every buffer of the other view).
	if (!id || !isBufferInIndex(id))
		return -1;

	TCITEM tie{};
	tie.lParam = -1;
	tie.mask = TCIF_PARAM;

	auto isAt = [&](int i) -> bool {
		if (i < 0 || static_cast<size_t>(i) >= _nbItem)
			return false;
		tie.lParam = -1;
		::SendMessage(_hSelf, TCM_GETITEM, i, reinterpret_cast<LPARAM>(&tie));
		return reinterpret_cast<BufferID>(tie.lParam) == id;
	};

	// Lookups are usually repeated for the same tab, or target the last tab (just added / closing right to left).
	if (isAt(_lastFoundIndex))
		return _lastFoundIndex;

	const int lastIndex = static_cast<int>(_nbItem) - 1;
	if (lastIndex != _lastFoundIndex && isAt(lastIndex))
	{
		_lastFoundIndex = lastIndex;
		return lastIndex;
	}

	for (int i = 0; i < lastIndex; ++i)
	{
		if (i != _lastFoundIndex && isAt(i))
		{
			_lastFoundIndex = i;
			return i;
		}
	}
	return -1;
}


BufferID DocTabView::getBufferByIndex(size_t index)
{
	TCITEM tie{};
	tie.lParam = -1;
	tie.mask = TCIF_PARAM;
	::SendMessage(_hSelf, TCM_GETITEM, index, reinterpret_cast<LPARAM>(&tie));

	// Callers very often look the index of this buffer back up right away (e.g. doClose(getBufferByIndex(i), ...)).
	if (index < _nbItem)
		_lastFoundIndex = static_cast<int>(index);

	return reinterpret_cast<BufferID>(tie.lParam);
}


void DocTabView::bufferUpdated(const Buffer* buffer, int mask)
{
	int index = getIndexByBuffer(buffer->getID());
	if (index == -1)
		return;

	TCITEM tie{};
	tie.lParam = -1;
	tie.mask = 0;

	if (mask & BufferChangeReadonly || mask & BufferChangeDirty)
	{
		tie.mask |= TCIF_IMAGE;
		tie.iImage = buffer->isDirty()?UNSAVED_IMG_INDEX:SAVED_IMG_INDEX;
		if (buffer->isMonitoringOn())
		{
			tie.iImage = MONITORING_IMG_INDEX;
		}
		else if (buffer->getFileReadOnly())
		{
			tie.iImage = REDONLYSYS_IMG_INDEX;
		}
		else if (buffer->getUserReadOnly())
		{
			tie.iImage = REDONLY_IMG_INDEX;
		}
	}

	//We must make space for the added ampersand characters.
	wchar_t encodedLabel[2 * MAX_PATH] = { '\0' };

	if (mask & BufferChangeFilename)
	{
		tie.mask |= TCIF_TEXT;
		tie.pszText = encodedLabel;

		encodeTabLabel(buffer->getCompactFileName(), encodedLabel);
	}

	// Every TCM_SETITEM makes the tab control re-measure and re-layout all of its tabs, even when nothing changed,
	// and buffers send many change notifications while they are being loaded. Only send what actually differs.
	if (tie.mask != 0)
	{
		wchar_t currentLabel[2 * MAX_PATH] = { '\0' };
		TCITEM current{};
		current.mask = tie.mask;
		current.pszText = currentLabel;
		current.cchTextMax = static_cast<int>(std::size(currentLabel));
		if (::SendMessage(_hSelf, TCM_GETITEM, index, reinterpret_cast<LPARAM>(&current)))
		{
			if ((tie.mask & TCIF_IMAGE) && current.iImage == tie.iImage)
				tie.mask &= ~TCIF_IMAGE;
			if ((tie.mask & TCIF_TEXT) && wcscmp(currentLabel, encodedLabel) == 0)
				tie.mask &= ~TCIF_TEXT;
		}
	}

	if (tie.mask != 0)
		::SendMessage(_hSelf, TCM_SETITEM, index, reinterpret_cast<LPARAM>(&tie));

	// send WM_SIZE only when change tab
	// It is needed while a tab is closed (so tab changed) in multi-line tab mode
	if (mask & BufferChangeRecentTag)
		requestParentResize(_hParent);
}


void DocTabView::setBuffer(size_t index, BufferID id)
{
	if (index >= _nbItem)
		return;

	BufferID previousId = getBufferByIndex(index);

	TCITEM tie{};
	tie.lParam = reinterpret_cast<LPARAM>(id);
	tie.mask = TCIF_PARAM;
	::SendMessage(_hSelf, TCM_SETITEM, index, reinterpret_cast<LPARAM>(&tie));

	removeBufferFromIndex(previousId);
	addBufferToIndex(id);
	_lastFoundIndex = static_cast<int>(index);

	bufferUpdated(MainFileManager.getBufferByID(id), BufferChangeMask);	//update tab, everything has changed

	requestParentResize(_hParent);
}


void DocTabView::reSizeTo(RECT & rc)
{
	NppParameters& nppParam = NppParameters::getInstance();
	int borderWidth = nppParam.getSVP()._borderWidth;
	const NppGUI& nppGUI = nppParam.getNppGUI();
	if (nppGUI._tabStatus & TAB_HIDE)
	{
		RECT rcTmp = rc;
		TabBar::reSizeTo(rcTmp);
		_pView->reSizeTo(rc);
	}
	else
	{
		TabBar::reSizeTo(rc);
		rc.left	 += borderWidth;
		rc.right -= borderWidth * 2;
		rc.top   += borderWidth;
		rc.bottom -= (borderWidth * 2);
		_pView->reSizeTo(rc);
	}
	SendMessage(_hParent, NPPM_INTERNAL_UPDATECLICKABLELINKS, reinterpret_cast<WPARAM>(_pView), 0);
}
