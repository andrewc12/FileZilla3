#ifndef FILELISTCTRL_INCLUDE_TEMPLATE_DEFINITION
// This works around a bug in GCC, appears to be [Bug pch/12707]
#include "filezilla.h"
#endif
#include "filelistctrl.h"
#include "filezillaapp.h"
#include "Options.h"
#include "conditionaldialog.h"
#include <algorithm>
#include "filelist_statusbar.h"
#include "themeprovider.h"

#ifndef __WXMSW__
#include <wx/mimetype.h>
#endif

#if defined(__WXGTK__) && !defined(__WXGTK3__)
#include <gtk/gtk.h>
#endif

#ifndef __WXMSW__
wxDECLARE_EVENT(fz_EVT_FILELIST_FOCUSCHANGE, wxCommandEvent);
wxDECLARE_EVENT(fz_EVT_DEFERRED_MOUSEEVENT, wxCommandEvent);
#ifndef FILELISTCTRL_INCLUDE_TEMPLATE_DEFINITION
wxDEFINE_EVENT(fz_EVT_FILELIST_FOCUSCHANGE, wxCommandEvent);
wxDEFINE_EVENT(fz_EVT_DEFERRED_MOUSEEVENT, wxCommandEvent);
#endif
#endif

BEGIN_EVENT_TABLE_TEMPLATE1(CFileListCtrl, wxListCtrlEx, CFileData)
EVT_LIST_COL_CLICK(wxID_ANY, CFileListCtrl<CFileData>::OnColumnClicked)
EVT_LIST_COL_RIGHT_CLICK(wxID_ANY, CFileListCtrl<CFileData>::OnColumnRightClicked)
EVT_LIST_ITEM_SELECTED(wxID_ANY, CFileListCtrl<CFileData>::OnItemSelected)
EVT_LIST_ITEM_DESELECTED(wxID_ANY, CFileListCtrl<CFileData>::OnItemDeselected)
#ifndef __WXMSW__
EVT_LIST_ITEM_FOCUSED(wxID_ANY, CFileListCtrl<CFileData>::OnFocusChanged)
EVT_COMMAND(wxID_ANY, fz_EVT_FILELIST_FOCUSCHANGE, CFileListCtrl<CFileData>::OnProcessFocusChange)
EVT_LEFT_DOWN(CFileListCtrl<CFileData>::OnLeftDown)
EVT_COMMAND(wxID_ANY, fz_EVT_DEFERRED_MOUSEEVENT, CFileListCtrl<CFileData>::OnProcessMouseEvent)
#endif
EVT_KEY_DOWN(CFileListCtrl<CFileData>::OnKeyDown)
EVT_SYS_COLOUR_CHANGED(CFileListCtrl<CFileData>::OnColorChange)
END_EVENT_TABLE()

#ifdef __WXMSW__
// wxWidgets does not handle LVN_ODSTATECHANGED, work around it

#pragma pack(push, 1)
typedef struct fz_tagNMLVODSTATECHANGE
{
	NMHDR hdr;
	int iFrom;
	int iTo;
	UINT uNewState;
	UINT uOldState;
} fzNMLVODSTATECHANGE;
#pragma pack(pop)

// MinGW lacks these constants and macros
#ifndef LVN_MARQUEEBEGIN
#define LVN_MARQUEEBEGIN (LVN_FIRST-56)
#endif
#ifndef APPCOMMAND_BROWSER_FORWARD
#define APPCOMMAND_BROWSER_FORWARD 2
#endif
#ifndef APPCOMMAND_BROWSER_BACKWARD
#define APPCOMMAND_BROWSER_BACKWARD 1
#endif
#ifndef GET_APPCOMMAND_LPARAM
#define GET_APPCOMMAND_LPARAM(x) (HIWORD(x)&~0xF000)
#endif

template<class CFileData> WXLRESULT CFileListCtrl<CFileData>::MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam)
{
	if (nMsg == WM_APPCOMMAND) {
		DWORD cmd = GET_APPCOMMAND_LPARAM(lParam);
		if (cmd == APPCOMMAND_BROWSER_FORWARD) {
			OnNavigationEvent(true);
			return true;
		}
		else if (cmd == APPCOMMAND_BROWSER_BACKWARD) {
			OnNavigationEvent(false);
			return true;
		}

		return wxListCtrlEx::MSWWindowProc(nMsg, wParam, lParam);
	}

	return wxListCtrlEx::MSWWindowProc(nMsg, wParam, lParam);
}

