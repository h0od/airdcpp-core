/*
 * Copyright (C) 2001-2006 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "stdafx.h"
#include "../client/DCPlusPlus.h"
#include "Resource.h"

#include "SystemFrame.h"
#include "WinUtil.h"
#include "TextFrame.h"
#include "../client/File.h"
#include "../client/LogManager.h"
#include "../client/ShareManager.h"

LRESULT SystemFrame::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
	ctrlPad.Create(m_hWnd, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN |
		WS_VSCROLL | ES_MULTILINE | ES_NOHIDESEL, WS_EX_CLIENTEDGE);

	ctrlPad.SetReadOnly(TRUE);
	ctrlPad.SetFont(WinUtil::font);
	ctrlPad.SetBackgroundColor(WinUtil::bgColor); 
	ctrlPad.SetDefaultCharFormat(WinUtil::m_ChatTextGeneral);
	ctrlPad.LimitText(128*1024);
	ctrlClientContainer.SubclassWindow(ctrlPad.m_hWnd);
	
	reg.assign(_T("((?<=\\s)(([A-Za-z0-9]:)|(\\\\))(\\\\[^\\\\:]+)(\\\\([^\\s:])([^\\\\:])+)*((\\.[a-z0-9]{2,10})|(\\\\))(?=(\\s|$|:|,)))"));

	//might miss some messages
	auto oldMessages = LogManager::getInstance()->getLastLogs();
	LogManager::getInstance()->addListener(this);

	for(auto i = oldMessages.begin(); i != oldMessages.end(); ++i) {
		addLine(i->second, Text::toT(i->first));
	}


	tabMenu = CreatePopupMenu();
	if(BOOLSETTING(LOG_SYSTEM)) {
		tabMenu.AppendMenu(MF_STRING, IDC_OPEN_SYSTEM_LOG, CTSTRING(OPEN_SYSTEM_LOG));
		tabMenu.AppendMenu(MF_SEPARATOR);
	}
	tabMenu.AppendMenu(MF_STRING, IDC_CLOSE_WINDOW, CTSTRING(CLOSE));

	CRect rc(SETTING(SYSLOG_LEFT), SETTING(SYSLOG_TOP), SETTING(SYSLOG_RIGHT), SETTING(SYSLOG_BOTTOM));
	if(! (rc.top == 0 && rc.bottom == 0 && rc.left == 0 && rc.right == 0) )
		MoveWindow(rc, TRUE);

	SettingsManager::getInstance()->addListener(this);
	WinUtil::SetIcon(m_hWnd, _T("systemlog.ico"));
	bHandled = FALSE;
	return 1;
}

LRESULT SystemFrame::onClose(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	CRect rc;
	if(!IsIconic()){
		//Get position of window
		GetWindowRect(&rc);
				
		//convert the position so it's relative to main window
		::ScreenToClient(GetParent(), &rc.TopLeft());
		::ScreenToClient(GetParent(), &rc.BottomRight());
				
		//save the position
		SettingsManager::getInstance()->set(SettingsManager::SYSLOG_BOTTOM, (rc.bottom > 0 ? rc.bottom : 0));
		SettingsManager::getInstance()->set(SettingsManager::SYSLOG_TOP, (rc.top > 0 ? rc.top : 0));
		SettingsManager::getInstance()->set(SettingsManager::SYSLOG_LEFT, (rc.left > 0 ? rc.left : 0));
		SettingsManager::getInstance()->set(SettingsManager::SYSLOG_RIGHT, (rc.right > 0 ? rc.right : 0));
	}
	LogManager::getInstance()->removeListener(this);
	SettingsManager::getInstance()->removeListener(this);
	bHandled = FALSE;
	WinUtil::setButtonPressed(IDC_SYSTEM_LOG, false);
	return 0;
	
}

void SystemFrame::UpdateLayout(BOOL /*bResizeBars*/ /* = TRUE */)
{
	CRect rc;

	GetClientRect(rc);
	
	rc.bottom -= 1;
	rc.top += 1;
	rc.left +=1;
	rc.right -=1;
	ctrlPad.MoveWindow(rc);
	
}

LRESULT SystemFrame::onLButton(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
	HWND focus = GetFocus();
	bHandled = false;
	if(focus == ctrlPad.m_hWnd) {
		POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		tstring x;
		tstring::size_type start = (tstring::size_type)WinUtil::textUnderCursor(pt, ctrlPad, x);
		tstring::size_type end = x.find(_T(" "), start);

		if(end == tstring::npos)
			end = x.length();
		
		bHandled = WinUtil::parseDBLClick(x.substr(start, end-start));
	}
	return 0;
}

LRESULT SystemFrame::onSpeaker(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	auto_ptr<pair<LogManager::MessageData, tstring> > msg((pair<LogManager::MessageData, tstring>*)wParam);
	
	addLine(msg->first, msg->second);
	setDirty();
	return 0;
}

