#include "WinPlatform.h"
#include "WinText.h"
#include "../../core/Text.h"
#include <uiautomation.h>
#include <wrl/client.h>
#include <math.h>

using Microsoft::WRL::ComPtr;

// Controls inside windows, through UI Automation.  The elements live in a
// table here and scripts hold their numbers, so Python never touches COM.

#define MAX_CHILDREN			10000
#define CONNECTION_TIMEOUT_MS	2000
#define TRANSACTION_TIMEOUT_MS	5000
#define CONTEXT_CHARS			4

static mutex _lock;
static map<uint64_t, ComPtr<IUIAutomationElement>> _elements;
static uint64_t _next = 0;

static const pair<CONTROLTYPEID, const char*> _roles[] = {
	{ UIA_AppBarControlTypeId, "app_bar" }, { UIA_ButtonControlTypeId, "button" }, { UIA_CalendarControlTypeId, "calendar" },
	{ UIA_CheckBoxControlTypeId, "checkbox" }, { UIA_ComboBoxControlTypeId, "combobox" }, { UIA_CustomControlTypeId, "custom" },
	{ UIA_DataGridControlTypeId, "datagrid" }, { UIA_DataItemControlTypeId, "dataitem" }, { UIA_DocumentControlTypeId, "document" },
	{ UIA_EditControlTypeId, "edit" }, { UIA_GroupControlTypeId, "group" }, { UIA_HeaderControlTypeId, "header" },
	{ UIA_HeaderItemControlTypeId, "headeritem" }, { UIA_HyperlinkControlTypeId, "hyperlink" }, { UIA_ImageControlTypeId, "image" },
	{ UIA_ListControlTypeId, "list" }, { UIA_ListItemControlTypeId, "listitem" }, { UIA_MenuControlTypeId, "menu" },
	{ UIA_MenuBarControlTypeId, "menubar" }, { UIA_MenuItemControlTypeId, "menuitem" }, { UIA_PaneControlTypeId, "pane" },
	{ UIA_ProgressBarControlTypeId, "progressbar" }, { UIA_RadioButtonControlTypeId, "radiobutton" },
	{ UIA_ScrollBarControlTypeId, "scrollbar" }, { UIA_SemanticZoomControlTypeId, "semanticzoom" },
	{ UIA_SeparatorControlTypeId, "separator" }, { UIA_SliderControlTypeId, "slider" }, { UIA_SpinnerControlTypeId, "spinner" },
	{ UIA_SplitButtonControlTypeId, "splitbutton" }, { UIA_StatusBarControlTypeId, "statusbar" }, { UIA_TabControlTypeId, "tab" },
	{ UIA_TabItemControlTypeId, "tabitem" }, { UIA_TableControlTypeId, "table" }, { UIA_TextControlTypeId, "text" },
	{ UIA_ThumbControlTypeId, "thumb" }, { UIA_TitleBarControlTypeId, "titlebar" }, { UIA_ToolBarControlTypeId, "toolbar" },
	{ UIA_ToolTipControlTypeId, "tooltip" }, { UIA_TreeControlTypeId, "tree" }, { UIA_TreeItemControlTypeId, "treeitem" },
	{ UIA_WindowControlTypeId, "window" },
};

static string RoleName(CONTROLTYPEID type) {
	for (auto& cur : _roles) {
		if (cur.first == type) {
			return cur.second;
		}
	}
	return "custom";
}

static CONTROLTYPEID RoleType(const string& name) {
	for (auto& cur : _roles) {
		if (name == cur.second) {
			return cur.first;
		}
	}
	return 0;
}

static IUIAutomation* Uia() {
	// UI Automation for the calling thread, which is set up for COM for the
	// rest of its life the first time it's needed
	thread_local ComPtr<IUIAutomation> uia;
	thread_local bool tried = false;
	if (!tried) {
		tried = true;
		CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		if (FAILED(CoCreateInstance(__uuidof(CUIAutomation8), NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia)))) {
			CoCreateInstance(__uuidof(CUIAutomation), NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia));
		}
		// A hung program shouldn't hang the macro for long
		ComPtr<IUIAutomation2> uia2;
		if (uia && SUCCEEDED(uia.As(&uia2))) {
			uia2->put_ConnectionTimeout(CONNECTION_TIMEOUT_MS);
			uia2->put_TransactionTimeout(TRANSACTION_TIMEOUT_MS);
		}
	}
	return uia.Get();
}

static uint64_t Keep(IUIAutomationElement* element) {
	if (element == NULL) {
		return 0;
	}
	lock_guard<mutex> hold(_lock);
	uint64_t id = ++_next;
	_elements[id] = element;
	return id;
}