template<class CFileData> bool CFileListCtrl<CFileData>::MSWOnNotify(int idCtrl, WXLPARAM lParam, WXLPARAM *result)
{
	if (!m_pFilelistStatusBar) {
		return wxListCtrlEx::MSWOnNotify(idCtrl, lParam, result);
	}

	*result = 0;

	NMHDR* pNmhdr = (NMHDR*)lParam;
	if (pNmhdr->code == LVN_ODSTATECHANGED) {
		// A range of items got (de)selected

		if (m_insideSetSelection) {
			return true;
		}

		if (!m_pFilelistStatusBar) {
			return true;
		}

		if (wxGetKeyState(WXK_CONTROL) && wxGetKeyState(WXK_SHIFT)) {
			// The behavior of Ctrl+Shift+Click is highly erratic.
			// Even though it is very slow, we need to manually recount.
			m_pFilelistStatusBar->UnselectAll();
			int item = -1;
			while ((item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != -1) {
				if (m_hasParent && !item) {
					continue;
				}

				const int index = m_indexMapping[item];
				const CFileData& data = m_fileData[index];
				if (data.comparison_flags == fill) {
					continue;
				}

				if (ItemIsDir(index)) {
					m_pFilelistStatusBar->SelectDirectory();
				}
				else {
					m_pFilelistStatusBar->SelectFile(ItemGetSize(index));
				}
			}
		}
		else {
			fzNMLVODSTATECHANGE* pNmOdStateChange = (fzNMLVODSTATECHANGE*)lParam;

			wxASSERT(pNmOdStateChange->iFrom <= pNmOdStateChange->iTo);
			for (int i = pNmOdStateChange->iFrom; i <= pNmOdStateChange->iTo; ++i) {
				if (m_hasParent && !i) {
					continue;
				}

				const int index = m_indexMapping[i];
				const CFileData& data = m_fileData[index];
				if (data.comparison_flags == fill) {
					continue;
				}

				if (ItemIsDir(index)) {
					m_pFilelistStatusBar->SelectDirectory();
				}
				else {
					m_pFilelistStatusBar->SelectFile(ItemGetSize(index));
				}
			}
		}
		return true;
	}
	else if (pNmhdr->code == LVN_ITEMCHANGED) {
		if (m_insideSetSelection) {
			return true;
		}

		NMLISTVIEW* pNmListView = (NMLISTVIEW*)lParam;

		// Item of -1 means change applied to all items
		if (pNmListView->iItem == -1 && !(pNmListView->uNewState & LVIS_SELECTED)) {
			m_pFilelistStatusBar->UnselectAll();
		}
	}
	else if (pNmhdr->code == LVN_MARQUEEBEGIN) {
		SetFocus();
	}
	else if (pNmhdr->code == LVN_GETDISPINFO) {
		// Handle this manually instead of using wx for it
		// so that we can set the overlay image
		LV_DISPINFO *info = (LV_DISPINFO *)lParam;

		LV_ITEM& lvi = info->item;
		long item = lvi.iItem;

		int column = GetColumnActualIndex(lvi.iSubItem);

		if (lvi.mask & LVIF_TEXT) {
			wxString text = GetItemText(item, column);
			wxStrncpy(lvi.pszText, text, lvi.cchTextMax - 1);
			lvi.pszText[lvi.cchTextMax - 1] = 0;
		}

		if (lvi.mask & LVIF_IMAGE) {
			if (!lvi.iSubItem) {
				lvi.iImage = OnGetItemImage(item);
			}
			else {
				lvi.iImage = -1;
			}
		}

		if (!lvi.iSubItem) {
			lvi.state = INDEXTOOVERLAYMASK(GetOverlayIndex(lvi.iItem));
		}

		return true;
	}

	return wxListCtrlEx::MSWOnNotify(idCtrl, lParam, result);
}
#endif

#if defined(__WXGTK__) && !defined(__WXGTK3__)
// Need to call a member function of a C++ template class
// from a C function.
// Sadly template functions with C linkage aren't possible,
// so use some proxy object.
class CGtkEventCallbackProxyBase
{
public:
	virtual void OnNavigationEvent(bool forward) = 0;
	virtual ~CGtkEventCallbackProxyBase() {}
};

template <class CFileData> class CGtkEventCallbackProxy : public CGtkEventCallbackProxyBase
{
public:
	CGtkEventCallbackProxy(CFileListCtrl<CFileData> *pData) : m_pData(pData) {}

	virtual void OnNavigationEvent(bool forward)
	{
		m_pData->OnNavigationEvent(forward);
	}
protected:
	CFileListCtrl<CFileData> *m_pData;
};

extern "C" {
static gboolean gtk_button_release_event(GtkWidget*, void *gdk_event, CGtkEventCallbackProxyBase *proxy)
{
	GdkEventButton* button_event = (GdkEventButton*)gdk_event;

	// 8 is back, 9 is forward.
	if (button_event->button != 8 && button_event->button != 9) {
		return FALSE;
	}

	proxy->OnNavigationEvent(button_event->button == 9);

	return FALSE;
}
}
#endif

template<class CFileData> CFileListCtrl<CFileData>::CFileListCtrl(wxWindow* pParent, CQueueView* pQueue, COptionsBase & options, bool border)
	: wxListCtrlEx(pParent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | wxLC_VIRTUAL | wxLC_REPORT | wxLC_EDIT_LABELS | (border ? wxBORDER_SUNKEN : wxNO_BORDER))
	, CComparableListing(this)
	, m_pQueue(pQueue)
	, options_(options)
{
	CreateSystemImageList(CThemeProvider::GetIconSize(iconSizeSmall).x);

#ifndef __WXMSW__
	m_dropHighlightAttribute.SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNSHADOW));
