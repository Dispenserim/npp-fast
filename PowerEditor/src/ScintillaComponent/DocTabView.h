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


#pragma once

#include <windows.h>

#include <unordered_map>
#include <vector>

#include "Buffer.h"
#include "ImageListSet.h"
#include "NppConstants.h"
#include "ScintillaEditView.h"
#include "TabBar.h"
#include "Window.h"


class DocTabView : public TabBarPlus
{
public:
	DocTabView() : TabBarPlus(), _pView(nullptr) {}
	~DocTabView() override {}

	void init(HINSTANCE hInst, HWND parent, ScintillaEditView * pView, unsigned char indexChoice, unsigned char buttonsStatus);

	void createIconSets();

	void changeIconSet(unsigned char choice) {
		if (choice >= _pIconListVector.size())
			return;
		_iconListIndexChoice = choice;
		TabBar::setImageList(_pIconListVector[_iconListIndexChoice]->getHandle());
	}

	void addBuffer(BufferID buffer);
	void closeBuffer(BufferID buffer);
	void bufferUpdated(const Buffer* buffer, int mask);

	bool activateBuffer(BufferID buffer);

	BufferID activeBuffer();
	BufferID findBufferByName(const wchar_t * fullfilename);	//-1 if not found, something else otherwise

	int getIndexByBuffer(BufferID id);
	BufferID getBufferByIndex(size_t index);

	void setBuffer(size_t index, BufferID id);

	// Hide TabBar::deletItemAt / deletAllItem so the buffer membership index stays in sync.
	void deletItemAt(size_t index);
	void deletAllItem();

	// Hides TabBar::getItemCount(): during a batch, closed tabs at the end can still be in the control (see below).
	int getItemCount() const { return static_cast<int>(_nbItem); }

	// Batch mode: while at least one batch is open, the WM_SIZE that every tab insertion/removal
	// would send to the main window is coalesced into a single one sent when the last batch ends.
	// Opening or closing N files then costs one full window layout instead of N.
	static void beginBatchUpdate() { ++_batchUpdateDepth; }
	static void endBatchUpdate();
	static void requestParentResize(HWND hParent);

	void reSizeTo(RECT & rc) override;

	void resizeIconsDpi() {
		UINT newSize = dpiManager().scale(g_TabIconSize);
		for (const IconList* const& i : _pIconListVector)
		{
			ImageList_SetIconSize(i->getHandle(), newSize, newSize);
		}

		createIconSets();

		if (_iconListIndexChoice < 0 || static_cast<size_t>(_iconListIndexChoice) >= _pIconListVector.size())
			_iconListIndexChoice = 0;

		TabBar::setImageList(_pIconListVector[_iconListIndexChoice]->getHandle());
	}

	const ScintillaEditView* getScintillaEditView() const {
		return _pView;
	}

	static void setIndividualTabColour(BufferID bufferId, int colorId);
	int getIndividualTabColourId(int tabIndex) override;
	
	HIMAGELIST getImgLst(UINT index) {
		if (index >= _pIconListVector.size())
			index = 0;
		return _pIconListVector[index]->getHandle();
	}

private :
	ScintillaEditView *_pView = nullptr;

	IconList _docTabIconList;
	IconList _docTabIconListAlt;
	IconList _docTabIconListDarkMode;

	std::vector<IconList *> _pIconListVector;
	int _iconListIndexChoice = -1;

	// Number of tabs holding each buffer (normally 0 or 1; can transiently be 2 while setBuffer() is used to reorder tabs).
	// Lets getIndexByBuffer() answer "not in this view" without walking every tab via TCM_GETITEM,
	// which made opening/closing many files O(n^2) window messages.
	std::unordered_map<BufferID, int> _bufferTabCount;
	// Index of the last tab found by getIndexByBuffer(); checked first since lookups are usually repeated or at the end.
	int _lastFoundIndex = -1;

	void addBufferToIndex(BufferID id) { if (id) ++_bufferTabCount[id]; }
	void removeBufferFromIndex(BufferID id);
	bool isBufferInIndex(BufferID id) const { return _bufferTabCount.find(id) != _bufferTabCount.end(); }

	static inline int _batchUpdateDepth = 0;
	static inline HWND _hPendingResizeParent = nullptr;

	// While a batch is open, tabs are laid out with TCS_FIXEDWIDTH: otherwise the tab control re-measures the text
	// of every tab on each insertion/removal, which makes adding or removing n tabs O(n^2) text measurements.
	// The real style is restored (and the tabs re-measured once) when the batch ends.
	// Painting is also turned off for the duration of the batch.
	bool _isInBatch = false;            // registered in _batchFixedWidthTabs, painting off
	bool _hasBatchAddedFixedWidth = false; // TCS_FIXEDWIDTH was set by the batch (not by the user's settings)
	static inline std::vector<DocTabView*> _batchFixedWidthTabs;
	void applyBatchFixedWidth();
	void restoreBatchFixedWidth();

	// While a batch is open, closing the last tab (what "Close All" and "Close All to the Right" do, right to left)
	// only drops it from _nbItem: each TCM_DELETEITEM re-lays out every remaining tab, so closing n tabs one by one
	// is O(n^2). The control keeps these closed tabs after the _nbItem live ones, hidden since painting is off,
	// and they are all removed at once by flushPendingDeletes() when the batch ends (or before a tab is added).
	size_t _nbPendingTrailingDeletes = 0;
	void flushPendingDeletes();

	using TabBarPlus::init;
};

// RAII helper: coalesces main-window relayouts caused by tab insertions/removals while it is alive.
class DocTabBatchUpdate
{
public:
	DocTabBatchUpdate() { DocTabView::beginBatchUpdate(); }
	~DocTabBatchUpdate() { DocTabView::endBatchUpdate(); }
	DocTabBatchUpdate(const DocTabBatchUpdate&) = delete;
	DocTabBatchUpdate& operator=(const DocTabBatchUpdate&) = delete;
};