static ComPtr<IUIAutomationElement> Lookup(uint64_t id) {
	lock_guard<mutex> hold(_lock);
	auto it = _elements.find(id);
	return it == _elements.end() ? ComPtr<IUIAutomationElement>() : it->second;
}

void WinPlatform::ReleaseControl(uint64_t control) {
	ComPtr<IUIAutomationElement> doomed;
	{
		lock_guard<mutex> hold(_lock);
		auto it = _elements.find(control);
		if (it == _elements.end()) {
			return;
		}
		doomed = it->second;
		_elements.erase(it);
	}
}

static string FromBstr(BSTR text) {
	string ret = text == NULL ? string() : Utf16ToUtf8(text, SysStringLen(text));
	SysFreeString(text);
	return ret;
}

static bool IsGlob(const string& text) {
	return text.find_first_of("*?") != string::npos;
}

static void AddCondition(IUIAutomation* uia, vector<ComPtr<IUIAutomationCondition>>& conditions, PROPERTYID property,
	const string& text, bool ignoreCase) {
	// An exact match UI Automation can test itself; patterns are checked here
	if (text.empty() || IsGlob(text)) {
		return;
	}
	VARIANT value;
	VariantInit(&value);
	value.vt = VT_BSTR;
	value.bstrVal = SysAllocString(Utf8ToUtf16(text).c_str());
	ComPtr<IUIAutomationCondition> cond;
	if (SUCCEEDED(uia->CreatePropertyConditionEx(property, value, ignoreCase ? PropertyConditionFlags_IgnoreCase : PropertyConditionFlags_None, &cond))) {
		conditions.push_back(cond);
	}
	VariantClear(&value);
}

static bool Wanted(IUIAutomationElement* element, const ControlQuery& query) {
	// The pattern fields, read from the cache FindAll filled
	BSTR text = NULL;
	if (IsGlob(query.Name) && (FAILED(element->get_CachedName(&text)) || !GlobMatch(query.Name, FromBstr(text)))) {
		return false;
	}
	text = NULL;
	if (IsGlob(query.ClassName) && (FAILED(element->get_CachedClassName(&text)) || !GlobMatch(query.ClassName, FromBstr(text)))) {
		return false;
	}
	text = NULL;
	if (IsGlob(query.AutomationId) && (FAILED(element->get_CachedAutomationId(&text)) || !GlobMatch(query.AutomationId, FromBstr(text)))) {
		return false;
	}
	return true;
}

static bool StartElement(IUIAutomation* uia, uint64_t root, uint64_t window, ComPtr<IUIAutomationElement>& start, string& error) {
	// Where a search starts: a control, a window, or the window in front
	if (uia == NULL) {
		error = "UI Automation isn't available";
		return false;
	}
	if (root != 0) {
		start = Lookup(root);
		if (!start) {
			error = "That control is gone";
			return false;
		}
		return true;
	}
	HWND hwnd = window != 0 ? (HWND)(uintptr_t)window : GetForegroundWindow();
	if (hwnd == NULL || FAILED(uia->ElementFromHandle(hwnd, &start)) || !start) {
		error = window != 0 ? "That window is gone" : "No window is in front";
		return false;
	}
	return true;
}