#endif

#ifdef __WXMSW__
	// Enable use of overlay images
	DWORD mask = ListView_GetCallbackMask((HWND)GetHandle()) | LVIS_OVERLAYMASK;
	ListView_SetCallbackMask((HWND)GetHandle(), mask);
#endif

#if defined(__WXGTK__) && !defined(__WXGTK3__)
	m_gtkEventCallbackProxy.reset(new CGtkEventCallbackProxy<CFileData>(this));

	GtkWidget* widget = GetMainWindow()->GetConnectWidget();
	g_signal_connect(widget, "button_release_event", G_CALLBACK(gtk_button_release_event), m_gtkEventCallbackProxy.get());
#endif

	m_genericTypes[genericTypes::file] = _("File").ToStdWstring();
	m_genericTypes[genericTypes::directory] = _("Directory").ToStdWstring();

	SetBackgroundStyle(wxBG_STYLE_SYSTEM);
#ifndef __WXMSW__
	GetMainWindow()->SetBackgroundStyle(wxBG_STYLE_SYSTEM);
#endif

#ifdef __WXMSW__
	Bind(wxEVT_SYS_COLOUR_CHANGED, [this](wxSysColourChangedEvent& evt) {
		// Delay refresh until parent had time to process change.
		CallAfter([this](){
			InitColors();
		});
		evt.Skip();
	});
#endif
}

template<class CFileData> void CFileListCtrl<CFileData>::SortList(int column /*=-1*/, int direction /*=-1*/, bool updateSelections /*=true*/)
{
	CancelLabelEdit();

	if (column != -1) {
		if (column != m_sortColumn) {
			const int oldVisibleColumn = GetColumnVisibleIndex(m_sortColumn);
			if (oldVisibleColumn != -1) {
				SetHeaderSortIconIndex(oldVisibleColumn, -1);
			}
		}
	}
	else {
		if (m_sortColumn != -1) {
			column = m_sortColumn;
		}
		else {
			column = 0;
		}
	}

	if (direction == -1) {
		direction = m_sortDirection;
	}


	if (column != m_sortColumn || direction != m_sortDirection) {
		int newVisibleColumn = GetColumnVisibleIndex(column);
		if (newVisibleColumn == -1) {
			newVisibleColumn = 0;
			column = 0;
		}
		SetHeaderSortIconIndex(newVisibleColumn, direction);
	}

	// Remember which files are selected
	bool *selected = 0;
	int focused_item = -1;
	unsigned int focused_index{};

	if (updateSelections) {
#ifndef __WXMSW__
		// GetNextItem is O(n) if nothing is selected, GetSelectedItemCount() is O(1)
		if (GetSelectedItemCount())
#endif
		{
			int item = GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
			if (item != -1) {
				selected = new bool[m_fileData.size()];
				memset(selected, 0, sizeof(bool) * m_fileData.size());

				do {
					selected[m_indexMapping[item]] = 1;
					item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
				} while (item != -1);
			}
		}
		focused_item = GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_FOCUSED);
		if (focused_item >= 0 && static_cast<size_t>(focused_item) < m_indexMapping.size()) {
			focused_index = m_indexMapping[focused_item];
		}
	}

	const int dirSortOption = options_.get_int(OPTION_FILELIST_DIRSORT);

	if (column == m_sortColumn && direction != m_sortDirection && !m_indexMapping.empty() &&
		dirSortOption != 1)
	{
		// Simply reverse everything
		m_sortDirection = direction;
		m_sortColumn = column;
		std::vector<unsigned int>::iterator start = m_indexMapping.begin();
		if (m_hasParent) {
			++start;
		}
		std::reverse(start, m_indexMapping.end());

		if (updateSelections) {
			SortList_UpdateSelections(selected, focused_item, focused_index);
			delete [] selected;
		}

		return;
	}

	m_sortDirection = direction;
	m_sortColumn = column;

	const unsigned int minsize = m_hasParent ? 3 : 2;
	if (m_indexMapping.size() < minsize) {
		delete [] selected;
		return;
	}

	std::vector<unsigned int>::iterator start = m_indexMapping.begin();
	if (m_hasParent) {
		++start;
	}
	UpdateSortComparisonObject();
	auto & object = GetSortComparisonObject();
	std::sort(start, m_indexMapping.end(), SortPredicate(object));

	if (updateSelections) {
		SortList_UpdateSelections(selected, focused_item, focused_index);
		delete [] selected;
	}
}