void SystemFrame::addLine(LogManager::MessageData md, const tstring& msg) {
	ctrlPad.SetRedraw(FALSE);
	
	POINT pt = { 0 };
	bool scroll = scrollIsAtEnd();
	ctrlPad.GetScrollPos(&pt);

	LONG SavedBegin, SavedEnd;
	LONG Begin = 0; 
	LONG End = 0;

	ctrlPad.GetSel(SavedBegin, SavedEnd);

	End = Begin = ctrlPad.GetTextLengthEx(GTL_NUMCHARS);

	tstring Text = msg + _T(" "); //kinda strange, but adding line endings in the start of new line makes it that way.
	tstring time = Text::toT("\r\n [" + Util::getTimeStamp(md.time) + "] ");
	tstring line = time + Text;

	LONG limitText = ctrlPad.GetLimitText();
	LONG TextLength = End + line.size();

	if((TextLength +1) > limitText) {
		dcdebug("textlength %s \n", Util::toString(TextLength).c_str());
		LONG RemoveEnd = 0;
		RemoveEnd = ctrlPad.LineIndex(ctrlPad.LineFromChar(2000));
		End = Begin -=RemoveEnd;
		SavedBegin -= RemoveEnd;
		SavedEnd -= RemoveEnd;
		//fix the scroll position if text was removed from the start
		pt.y -= ctrlPad.PosFromChar(RemoveEnd).y;

		ctrlPad.SetSel(0, RemoveEnd);
		ctrlPad.ReplaceSel(_T(""));

	}

	ctrlPad.AppendText(line.c_str());

	End += time.size() -1;
	ctrlPad.SetSel(Begin, End);
	ctrlPad.SetSelectionCharFormat(WinUtil::m_TextStyleTimestamp);

	if (md.severity == LogManager::LOG_ERROR) {
		ctrlPad.SetSel(End, End+Text.length()-1);
		CHARFORMAT2 ec = WinUtil::m_ChatTextGeneral;
		ec.crTextColor = SETTING(ERROR_COLOR);
		ctrlPad.SetSelectionCharFormat(ec);
	}

	Colorize(Text, End); //timestamps should always be timestamps right?
	
	ctrlPad.SetSel(SavedBegin, SavedEnd); //restore the user selection

	if(scroll) {                
		ctrlPad.PostMessage(EM_SCROLL, SB_BOTTOM, 0);
	} else {
		ctrlPad.SetScrollPos(&pt);
	}
	
	ctrlPad.SetRedraw(TRUE);
	ctrlPad.InvalidateRect(NULL);
}

void SystemFrame::Colorize(const tstring& line, LONG Begin){

	tstring::const_iterator start = line.begin();
	tstring::const_iterator end = line.end();
	boost::match_results<tstring::const_iterator> result;
	int pos=0;

	while(boost::regex_search(start, end, result, reg, boost::match_default)) {
		ctrlPad.SetSel(pos + Begin + result.position(), pos + Begin + result.position() + result.length());
		ctrlPad.SetSelectionCharFormat(WinUtil::m_ChatTextServer);
		start = result[0].second;
		pos=pos+result.position() + result.length();
	}
}

void SystemFrame::scrollToEnd() {
	POINT pt = { 0 };

	ctrlPad.GetScrollPos(&pt);
	ctrlPad.PostMessage(EM_SCROLL, SB_BOTTOM, 0);
	ctrlPad.SetScrollPos(&pt);
}

bool SystemFrame::scrollIsAtEnd() {
	SCROLLINFO si = { 0 };
	si.cbSize = sizeof(si);
	si.fMask = SIF_ALL;
	ctrlPad.GetScrollInfo(SB_VERT, &si);

	return (si.nPage == 0 || ((size_t)si.nPos >= (size_t)si.nMax - si.nPage - 5) && ((size_t)si.nTrackPos >= (size_t)si.nMax - si.nPage - 5));
}


LRESULT SystemFrame::onTabContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/) {
	POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };        // location of mouse click 
	tabMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd);
	return TRUE;
}

LRESULT SystemFrame::onSystemLog(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	string filename = LogManager::getInstance()->getPath(LogManager::SYSTEM);
	if(Util::fileExists(filename)){
		WinUtil::viewLog(filename);
	} else {
		MessageBox(CTSTRING(NO_LOG_FOR_HUB),CTSTRING(NO_LOG_FOR_HUB), MB_OK );	  
	}
	
	return 0; 
}

void SystemFrame::on(SettingsManagerListener::Save, SimpleXML& /*xml*/) noexcept {
    PostMessage(WM_REFRESH_SETTINGS);
}

void SystemFrame::on(Message, time_t t, const string& message, uint8_t sev) {
	PostMessage(WM_SPEAKER, (WPARAM)(new pair<LogManager::MessageData, tstring>(LogManager::MessageData(t, (LogManager::Severity)sev), Text::toT(message))));
}