bool WinPlatform::FindControls(uint64_t root, uint64_t window, const ControlQuery& query, bool all, vector<uint64_t>& found, string& error) {
	IUIAutomation* uia = Uia();
	ComPtr<IUIAutomationElement> start;
	if (!StartElement(uia, root, window, start, error)) {
		return false;
	}

	vector<ComPtr<IUIAutomationCondition>> conditions;
	ComPtr<IUIAutomationCondition> controlView;
	if (SUCCEEDED(uia->get_ControlViewCondition(&controlView))) {
		conditions.push_back(controlView);
	}
	if (!query.Role.empty()) {
		CONTROLTYPEID type = RoleType(query.Role);
		if (type == 0) {
			error = "Unknown role " + query.Role;
			return false;
		}
		VARIANT value;
		VariantInit(&value);
		value.vt = VT_I4;
		value.lVal = type;
		ComPtr<IUIAutomationCondition> cond;
		if (SUCCEEDED(uia->CreatePropertyCondition(UIA_ControlTypePropertyId, value, &cond))) {
			conditions.push_back(cond);
		}
	}
	AddCondition(uia, conditions, UIA_NamePropertyId, query.Name, true);
	AddCondition(uia, conditions, UIA_ClassNamePropertyId, query.ClassName, true);
	AddCondition(uia, conditions, UIA_AutomationIdPropertyId, query.AutomationId, false);

	vector<IUIAutomationCondition*> raw;
	for (auto& cur : conditions) {
		raw.push_back(cur.Get());
	}
	ComPtr<IUIAutomationCondition> condition;
	if (raw.empty() ? FAILED(uia->CreateTrueCondition(&condition)) :
		FAILED(uia->CreateAndConditionFromNativeArray(raw.data(), (int)raw.size(), &condition))) {
		error = "Unable to search for controls";
		return false;
	}

	ComPtr<IUIAutomationCacheRequest> cache;
	if (FAILED(uia->CreateCacheRequest(&cache))) {
		error = "Unable to search for controls";
		return false;
	}
	cache->AddProperty(UIA_NamePropertyId);
	cache->AddProperty(UIA_ClassNamePropertyId);
	cache->AddProperty(UIA_AutomationIdPropertyId);

	bool filter = IsGlob(query.Name) || IsGlob(query.ClassName) || IsGlob(query.AutomationId);
	if (!all && !filter) {
		ComPtr<IUIAutomationElement> element;
		HRESULT hr = start->FindFirstBuildCache(TreeScope_Descendants, condition.Get(), cache.Get(), &element);
		if (FAILED(hr)) {
			error = "The window didn't answer: " + LastErrorText(hr);
			return false;
		}
		if (element) {
			found.push_back(Keep(element.Get()));
		}
		return true;
	}
	ComPtr<IUIAutomationElementArray> elements;
	HRESULT hr = start->FindAllBuildCache(TreeScope_Descendants, condition.Get(), cache.Get(), &elements);
	if (FAILED(hr)) {
		error = "The window didn't answer: " + LastErrorText(hr);
		return false;
	}
	int count = 0;
	if (elements) {
		elements->get_Length(&count);
	}
	for (int i = 0; i < count; i++) {
		ComPtr<IUIAutomationElement> element;
		if (SUCCEEDED(elements->GetElement(i, &element)) && element && Wanted(element.Get(), query)) {
			found.push_back(Keep(element.Get()));
			if (!all) {
				break;
			}
		}
	}
	return true;
}

static HWND ElementWindow(IUIAutomationElement* element) {
	// The top-level window a control is in: the first one up the tree that
	// has a window of its own, and that window's root
	IUIAutomation* uia = Uia();
	ComPtr<IUIAutomationTreeWalker> walker;
	if (uia == NULL || FAILED(uia->get_RawViewWalker(&walker))) {
		return NULL;
	}
	ComPtr<IUIAutomationElement> cur = element;
	for (int depth = 0; cur && depth < 100; depth++) {
		UIA_HWND hwnd = NULL;
		if (SUCCEEDED(cur->get_CurrentNativeWindowHandle(&hwnd)) && hwnd != NULL) {
			return GetAncestor((HWND)hwnd, GA_ROOT);
		}
		ComPtr<IUIAutomationElement> parent;
		if (FAILED(walker->GetParentElement(cur.Get(), &parent))) {
			break;
		}
		cur = parent;
	}
	return NULL;
}

static string RuntimeId(IUIAutomationElement* element) {
	SAFEARRAY* ids = NULL;
	if (FAILED(element->GetRuntimeId(&ids)) || ids == NULL) {
		return "";
	}
	string ret;
	LONG lower = 0, upper = -1;
	SafeArrayGetLBound(ids, 1, &lower);
	SafeArrayGetUBound(ids, 1, &upper);
	for (LONG i = lower; i <= upper; i++) {
		int value = 0;
		SafeArrayGetElement(ids, &i, &value);
		ret += (ret.empty() ? "" : ".") + to_string(value);
	}
	SafeArrayDestroy(ids);
	return ret;
}