template<class CFileData> void CFileListCtrl<CFileData>::SortList_UpdateSelections(bool* selections, int focused_item, unsigned int focused_index)
{
	if (focused_item >= 0) {
		if (m_indexMapping[focused_item] != focused_index) {
			SetItemState(focused_item, 0, wxLIST_STATE_FOCUSED);

			for (unsigned int i = m_hasParent ? 1 : 0; i < m_indexMapping.size(); ++i) {
				if (m_indexMapping[i] == focused_index) {
					SetItemState(i, wxLIST_STATE_FOCUSED, wxLIST_STATE_FOCUSED);
				}
			}
		}
	}

	if (selections) {
		for (unsigned int i = m_hasParent ? 1 : 0; i < m_indexMapping.size(); ++i) {
			const int state = GetItemState(i, wxLIST_STATE_SELECTED);
			const bool selected = (state & wxLIST_STATE_SELECTED) != 0;

			int item = m_indexMapping[i];
			if (selections[item] != selected) {
				SetSelection(i, selections[item]);
			}
		}
	}
}

template<class CFileData> CFileListCtrlSortBase::DirSortMode CFileListCtrl<CFileData>::GetDirSortMode()
{
	const int dirSortOption = options_.get_int(OPTION_FILELIST_DIRSORT);

	CFileListCtrlSortBase::DirSortMode dirSortMode;
	switch (dirSortOption)
	{
	case 0:
	default:
		dirSortMode = CFileListCtrlSortBase::dirsort_ontop;
		break;
	case 1:
		if (m_sortDirection) {
			dirSortMode = CFileListCtrlSortBase::dirsort_onbottom;
		}
		else {
			dirSortMode = CFileListCtrlSortBase::dirsort_ontop;
		}
		break;
	case 2:
		dirSortMode = CFileListCtrlSortBase::dirsort_inline;
		break;
	}

	return dirSortMode;
}

template<class CFileData> NameSortMode CFileListCtrl<CFileData>::GetNameSortMode()
{
	const int nameSortOption = options_.get_int(OPTION_FILELIST_NAMESORT);

	NameSortMode nameSortMode;
	switch (nameSortOption)
	{
	case 0:
	default:
		nameSortMode = NameSortMode::case_insensitive;
		break;
	case 1:
		nameSortMode = NameSortMode::case_sensitive;
		break;
	case 2:
		nameSortMode = NameSortMode::natural;
		break;
	}

	return nameSortMode;
}

template<class CFileData> void CFileListCtrl<CFileData>::OnColumnClicked(wxListEvent &event)
{
	int col = GetColumnActualIndex(event.GetColumn());
	if (col == -1) {
		return;
	}

	if (IsComparing()) {
#ifdef __WXMSW__
		ReleaseCapture();
		Refresh();
#endif
		CConditionalDialog dlg(this, CConditionalDialog::compare_changesorting, CConditionalDialog::yesno, options_);
		dlg.SetTitle(_("Directory comparison"));
		dlg.AddText(_("Sort order cannot be changed if comparing directories."));
		dlg.AddText(_("End comparison and change sorting order?"));
		if (!dlg.Run()) {
			return;
		}
		ExitComparisonMode();
	}

	int dir;
	if (col == m_sortColumn) {
		dir = m_sortDirection ? 0 : 1;
	}
	else {
		dir = m_sortDirection;
	}

	SortList(col, dir);
	RefreshListOnly(false);
}

#ifdef __WXMSW__
namespace {
std::wstring GetExt(std::wstring const& file)
{
	std::wstring ret;

	size_t pos = file.rfind('.');
	if (pos != std::wstring::npos && pos > 0 && pos + 1 < file.size()) { // Does neither starts nor end with dot
		ret = file.substr(pos + 1);
	}

	return ret;
}
}
#endif