LRESULT SystemFrame::onRefreshSettings(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
    RedrawWindow(NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
	return 0;
}

LRESULT SystemFrame::onContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/) {
	POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };        // location of mouse click

	if(pt.x == -1 && pt.y == -1) {
		CRect erc;
		ctrlPad.GetRect(&erc);
		pt.x = erc.Width() / 2;
		pt.y = erc.Height() / 2;
		ClientToScreen(&pt);
	}

	POINT ptCl = pt;
	ScreenToClient(&ptCl); 
	OnRButtonDown(ptCl); 

	OMenu menu;
	menu.CreatePopupMenu();

	menu.AppendMenu(MF_STRING, ID_EDIT_COPY, CTSTRING(COPY));
	if (!selWord.empty()) {
		menu.AppendMenu(MF_SEPARATOR);
		menu.AppendMenu(MF_STRING, IDC_SEARCHDIR, CTSTRING(SEARCH_DIRECTORY));
		if (selWord[selWord.length()-1] != PATH_SEPARATOR)
			menu.AppendMenu(MF_STRING, IDC_SEARCH, CTSTRING(SEARCH_FILENAME));
	
		menu.AppendMenu(MF_SEPARATOR);
		menu.AppendMenu(MF_STRING, IDC_OPEN_FOLDER, CTSTRING(OPEN_FOLDER));
	}

	menu.AppendMenu(MF_SEPARATOR);
	if(BOOLSETTING(LOG_SYSTEM)) {
		menu.AppendMenu(MF_STRING, IDC_OPEN_SYSTEM_LOG, CTSTRING(OPEN_SYSTEM_LOG));
		menu.AppendMenu(MF_SEPARATOR);
	}
	menu.AppendMenu(MF_STRING, ID_EDIT_SELECT_ALL, CTSTRING(SELECT_ALL));
	menu.AppendMenu(MF_STRING, ID_EDIT_CLEAR_ALL, CTSTRING(CLEAR_MESSAGES));
	
	menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd);

	return 0;

}
LRESULT SystemFrame::OnRButtonDown(POINT pt) {
	selWord.clear();

	selWord = WordFromPos(pt);
	return 0;
}

tstring SystemFrame::WordFromPos(const POINT& p) {

	int iCharPos = ctrlPad.CharFromPos(p);
	int line = ctrlPad.LineFromChar(iCharPos);
	int len = ctrlPad.LineLength(iCharPos);
	if(len < 3)
		return Util::emptyStringT;

	int Begin = ctrlPad.LineIndex(line);
	int c = iCharPos - Begin;

	tstring x;
	x.resize(len+1);
	x.resize(ctrlPad.GetLine(line, &x[0], len+1));

	tstring::const_iterator start = x.begin();
	tstring::const_iterator end = x.end();
	boost::match_results<tstring::const_iterator> result;
	int pos=0;

	while(boost::regex_search(start, end, result, reg, boost::match_default)) {
		if (pos + result.position() <= c && pos + result.position() + result.length() >= c) {
			ctrlPad.SetSel(pos + Begin + result.position(), pos + Begin + result.position() + result.length());
			return x.substr(pos + result.position(), result.length());
		}
		start = result[0].second;
		pos=pos+result.position() + result.length();
	}

	/* No path found, try to get the current selection */
	tstring ret;
	CHARRANGE cr;
	ctrlPad.GetSel(cr);
	if(cr.cpMax != cr.cpMin) {
		TCHAR *buf = new TCHAR[cr.cpMax - cr.cpMin + 1];
		ctrlPad.GetSelText(buf);
		ret = Util::replace(buf, _T("\r"), _T("\r\n"));
		delete[] buf;
	}

	return ret;
}

LRESULT SystemFrame::onSize(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if((wParam != SIZE_MINIMIZED && HIWORD(lParam) > 0) && scrollIsAtEnd()) {
		scrollToEnd();
	}

	bHandled = FALSE;
	return 0;
}

LRESULT SystemFrame::onOpenFolder(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	tstring tmp = Util::getFilePath(selWord); //need to pick up the path here if we have a missing file, they dont exist :)
	if(Util::fileExists(Text::fromT(tmp)))
		WinUtil::openFolder(tmp);

	ctrlPad.SetSelNone();
	return 0;
}
LRESULT SystemFrame::onEditCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	ctrlPad.Copy();
	return 0;
}
LRESULT SystemFrame::onEditSelectAll(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	ctrlPad.SetSelAll();
	return 0;
}

LRESULT SystemFrame::onEditClearAll(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	LogManager::getInstance()->clearLastLogs();
	ctrlPad.SetWindowText(_T(""));
	return 0;
}

LRESULT SystemFrame::onSearchFile(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	WinUtil::searchAny(Util::getFileName(selWord));
	return 0;
}

LRESULT SystemFrame::onSearchDir(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	WinUtil::searchAny(Text::toT(Util::getDir(Text::fromT(selWord), true, true)));
	return 0;
}