bool WinPlatform::GetControlInfo(uint64_t control, ControlInfo& info) {
	ComPtr<IUIAutomationElement> element = Lookup(control);
	CONTROLTYPEID type = 0;
	if (!element || FAILED(element->get_CurrentControlType(&type))) {
		return false;
	}
	BSTR text = NULL;
	if (SUCCEEDED(element->get_CurrentName(&text))) {
		info.Name = FromBstr(text);
	}
	text = NULL;
	if (SUCCEEDED(element->get_CurrentAutomationId(&text))) {
		info.AutomationId = FromBstr(text);
	}
	text = NULL;
	if (SUCCEEDED(element->get_CurrentClassName(&text))) {
		info.ClassName = FromBstr(text);
	}
	info.Role = RoleName(type);
	info.RuntimeId = RuntimeId(element.Get());
	BOOL flag = FALSE;
	info.Enabled = SUCCEEDED(element->get_CurrentIsEnabled(&flag)) && flag;
	info.Focused = SUCCEEDED(element->get_CurrentHasKeyboardFocus(&flag)) && flag;
	info.Offscreen = SUCCEEDED(element->get_CurrentIsOffscreen(&flag)) && flag;
	RECT rect = { 0 };
	if (SUCCEEDED(element->get_CurrentBoundingRectangle(&rect))) {
		info.Bounds.X = rect.left;
		info.Bounds.Y = rect.top;
		info.Bounds.Width = rect.right - rect.left;
		info.Bounds.Height = rect.bottom - rect.top;
	}
	info.Window = (uint64_t)(uintptr_t)ElementWindow(element.Get());

	ComPtr<IUIAutomationInvokePattern> invoke;
	if (SUCCEEDED(element->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invoke))) && invoke) {
		info.Actions.push_back("click");
	}
	ComPtr<IUIAutomationTogglePattern> toggle;
	ToggleState state;
	if (SUCCEEDED(element->GetCurrentPatternAs(UIA_TogglePatternId, IID_PPV_ARGS(&toggle))) && toggle &&
		SUCCEEDED(toggle->get_CurrentToggleState(&state))) {
		info.Toggle = state == ToggleState_On ? 1 : state == ToggleState_Off ? 0 : 2;
		info.Actions.push_back("toggle");
	}
	ComPtr<IUIAutomationSelectionItemPattern> select;
	if (SUCCEEDED(element->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&select))) && select &&
		SUCCEEDED(select->get_CurrentIsSelected(&flag))) {
		info.Selected = flag ? 1 : 0;
		info.Actions.push_back("select");
	}
	ComPtr<IUIAutomationExpandCollapsePattern> expand;
	ExpandCollapseState expanded;
	if (SUCCEEDED(element->GetCurrentPatternAs(UIA_ExpandCollapsePatternId, IID_PPV_ARGS(&expand))) && expand &&
		SUCCEEDED(expand->get_CurrentExpandCollapseState(&expanded)) && expanded != ExpandCollapseState_LeafNode) {
		info.Expanded = expanded == ExpandCollapseState_Collapsed ? 0 : 1;
		info.Actions.push_back("expand");
		info.Actions.push_back("collapse");
	}
	ComPtr<IUIAutomationValuePattern> value;
	if (SUCCEEDED(element->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&value))) && value) {
		info.HasValue = true;
		text = NULL;
		if (SUCCEEDED(value->get_CurrentValue(&text))) {
			info.Value = FromBstr(text);
		}
		info.ReadOnly = !(SUCCEEDED(value->get_CurrentIsReadOnly(&flag)) && !flag);
		if (!info.ReadOnly) {
			info.Actions.push_back("set_value");
		}
	}
	ComPtr<IUIAutomationScrollItemPattern> scroll;
	if (SUCCEEDED(element->GetCurrentPatternAs(UIA_ScrollItemPatternId, IID_PPV_ARGS(&scroll))) && scroll) {
		info.Actions.push_back("scroll");
	}
	return true;
}

bool WinPlatform::GetControlText(uint64_t control, string& text) {
	// A document's text, else the value, else the name
	ComPtr<IUIAutomationElement> element = Lookup(control);
	if (!element) {
		return false;
	}
	BSTR bstr = NULL;
	ComPtr<IUIAutomationTextPattern> pattern;
	ComPtr<IUIAutomationTextRange> range;
	if (SUCCEEDED(element->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&pattern))) && pattern &&
		SUCCEEDED(pattern->get_DocumentRange(&range)) && range && SUCCEEDED(range->GetText(-1, &bstr))) {
		text = FromBstr(bstr);
		return true;
	}
	ComPtr<IUIAutomationValuePattern> value;
	bstr = NULL;
	if (SUCCEEDED(element->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&value))) && value && SUCCEEDED(value->get_CurrentValue(&bstr))) {
		text = FromBstr(bstr);
		return true;
	}
	bstr = NULL;
	if (SUCCEEDED(element->get_CurrentName(&bstr))) {
		text = FromBstr(bstr);
		return true;
	}
	return false;
}

template <class T>
static HRESULT WithPattern(IUIAutomationElement* element, PATTERNID id, function<HRESULT(T*)> act) {
	ComPtr<T> pattern;
	if (FAILED(element->GetCurrentPatternAs(id, IID_PPV_ARGS(&pattern))) || !pattern) {
		return E_NOINTERFACE;
	}
	return act(pattern.Get());
}