template<class CFileData> std::wstring CFileListCtrl<CFileData>::GetType(std::wstring const& name, bool dir, std::wstring const& path)
{
	static std::wstring const generic_suffix = L"-" + _("file").ToStdWstring();
#ifdef __WXMSW__
	std::wstring ext;
	if (dir) {
		if (!path.empty()) {
			ext = '/';
		}
	}
	else {
		ext = GetExt(name);
		fz::str_tolower_inplace(ext);
	}
	auto typeIter = m_fileTypeMap.find(ext);
	if (typeIter != m_fileTypeMap.end()) {
		return typeIter->second;
	}

	std::wstring type;

	std::wstring fullname;
	int flags = SHGFI_TYPENAME;
	if (path.empty()) {
		flags |= SHGFI_USEFILEATTRIBUTES;
	}
	else if (path == _T("\\")) {
		fullname.reserve(name.size() + 1);
		fullname = name;
		fullname += '\\';
	}
	else {
		fullname.reserve(name.size() + path.size());
		fullname = path;
		fullname += name;
	}

	SHFILEINFO shFinfo;
	memset(&shFinfo, 0, sizeof(SHFILEINFO));
	if (SHGetFileInfo(path.empty() ? name.c_str() : fullname.c_str(),
		dir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL,
		&shFinfo,
		sizeof(shFinfo),
		flags))
	{
		if (!*shFinfo.szTypeName) {
			if (!ext.empty()) {
				type.reserve(ext.size() + generic_suffix.size());
				type = ext;
				fz::str_toupper_inplace(ext);
				type += generic_suffix;
			}
			else {
				type = m_genericTypes[genericTypes::file];
			}
		}
		else {
			type = shFinfo.szTypeName;
			if (!ext.empty()) {
				m_fileTypeMap[ext] = type;
			}
		}
	}
	else {
		if (!ext.empty()) {
			type.reserve(ext.size() + generic_suffix.size());
			type = ext;
			fz::str_toupper_inplace(ext);
			type += generic_suffix;
		}
		else {
			type = m_genericTypes[genericTypes::file];
		}
	}
	return type;
#else
	(void)path;

	if (dir) {
		return m_genericTypes[genericTypes::directory];
	}

	size_t pos = name.rfind('.');
	if (pos == std::wstring::npos || pos < 1 || !name[pos + 1]) { // No dot or starts or ends with dot
		return m_genericTypes[genericTypes::file];
	}
	std::wstring ext = name.substr(pos + 1);
	std::wstring lower_ext = fz::str_tolower(ext);

	auto typeIter = m_fileTypeMap.find(lower_ext);
	if (typeIter != m_fileTypeMap.end()) {
		return typeIter->second;
	}

	std::wstring type;

	wxFileType *pType = wxTheMimeTypesManager->GetFileTypeFromExtension(ext);
	if (!pType) {
		type.reserve(ext.size() + generic_suffix.size());
		type = ext;
		type += generic_suffix;
		m_fileTypeMap[ext] = type;
		return type;
	}

	wxString desc;
	if (pType->GetDescription(&desc) && !desc.empty()) {
		delete pType;
		type = desc.ToStdWstring();
		m_fileTypeMap[lower_ext] = type;
		return type;
	}
	delete pType;
	type.reserve(ext.size() + generic_suffix.size());
	type = ext;
	type += generic_suffix;
	m_fileTypeMap[lower_ext] = type;
	return type;
#endif
}

template<class CFileData> void CFileListCtrl<CFileData>::ScrollTopItem(int item)
{
	wxListCtrlEx::ScrollTopItem(item);
}

template<class CFileData> void CFileListCtrl<CFileData>::OnPostScroll()
{
	if (!IsComparing()) {
		return;
	}

	CComparableListing* pOther = GetOther();
	if (!pOther) {
		return;
	}

	pOther->ScrollTopItem(GetTopItem());
}

template<class CFileData> void CFileListCtrl<CFileData>::OnExitComparisonMode()
{
	if (m_originalIndexMapping.empty()) {
		return;
	}

	ComparisonRememberSelections();

	m_indexMapping.clear();
	m_indexMapping.swap(m_originalIndexMapping);

	for (unsigned int i = 0; i < m_fileData.size() - 1; ++i) {
		m_fileData[i].comparison_flags = normal;
	}

	SetItemCount(m_indexMapping.size());

	ComparisonRestoreSelections();

	RefreshListOnly();
}

template<class CFileData> void CFileListCtrl<CFileData>::CompareAddFile(t_fileEntryFlags flags)
{
	if (flags == fill) {
		m_indexMapping.push_back(m_fileData.size() - 1);
		return;
	}

	int index = m_originalIndexMapping[m_comparisonIndex];
	m_fileData[index].comparison_flags = flags;

	m_indexMapping.push_back(index);
}

template<class CFileData> void CFileListCtrl<CFileData>::ComparisonRememberSelections()
{
	m_comparisonSelections.clear();

	if (GetItemCount() != (int)m_indexMapping.size()) {
		return;
	}

	int focus = GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_FOCUSED);
	if (focus != -1) {
		SetItemState(focus, 0, wxLIST_STATE_FOCUSED);
		int index = m_indexMapping[focus];
		if (m_fileData[index].comparison_flags == fill) {
			focus = -1;
		}
		else {
			focus = index;
		}
	}
	m_comparisonSelections.push_back(focus);

#ifndef __WXMSW__
	// GetNextItem is O(n) if nothing is selected, GetSelectedItemCount() is O(1)
	if (GetSelectedItemCount())
#endif
	{
		int item = -1;
		while ((item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != -1) {
			int index = m_indexMapping[item];
			if (m_fileData[index].comparison_flags == fill) {
				continue;
			}
			m_comparisonSelections.push_back(index);
		}
	}
}