bool WinPlatform::ControlAction(uint64_t control, const string& action, const string& value, string& error) {
	ComPtr<IUIAutomationElement> element = Lookup(control);
	CONTROLTYPEID type = 0;
	if (!element || FAILED(element->get_CurrentControlType(&type))) {
		error = "That control is gone";
		return false;
	}
	HRESULT hr = E_NOINTERFACE;
	if (action == "click") {
		hr = WithPattern<IUIAutomationInvokePattern>(element.Get(), UIA_InvokePatternId, [](IUIAutomationInvokePattern* p) { return p->Invoke(); });
		// A button that opens a modal dialog can keep Invoke from returning
		// until the dialog closes; the click did happen
		if (hr == UIA_E_TIMEOUT) {
			hr = S_OK;
		}
	}
	else if (action == "toggle") {
		hr = WithPattern<IUIAutomationTogglePattern>(element.Get(), UIA_TogglePatternId, [](IUIAutomationTogglePattern* p) { return p->Toggle(); });
	}
	else if (action == "select") {
		hr = WithPattern<IUIAutomationSelectionItemPattern>(element.Get(), UIA_SelectionItemPatternId, [](IUIAutomationSelectionItemPattern* p) { return p->Select(); });
	}
	else if (action == "expand" || action == "collapse") {
		bool open = action == "expand";
		hr = WithPattern<IUIAutomationExpandCollapsePattern>(element.Get(), UIA_ExpandCollapsePatternId, [open](IUIAutomationExpandCollapsePattern* p) {
			return open ? p->Expand() : p->Collapse();
		});
	}
	else if (action == "set_value") {
		wstring wide = Utf8ToUtf16(value);
		hr = WithPattern<IUIAutomationValuePattern>(element.Get(), UIA_ValuePatternId, [&wide](IUIAutomationValuePattern* p) {
			BSTR text = SysAllocString(wide.c_str());
			HRESULT ret = p->SetValue(text);
			SysFreeString(text);
			return ret;
		});
	}
	else if (action == "scroll") {
		hr = WithPattern<IUIAutomationScrollItemPattern>(element.Get(), UIA_ScrollItemPatternId, [](IUIAutomationScrollItemPattern* p) { return p->ScrollIntoView(); });
	}
	else if (action == "focus") {
		hr = element->SetFocus();
	}
	else {
		error = "Unknown action " + action;
		return false;
	}
	if (hr == E_NOINTERFACE) {
		error = "A " + RoleName(type) + " can't " + (action == "set_value" ? string("take a value") : action);
		return false;
	}
	if (FAILED(hr)) {
		error = "The control didn't " + (action == "set_value" ? string("take the value") : action) + ": " + LastErrorText(hr);
		return false;
	}
	return true;
}

vector<uint64_t> WinPlatform::RelatedControls(uint64_t control, bool parent) {
	vector<uint64_t> ret;
	IUIAutomation* uia = Uia();
	ComPtr<IUIAutomationElement> element = Lookup(control);
	ComPtr<IUIAutomationTreeWalker> walker;
	if (uia == NULL || !element || FAILED(uia->get_ControlViewWalker(&walker))) {
		return ret;
	}
	if (parent) {
		// The desktop itself isn't a control anyone means
		ComPtr<IUIAutomationElement> up, desktop;
		BOOL same = FALSE;
		if (SUCCEEDED(walker->GetParentElement(element.Get(), &up)) && up &&
			!(SUCCEEDED(uia->GetRootElement(&desktop)) && SUCCEEDED(uia->CompareElements(up.Get(), desktop.Get(), &same)) && same)) {
			ret.push_back(Keep(up.Get()));
		}
		return ret;
	}
	ComPtr<IUIAutomationElement> child;
	if (FAILED(walker->GetFirstChildElement(element.Get(), &child))) {
		return ret;
	}
	while (child && ret.size() < MAX_CHILDREN) {
		ret.push_back(Keep(child.Get()));
		ComPtr<IUIAutomationElement> next;
		if (FAILED(walker->GetNextSiblingElement(child.Get(), &next))) {
			break;
		}
		child = next;
	}
	return ret;
}

uint64_t WinPlatform::FocusedControl() {
	IUIAutomation* uia = Uia();
	ComPtr<IUIAutomationElement> element;
	if (uia == NULL || FAILED(uia->GetFocusedElement(&element))) {
		return 0;
	}
	return Keep(element.Get());
}

static ScreenRect ToRect(double left, double top, double width, double height) {
	ScreenRect ret;
	ret.X = (int)floor(left);
	ret.Y = (int)floor(top);
	ret.Width = (int)ceil(left + width) - ret.X;
	ret.Height = (int)ceil(top + height) - ret.Y;
	return ret;
}

static bool Showing(const ScreenRect& rect, HWND top) {
	// UI Automation knows what's scrolled away but not what another window
	// covers, so check what's really at the middle
	if (rect.Width <= 0 || rect.Height <= 0) {
		return false;
	}
	POINT pt = { rect.X + rect.Width / 2, rect.Y + rect.Height / 2 };
	HWND at = WindowFromPoint(pt);
	return at != NULL && (top == NULL || GetAncestor(at, GA_ROOT) == top);
}

static bool Belongs(IUIAutomationElement* element, const ScreenRect& rect) {
	// Whether what's at the middle of a match on screen is in the document
	// or control it was found in.  A browser can keep a hidden page, such
	// as a new tab made ready ahead of time, whose text says it's where the
	// page in view is; and a page can cover its own text with a pop-up.
	IUIAutomation* uia = Uia();
	ComPtr<IUIAutomationElement> cur;
	ComPtr<IUIAutomationTreeWalker> walker;
	POINT pt = { rect.X + rect.Width / 2, rect.Y + rect.Height / 2 };
	if (uia == NULL || FAILED(uia->ElementFromPoint(pt, &cur)) || !cur || FAILED(uia->get_RawViewWalker(&walker))) {
		return true;
	}
	for (int depth = 0; cur && depth < 100; depth++) {
		BOOL same = FALSE;
		if (SUCCEEDED(uia->CompareElements(cur.Get(), element, &same)) && same) {
			return true;
		}
		ComPtr<IUIAutomationElement> parent;
		if (FAILED(walker->GetParentElement(cur.Get(), &parent))) {
			break;
		}
		cur = parent;
	}
	return false;
}

static vector<ScreenRect> RangeRects(IUIAutomationTextRange* range, HWND top) {
	// One rectangle per line the range covers that can be seen
	vector<ScreenRect> ret;
	SAFEARRAY* rects = NULL;
	if (FAILED(range->GetBoundingRectangles(&rects)) || rects == NULL) {
		return ret;
	}
	double* values = NULL;
	LONG lower = 0, upper = -1;
	SafeArrayGetLBound(rects, 1, &lower);
	SafeArrayGetUBound(rects, 1, &upper);
	if (SUCCEEDED(SafeArrayAccessData(rects, (void**)&values))) {
		for (LONG i = 0; i + 3 <= upper - lower; i += 4) {
			ScreenRect rect = ToRect(values[i], values[i + 1], values[i + 2], values[i + 3]);
			if (Showing(rect, top)) {
				ret.push_back(rect);
			}
		}
		SafeArrayUnaccessData(rects);
	}
	SafeArrayDestroy(rects);
	return ret;
}

static wstring RangeTextW(IUIAutomationTextRange* range) {
	BSTR text = NULL;
	if (FAILED(range->GetText(-1, &text)) || text == NULL) {
		return L"";
	}
	wstring ret(text, SysStringLen(text));
	SysFreeString(text);
	return ret;
}

static string RangeText(IUIAutomationTextRange* range) {
	return Utf16ToUtf8(RangeTextW(range));
}

static int FindIn(const wstring& text, const wstring& part, int from, bool matchCase) {
	// Where part is in text, from a position on, or -1
	if (from < 0 || from > (int)text.size()) {
		return -1;
	}
	int at = FindNLSStringEx(LOCALE_NAME_INVARIANT, FIND_FROMSTART | (matchCase ? 0 : LINGUISTIC_IGNORECASE), text.c_str() + from,
		(int)text.size() - from, part.c_str(), (int)part.size(), NULL, NULL, NULL, 0);
	return at < 0 ? -1 : at + from;
}

static bool Contains(const wstring& text, const wstring& part, bool matchCase) {
	return FindIn(text, part, 0, matchCase) >= 0;
}

static ComPtr<IUIAutomationTextRange> Realign(IUIAutomationTextRange* hit, const wstring& wanted, bool matchCase) {
	// The range for a match, when what was found isn't the text.  Chrome's
	// search hands back a range a character later for each paragraph before
	// the match, so the text is looked for again a little before it, ever
	// further back, and the range is moved onto it.
	for (int reach = 16; reach <= 4096; reach *= 4) {
		ComPtr<IUIAutomationTextRange> wider, fixed;
		int back = 0, moved = 0;
		if (FAILED(hit->Clone(&wider)) || FAILED(wider->MoveEndpointByUnit(TextPatternRangeEndpoint_Start, TextUnit_Character, -reach, &back)) ||
			FAILED(wider->MoveEndpointByUnit(TextPatternRangeEndpoint_End, TextUnit_Character, CONTEXT_CHARS, &moved))) {
			return nullptr;
		}
		wstring text = RangeTextW(wider.Get());
		// The last place at or before where the search said, else the first
		// after it
		int start = -back, before = -1, after = -1;
		for (int at = FindIn(text, wanted, 0, matchCase); at >= 0; at = FindIn(text, wanted, at + 1, matchCase)) {
			if (at <= start) {
				before = at;
			}
			else if (after < 0) {
				after = at;
			}
		}
		int best = before >= 0 ? before : after;
		if (best < 0) {
			if (back > -reach) {
				return nullptr;
			}
			continue;
		}
		if (FAILED(wider->Clone(&fixed)) || FAILED(fixed->MoveEndpointByUnit(TextPatternRangeEndpoint_Start, TextUnit_Character, best, &moved)) ||
			moved != best || FAILED(fixed->MoveEndpointByRange(TextPatternRangeEndpoint_End, fixed.Get(), TextPatternRangeEndpoint_Start)) ||
			FAILED(fixed->MoveEndpointByUnit(TextPatternRangeEndpoint_End, TextUnit_Character, (int)wanted.size(), &moved))) {
			return nullptr;
		}
		wstring got = RangeTextW(fixed.Get());
		return got.size() == wanted.size() && Contains(got, wanted, matchCase) ? fixed : nullptr;
	}
	return nullptr;
}