template<class CFileData> void CFileListCtrl<CFileData>::ComparisonRestoreSelections()
{
	if (m_comparisonSelections.empty()) {
		return;
	}

	int focus = m_comparisonSelections.front();
	m_comparisonSelections.pop_front();

	int item = -1;
	if (!m_comparisonSelections.empty()) {
		item = m_comparisonSelections.front();
		m_comparisonSelections.pop_front();
	}
	if (focus == -1) {
		focus = item;
	}

	for (unsigned int i = 0; i < m_indexMapping.size(); ++i) {
		int index = m_indexMapping[i];
		if (focus == index) {
			SetItemState(i, wxLIST_STATE_FOCUSED, wxLIST_STATE_FOCUSED);
			focus = -1;
		}

		bool isSelected = GetItemState(i, wxLIST_STATE_SELECTED) == wxLIST_STATE_SELECTED;
		bool shouldSelected = item == index;
		if (isSelected != shouldSelected) {
			SetSelection(i, shouldSelected);
		}

		if (shouldSelected) {
			if (m_comparisonSelections.empty()) {
				item = -1;
			}
			else {
				item = m_comparisonSelections.front();
				m_comparisonSelections.pop_front();
			}
		}
	}
}

template<class CFileData> void CFileListCtrl<CFileData>::OnColumnRightClicked(wxListEvent&)
{
	ShowColumnEditor();
}

template<class CFileData> void CFileListCtrl<CFileData>::InitSort(interfaceOptions optionID)
{
	std::wstring sortInfo = options_.get_string(optionID);
	if (!sortInfo.empty()) {
		m_sortDirection = sortInfo[0] - '0';
	}
	else {
		m_sortDirection = 0;
	}
	if (m_sortDirection < 0 || m_sortDirection > 1) {
		m_sortDirection = 0;
	}

	if (sortInfo.size() == 3) {
		m_sortColumn = sortInfo[2] - '0';
		if (GetColumnVisibleIndex(m_sortColumn) == -1) {
			m_sortColumn = 0;
		}
	}
	else {
		m_sortColumn = 0;
	}

	SetHeaderSortIconIndex(GetColumnVisibleIndex(m_sortColumn), m_sortDirection);
}

template<class CFileData> void CFileListCtrl<CFileData>::OnItemSelected(wxListEvent& event)
{
#ifndef __WXMSW__
	// On MSW this is done in the subclassed window proc
	if (m_insideSetSelection) {
		return;
	}
	if (m_pending_focus_processing) {
		return;
	}
#endif

	const int item = event.GetIndex();
	if (item < 0 || item >= (int)m_indexMapping.size()) {
		return;
	}

#ifndef __WXMSW__
	if (m_selections[item]) {
		// Need to re-do all selections
		// Unfortunately the focus change event comes before the selection change event
		// in the generic list ctrl if multiple items are already selected
		// and selecting one of those a second time, causing all others to be deselected
		UpdateSelections(0, GetItemCount() - 1);
		return;
	}
	m_selections[item] = true;
#endif

	if (!m_pFilelistStatusBar) {
		return;
	}


	if (m_hasParent && !item) {
		return;
	}

	const int index = m_indexMapping[item];
	const CFileData& data = m_fileData[index];
	if (data.comparison_flags == fill) {
		return;
	}

	if (ItemIsDir(index)) {
		m_pFilelistStatusBar->SelectDirectory();
	}
	else {
		m_pFilelistStatusBar->SelectFile(ItemGetSize(index));
	}
}

template<class CFileData> void CFileListCtrl<CFileData>::OnItemDeselected(wxListEvent& event)
{
#ifndef __WXMSW__
	// On MSW this is done in the subclassed window proc
	if (m_insideSetSelection) {
		return;
	}
#endif

	const int item = event.GetIndex();

#if !defined(__WXMSW__)
	if (item == -1) {
		if (wxGetKeyState(WXK_SHIFT)) {
			// Sad, so expensive.
			if (m_pending_focus_processing) {
				updateAllSelections_ = true;
			}
			else {
				UpdateSelections(0, static_cast<int>(m_selections.size()) - 1);
			}
		}
		else {
			// Everything deselected...
			for (unsigned int i = 0; i < m_selections.size(); ++i) {
				m_selections[i] = false;
			}
			if (m_pFilelistStatusBar) {
				m_pFilelistStatusBar->UnselectAll();
			}

			// ...but possibly the focus
			if (m_hasParent && !m_focusItem) {
				return;
			}
			if (m_focusItem >= 0 && m_focusItem < GetItemCount()) {
				bool selected = GetItemState(m_focusItem, wxLIST_STATE_SELECTED) == wxLIST_STATE_SELECTED;
				if (selected) {
					m_selections[m_focusItem] = true;
					if (static_cast<size_t>(m_focusItem) >= m_indexMapping.size()) {
						return;
					}
					const int index = m_indexMapping[m_focusItem];
					const CFileData& data = m_fileData[index];
					if (data.comparison_flags == fill) {
						return;
					}

					if (m_pFilelistStatusBar) {
						if (ItemIsDir(index)) {
							m_pFilelistStatusBar->SelectDirectory();
						}
						else {
							m_pFilelistStatusBar->SelectFile(ItemGetSize(index));
						}
					}
				}
			}
		}
		return;
	}
#endif

	if (item < 0 || item >= (int)m_indexMapping.size()) {
		return;
	}

#ifndef __WXMSW__
	if (!m_selections[item]) {
		return;
	}
	m_selections[item] = false;
#endif

	if (!m_pFilelistStatusBar) {
		return;
	}

	if (m_hasParent && !item) {
		return;
	}

	const int index = m_indexMapping[item];
	const CFileData& data = m_fileData[index];
	if (data.comparison_flags == fill) {
		return;
	}

	if (ItemIsDir(index)) {
		m_pFilelistStatusBar->UnselectDirectory();
	}
	else {
		m_pFilelistStatusBar->UnselectFile(ItemGetSize(index));
	}
}