static string Around(IUIAutomationTextRange* hit) {
	// A match and a few characters either side.  Some browsers count
	// characters a little differently from the text they give, so this is
	// wider than one character, and the match is looked for in it.
	ComPtr<IUIAutomationTextRange> wider;
	int moved = 0;
	if (FAILED(hit->Clone(&wider)) ||
		FAILED(wider->MoveEndpointByUnit(TextPatternRangeEndpoint_Start, TextUnit_Character, -CONTEXT_CHARS, &moved)) ||
		FAILED(wider->MoveEndpointByUnit(TextPatternRangeEndpoint_End, TextUnit_Character, CONTEXT_CHARS, &moved))) {
		return "";
	}
	return RangeText(wider.Get());
}

static void SearchDocument(IUIAutomationElement* doc, const wstring& text, bool matchCase, size_t limit, HWND top, vector<TextMatch>& found) {
	ComPtr<IUIAutomationTextPattern> pattern;
	ComPtr<IUIAutomationTextRange> range;
	if (FAILED(doc->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&pattern))) || !pattern ||
		FAILED(pattern->get_DocumentRange(&range)) || !range) {
		return;
	}
	BSTR wanted = SysAllocString(text.c_str());
	// Every match is looked at, seen or not, so there's a cap on how many
	for (int tries = 0; found.size() < limit && tries < 1000; tries++) {
		ComPtr<IUIAutomationTextRange> hit, previous;
		if (FAILED(range->FindText(wanted, FALSE, matchCase ? FALSE : TRUE, &hit)) || !hit || FAILED(range->Clone(&previous))) {
			break;
		}
		ComPtr<IUIAutomationTextRange> spot = hit;
		wstring got = RangeTextW(hit.Get());
		if (got.size() != text.size() || !Contains(got, text, matchCase)) {
			spot = Realign(hit.Get(), text, matchCase);
		}
		TextMatch match;
		if (spot) {
			match.Rects = RangeRects(spot.Get(), top);
		}
		if (!match.Rects.empty() && Belongs(doc, match.Rects[0])) {
			match.InDocument = true;
			match.Text = RangeText(spot.Get());
			match.Context = Around(spot.Get());
			found.push_back(match);
		}
		// Carry on after this match, as long as that's further on: an empty
		// match, or one from before where the search started, would go round
		// for ever
		int ahead = 0;
		if (FAILED(range->MoveEndpointByRange(TextPatternRangeEndpoint_Start, hit.Get(), TextPatternRangeEndpoint_End)) ||
			FAILED(range->CompareEndpoints(TextPatternRangeEndpoint_Start, previous.Get(), TextPatternRangeEndpoint_Start, &ahead)) || ahead <= 0) {
			break;
		}
	}
	SysFreeString(wanted);
}

static HRESULT PropertyIs(IUIAutomation* uia, PROPERTYID property, int value, ComPtr<IUIAutomationCondition>& cond) {
	VARIANT var;
	VariantInit(&var);
	var.vt = VT_I4;
	var.lVal = value;
	return uia->CreatePropertyCondition(property, var, &cond);
}