template<class CFileData> void CFileListCtrl<CFileData>::SetSelection(int item, bool select)
{
	m_insideSetSelection = true;
	SetItemState(item, select ? wxLIST_STATE_SELECTED : 0, wxLIST_STATE_SELECTED);
	m_insideSetSelection = false;
#ifndef __WXMSW__
	m_selections[item] = select;
#endif
}

#ifndef __WXMSW__
template<class CFileData> void CFileListCtrl<CFileData>::OnFocusChanged(wxListEvent& event)
{
	const int focusItem = event.GetIndex();

	// Need to defer processing, as focus it set before selection by wxWidgets internally
	wxCommandEvent *evt = new wxCommandEvent();
	evt->SetEventType(fz_EVT_FILELIST_FOCUSCHANGE);
	evt->SetInt(m_focusItem);
	evt->SetExtraLong((long)focusItem);
	m_pending_focus_processing++;
	QueueEvent(evt);

	m_focusItem = focusItem;
}

template<class CFileData> void CFileListCtrl<CFileData>::SetItemCount(int count)
{
	m_selections.resize(count, false);
	if (m_focusItem >= count) {
		m_focusItem = -1;
	}
	wxListCtrlEx::SetItemCount(count);
}

template<class CFileData> void CFileListCtrl<CFileData>::OnProcessFocusChange(wxCommandEvent& event)
{
	m_pending_focus_processing--;
	if (updateAllSelections_) {
		updateAllSelections_ = false;
		UpdateSelections(0, static_cast<int>(m_selections.size()) - 1);
		return;
	}

	int old_focus = event.GetInt();
	int new_focus = (int)event.GetExtraLong();

	if (old_focus >= GetItemCount()) {
		return;
	}

	if (old_focus >= 0) {
		bool selected = GetItemState(old_focus, wxLIST_STATE_SELECTED) == wxLIST_STATE_SELECTED;
		if (!selected && static_cast<size_t>(old_focus) < m_selections.size() && m_selections[old_focus]) {
			// Need to deselect all
			if (m_pFilelistStatusBar) {
				m_pFilelistStatusBar->UnselectAll();
			}
			for (unsigned int i = 0; i < m_selections.size(); ++i) {
				m_selections[i] = 0;
			}
		}
	}

	int min;
	int max;
	if (new_focus > old_focus) {
		min = old_focus;
		max = new_focus;
	}
	else {
		min = new_focus;
		max = old_focus;
	}
	if (min == -1) {
		min++;
	}
	if (max == -1) {
		return;
	}

	if (max >= GetItemCount()) {
		return;
	}

	UpdateSelections(min, max);
}

template <class CFileData> void CFileListCtrl<CFileData>::UpdateSelections(int min, int max)
{
	if (min < 0 || min > max) {
		return;
	}

	size_t smax = static_cast<size_t>(max);
	if (m_selections.size() <= smax || m_indexMapping.size() <= smax || m_fileData.size() <= smax) {
		return;
	}

	for (int i = min; i <= max; ++i) {
		bool selected = GetItemState(i, wxLIST_STATE_SELECTED) == wxLIST_STATE_SELECTED;
		if (selected == m_selections[i]) {
			continue;
		}

		m_selections[i] = selected;

		if (!m_pFilelistStatusBar) {
			continue;
		}

		if (m_hasParent && !i) {
			continue;
		}

		const int index = m_indexMapping[i];
		const CFileData& data = m_fileData[index];
		if (data.comparison_flags == fill) {
			continue;
		}

		if (selected) {
			if (ItemIsDir(index)) {
				m_pFilelistStatusBar->SelectDirectory();
			}
			else {
				m_pFilelistStatusBar->SelectFile(ItemGetSize(index));
			}
		}
		else {
			if (ItemIsDir(index)) {
				m_pFilelistStatusBar->UnselectDirectory();
			}
			else {
				m_pFilelistStatusBar->UnselectFile(ItemGetSize(index));
			}
		}
	}
}