bool WinPlatform::FindControlText(uint64_t root, uint64_t window, const string& text, bool matchCase, bool documents, size_t limit,
	vector<TextMatch>& found, string& error) {
	IUIAutomation* uia = Uia();
	ComPtr<IUIAutomationElement> start;
	if (!StartElement(uia, root, window, start, error)) {
		return false;
	}
	HWND top = root != 0 ? ElementWindow(start.Get()) : GetAncestor(window != 0 ? (HWND)(uintptr_t)window : GetForegroundWindow(), GA_ROOT);
	wstring wide = Utf8ToUtf16(text);

	// Documents and text boxes search their own text, which finds a phrase
	// anywhere in a web page, even split across links or bold words.  Not
	// every program says which have text it can search, so each one is
	// asked.
	ComPtr<IUIAutomationCondition> isDoc, isEdit, docs;
	ComPtr<IUIAutomationElementArray> elements;
	int count = 0;
	if (documents && SUCCEEDED(PropertyIs(uia, UIA_ControlTypePropertyId, UIA_DocumentControlTypeId, isDoc)) &&
		SUCCEEDED(PropertyIs(uia, UIA_ControlTypePropertyId, UIA_EditControlTypeId, isEdit)) &&
		SUCCEEDED(uia->CreateOrCondition(isDoc.Get(), isEdit.Get(), &docs)) &&
		SUCCEEDED(start->FindAll(TreeScope_Subtree, docs.Get(), &elements)) && elements) {
		elements->get_Length(&count);
	}
	if (documents) {
		for (int i = 0; i < count && found.size() < limit; i++) {
			ComPtr<IUIAutomationElement> doc;
			if (SUCCEEDED(elements->GetElement(i, &doc)) && doc) {
				SearchDocument(doc.Get(), wide, matchCase, limit, top, found);
			}
		}
		return true;
	}

	// Otherwise controls whose names have the text, such as buttons and
	// links outside any document, or whose values do, such as text boxes.
	// Whole windows, panes and documents are named for their titles, which
	// isn't text anyone means to click.
	VARIANT value;
	VariantInit(&value);
	value.vt = VT_BSTR;
	value.bstrVal = SysAllocString(wide.c_str());
	PropertyConditionFlags flags = (PropertyConditionFlags)(PropertyConditionFlags_MatchSubstring | (matchCase ? 0 : PropertyConditionFlags_IgnoreCase));
	ComPtr<IUIAutomationCondition> named, valued, either, controlView, both;
	HRESULT hr = uia->CreatePropertyConditionEx(UIA_NamePropertyId, value, flags, &named);
	if (SUCCEEDED(hr)) {
		hr = uia->CreatePropertyConditionEx(UIA_ValueValuePropertyId, value, flags, &valued);
	}
	if (SUCCEEDED(hr)) {
		hr = uia->CreateOrCondition(named.Get(), valued.Get(), &either);
	}
	VariantClear(&value);
	ComPtr<IUIAutomationCacheRequest> cache;
	ComPtr<IUIAutomationElementArray> controls;
	count = 0;
	if (SUCCEEDED(hr) && SUCCEEDED(uia->get_ControlViewCondition(&controlView)) &&
		SUCCEEDED(uia->CreateAndCondition(controlView.Get(), either.Get(), &both)) && SUCCEEDED(uia->CreateCacheRequest(&cache))) {
		cache->AddProperty(UIA_NamePropertyId);
		cache->AddProperty(UIA_ValueValuePropertyId);
		cache->AddProperty(UIA_ControlTypePropertyId);
		cache->AddProperty(UIA_BoundingRectanglePropertyId);
		cache->AddProperty(UIA_IsOffscreenPropertyId);
		hr = start->FindAllBuildCache(TreeScope_Descendants, both.Get(), cache.Get(), &controls);
		if (FAILED(hr)) {
			error = "The window didn't answer: " + LastErrorText(hr);
			return false;
		}
		if (controls) {
			controls->get_Length(&count);
		}
	}
	for (int i = 0; i < count && found.size() < limit; i++) {
		ComPtr<IUIAutomationElement> element;
		CONTROLTYPEID type = 0;
		BOOL offscreen = FALSE;
		RECT rect = { 0 };
		BSTR name = NULL;
		if (FAILED(controls->GetElement(i, &element)) || !element || FAILED(element->get_CachedControlType(&type)) ||
			type == UIA_WindowControlTypeId || type == UIA_PaneControlTypeId || type == UIA_DocumentControlTypeId ||
			type == UIA_TitleBarControlTypeId || (SUCCEEDED(element->get_CachedIsOffscreen(&offscreen)) && offscreen) ||
			FAILED(element->get_CachedBoundingRectangle(&rect))) {
			continue;
		}
		TextMatch match;
		match.Rects.push_back(ToRect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top));
		if (!Showing(match.Rects[0], top) || !Belongs(element.Get(), match.Rects[0]) || FAILED(element->get_CachedName(&name))) {
			continue;
		}
		match.Name = FromBstr(name);
		// The value instead, when it's the value that has the text
		VARIANT current;
		VariantInit(&current);
		if (!Contains(Utf8ToUtf16(match.Name), wide, matchCase) &&
			SUCCEEDED(element->GetCachedPropertyValue(UIA_ValueValuePropertyId, &current)) && current.vt == VT_BSTR && current.bstrVal != NULL) {
			match.Name = Utf16ToUtf8(current.bstrVal, SysStringLen(current.bstrVal));
		}
		VariantClear(&current);
		found.push_back(match);
	}
	return true;
}

uint64_t WinPlatform::ControlAt(int x, int y) {
	IUIAutomation* uia = Uia();
	ComPtr<IUIAutomationElement> element;
	POINT pt = { x, y };
	if (uia == NULL || FAILED(uia->ElementFromPoint(pt, &element))) {
		return 0;
	}
	return Keep(element.Get());
}