template<class CFileData> void CFileListCtrl<CFileData>::OnLeftDown(wxMouseEvent& event)
{
	// Left clicks in the whitespace around the items deselect everything
	// but does not change focus. Defer event.
	event.Skip();
	wxCommandEvent *evt = new wxCommandEvent();
	evt->SetEventType(fz_EVT_DEFERRED_MOUSEEVENT);
	QueueEvent(evt);
}

template<class CFileData> void CFileListCtrl<CFileData>::OnProcessMouseEvent(wxCommandEvent&)
{
	if (m_pending_focus_processing) {
		return;
	}

	if (m_focusItem >= GetItemCount()) {
		return;
	}
	if (m_focusItem == -1) {
		return;
	}

	bool selected = GetItemState(m_focusItem, wxLIST_STATE_SELECTED) == wxLIST_STATE_SELECTED;
	if (!selected) {
		if (wxGetKeyState(WXK_CONTROL)) {
			UpdateSelections(0, static_cast<int>(m_selections.size()) - 1);
		}
		else {
			// Need to deselect all
			if (m_pFilelistStatusBar) {
				m_pFilelistStatusBar->UnselectAll();
			}
			for (unsigned int i = 0; i < m_selections.size(); ++i) {
				m_selections[i] = 0;
			}
		}
	}
}
#endif

template<class CFileData> void CFileListCtrl<CFileData>::ClearSelection()
{
	// Clear selection
#ifndef __WXMSW__
	// GetNextItem is O(n) if nothing is selected, GetSelectedItemCount() is O(1)
	if (GetSelectedItemCount())
#endif
	{
		int item = -1;
		while ((item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != -1) {
			SetSelection(item, false);
		}
	}

	int item = GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_FOCUSED);
	if (item != -1) {
		SetItemState(item, 0, wxLIST_STATE_FOCUSED);
	}
}

template<class CFileData> void CFileListCtrl<CFileData>::OnKeyDown(wxKeyEvent& event)
{
#ifdef __WXMAC__
#define CursorModifierKey wxMOD_CMD
#else
#define CursorModifierKey wxMOD_ALT
#endif

	const int code = event.GetKeyCode();
	const int mods = event.GetModifiers();
	if (code == 'A' && (mods & wxMOD_CMD || (mods & (wxMOD_CONTROL | wxMOD_META)) == (wxMOD_CONTROL | wxMOD_META))) {
		int mask = fill;
		if (mods & wxMOD_SHIFT) {
			mask |= normal;
		}

		for (unsigned int i = m_hasParent ? 1 : 0; i < m_indexMapping.size(); ++i) {
			const CFileData& data = m_fileData[m_indexMapping[i]];
			if (data.comparison_flags & mask) {
				SetSelection(i, false);
			}
			else {
				SetSelection(i, true);
			}
		}
		if (m_hasParent && GetItemCount()) {
			SetSelection(0, false);
		}
		if (m_pFilelistStatusBar) {
			m_pFilelistStatusBar->SelectAll();
		}
	}
	else if (code == WXK_BACK ||
			(code == WXK_UP && event.GetModifiers() == CursorModifierKey) ||
			(code == WXK_LEFT && event.GetModifiers() == CursorModifierKey)
		)
	{
		OnNavigationEvent(false);
	}
	else {
		event.Skip();
	}
}

template<class CFileData> void CFileListCtrl<CFileData>::UpdateSelections_ItemsAdded(std::vector<int> const& added_indexes)
{
	if (added_indexes.empty()) {
		return;
	}

	auto added_index = added_indexes.cbegin();
	std::deque<bool> selected;
	// This is O(n) in number of items. I think it's possible to make it O(n) in number of selections
	for (unsigned int i = *added_index; i < m_indexMapping.size(); ++i) {
		if (added_index != added_indexes.end() && i == static_cast<unsigned int>(*added_index)) {
			selected.push_front(false);
			++added_index;
		}
		bool is_selected = GetItemState(i, wxLIST_STATE_SELECTED) != 0;
		selected.push_back(is_selected);

		bool should_selected = selected.front();
		selected.pop_front();
		if (is_selected != should_selected) {
			SetSelection(i, should_selected);
		}
	}
}

template<class CFileData> CFileListCtrlSortBase& CFileListCtrl<CFileData>::GetSortComparisonObject()
{
	if (!sortComparisonObject_) {
		UpdateSortComparisonObject();
	}

	return *sortComparisonObject_;
}

template<class CFileData> void CFileListCtrl<CFileData>::OnColorChange(wxSysColourChangedEvent & ev)
{
	InitColors();
	Refresh();
	ev.Skip();
}

