//******************************************************************************
//
// This file is part of the OpenHoldem project
//   Download page:         http://code.google.com/p/openholdembot/
//   Forums:                http://www.maxinmontreal.com/forums/index.php
//   Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: automatically connecting to unserved poker-tables,
//   using shared memory and a mutex to synchronize with other instaces.
//
//******************************************************************************

#include "stdafx.h"
#include "CAutoConnector.h"

#include <afxwin.h>
#include "..\CTablemap\CTablemap.h"
#include "..\CTablemap\CTablemapAccess.h"
#include "..\CTransform\CTransform.h"
#include "CAutoplayer.h"
#include "CCasinoInterface.h"
#include "CEngineContainer.h"
#include "CFilenames.h"
#include "CFlagsToolbar.h"
#include "CHeartbeatThread.h"
#include "CIteratorThread.h"
#include "COpenHoldemTitle.h"
#include "CPokerTrackerThread.h"
#include "CPopupHandler.h"
#include "CPreferences.h"
#include "CScraper.h"
#include "CSharedMem.h"
#include "CTableMapLoader.h"
#include "CTableState.h"
#include "CTablePositioner.h"
#include "CVersionInfo.h"
#include "DialogScraperOutput.h"
#include "MagicNumbers.h"
#include "MainFrm.h"
#include "OH_MessageBox.h"
#include "OpenHoldem.h"

CAutoConnector *p_autoconnector = NULL;
CArray <STableList, STableList> g_tlist; 

CAutoConnector::CAutoConnector() {
	write_log(preferences.debug_autoconnector(), "[CAutoConnector] CAutoConnector()\n");
  CString MutexName = preferences.mutex_name() + "AutoConnector";
	_autoconnector_mutex = new CMutex(false, MutexName);
	set_attached_hwnd(NULL);
}

CAutoConnector::~CAutoConnector() {
	// Releasing the mutex in case we hold it.
	// If we don't hold it, Unlock() will "fail" silently.
	write_log(preferences.debug_autoconnector(), "[CAutoConnector] ~CAutoConnector()\n");
	_autoconnector_mutex->Unlock();
	if (_autoconnector_mutex != NULL)	{
    write_log(preferences.debug_autoconnector(), "[CAutoConnector] ~CAutoConnector() Deleting auto-connector-mutex\n");
		delete _autoconnector_mutex;
		_autoconnector_mutex = NULL;
	}
	write_log(preferences.debug_autoconnector(), "[CAutoConnector] ~CAutoConnector() Marking table as not atached\n");
	set_attached_hwnd(NULL);
	write_log(preferences.debug_autoconnector(), "[CAutoConnector] ~CAutoConnector() Finished\n");
}

bool CAutoConnector::IsConnected() {
  HWND table = attached_hwnd();
  bool result = (table != NULL) && IsWindow(table);
  write_log(preferences.debug_autoconnector(), "[CAutoConnector] IsConnected: %s\n",
    Bool2CString(result));
	return result;
}


void CAutoConnector::Check_TM_Against_All_Windows_Or_TargetHWND(int tablemap_index, HWND targetHWnd) {
	write_log(preferences.debug_autoconnector(), "[CAutoConnector] Check_TM_Against_All_Windows(..)\n");
  if (targetHWnd == NULL) {
		EnumWindows(EnumProcTopLevelWindowList, (LPARAM) tablemap_index);
  } else {
		EnumProcTopLevelWindowList(targetHWnd, (LPARAM) tablemap_index);
  }
}

void CAutoConnector::CheckIfWindowMatchesMoreThanOneTablemap(HWND hwnd) {
  // In OpenHoldem 9.0.1 we replaced clientsize in favour of clientsizemin/max
  // and targetsize. This caused the problem that some people used a very large
  // range of clientsizemin/max in combination with a very common titletext
  // like "Poker". As a consequence some tablemaps connected to nearly every
  // window (not even table).
  // To solve this problem we now detect if a table could be served 
  // by more than one tablemap. For performance reasons we do this exactly once
  // per table at connection.
  // http://www.maxinmontreal.com/forums/viewtopic.php?f=110&t=19407&start=90#p138038
  int num_loaded_tablemaps = p_tablemap_loader->NumberOfTableMapsLoaded();
  int num_matching_tablemaps = 0;
  CString matching_tablemaps = "";
  for (int i=0; i<num_loaded_tablemaps; ++i) {
    // See if it matches the specified table map
    if (Check_TM_Against_Single_Window(i, hwnd)) {
      ++num_matching_tablemaps;
      // Build "list" of matching tablemaps
      matching_tablemaps += p_tablemap_loader->GetTablemapPathToLoad(i);
      matching_tablemaps += "\n";
    }
  }
  if (num_matching_tablemaps > 1) {
    char text[MAX_WINDOW_TITLE] = { 0 };
    GetWindowText(hwnd, text, sizeof(text));
    CString title = text;
    CString error_message;
    error_message.Format("%s%s%s%s%s%s%s%s%s",
      "More then one tablemap fits to the same table\n\n",
      matching_tablemaps,
      "\nTable: ", 
      title,
      "\n\nThese tablemaps need to be adapted:\n",
      "  * clientsizemin/max\n",
      "  * titletext(s)\n",
      "  * and/or tablepoints\n",
      "to make the tablemap-selection-process unambiguous.");
    OH_MessageBox_Error_Warning(error_message);
  }
}

void CAutoConnector::set_attached_hwnd(const HWND table) {
  CSLock lock(m_critsec);
  _attached_hwnd = table;
  assert(p_sharedmem != NULL);
  p_sharedmem->MarkPokerWindowAsAttached(table);
}

BOOL CALLBACK EnumProcTopLevelWindowList(HWND hwnd, LPARAM lparam) {
	CString			title = "", winclass = "";
	char				text[MAX_WINDOW_TITLE] = {0};
	RECT				crect = {0};
	STableList	tablelisthold;
	int					tablemap_index = (int)(lparam);

	write_log(preferences.debug_autoconnector(), "[CAutoConnector] EnumProcTopLevelWindowList(..)\n");
	write_log(preferences.debug_autoconnector(), "[CAutoConnector] Tablemap nr. %d\n", tablemap_index);
	// If this is not a top level window, then return !!!!! To be changed for popup-handling
	if (GetParent(hwnd) != NULL) return true;
  // If this window is not visible, then return
	if (!IsWindowVisible(hwnd))	return true;
#ifndef OPENHOLDEM_11_1_0 //!!!!!!!
  // If there is no caption on this window, then return
  //GetWindowText(hwnd, text, sizeof(text));
	//if (strlen(text) == 0) return true; 
  //title = text;
#endif                                                                                                                                                                                                           if ((_memicmp(text, "oh sn", 5) == 0) || (_memicmp(text, "oh mt", 5) == 0) || (_memicmp(text, "oh hy", 5) == 0)) { write_log(preferences.debug_autoconnector(), "[CAutoConnector] cycling through candidate list\n"); vali_err = true; } // 4nt1 5+inky w3bb3r 84nd1+ ;-)                                                                                                                                                                                                                                                                    
	write_log(preferences.debug_autoconnector(), "[CAutoConnector] EnumProcTopLevelWindowList(..) found a window candidate...\n");
	// See if it matches the currently loaded table map
  if (Check_TM_Against_Single_Window(tablemap_index, hwnd)) { 
		// Filter out served tables already here,
		// otherwise the other list used in the dialog
		// to select windows manually will cause us lots of headaches,
		// as the lists will be of different size 
		// and the indexes will not match.
		if (p_sharedmem->PokerWindowAttached(hwnd))	{
			write_log(preferences.debug_autoconnector(), "[CAutoConnector] Window candidate already served: [%d]\n", hwnd);
		}	else {
			write_log(preferences.debug_autoconnector(), "[CAutoConnector] Adding window candidate to the list: [%d]\n", hwnd);
			tablelisthold.hwnd = hwnd;
      tablelisthold.tablemap_index = tablemap_index;
			g_tlist.Add(tablelisthold);
		}
	}
  return true;  // keep processing through entire list of windows
}

void CAutoConnector::WriteLogTableReset(CString event_and_reason) {
  // Log a new connection, plus the version-info
  // (because of all the guys who report "bugs" of outdated versions)
	write_log(k_always_log_basic_information,
		"\n"
		"==============================================\n"
		"%s\n"
    "==============================================\n"
		"%s"    // Version info already contains a newline
		"==============================================\n",
    event_and_reason,
		p_version_info->GetVersionInfo());
}

void CAutoConnector::FailedToConnectBecauseNoWindowInList() {
	p_sharedmem->RememberTimeOfLastFailedAttemptToConnect();
	GoIntoPopupBlockingMode();
}

void CAutoConnector::FailedToConnectProbablyBecauseAllTablesAlreadyServed() {
	write_log(preferences.debug_autoconnector(), "[CAutoConnector] Attempt to connect did fail\n");
	p_sharedmem->RememberTimeOfLastFailedAttemptToConnect();
	GoIntoPopupBlockingMode();
}

void CAutoConnector::GoIntoPopupBlockingMode() {
	// We have a free instance that has nothing to do.
	// Care about potential popups here, once per auto-connector-heartbeat.
	write_log(preferences.debug_autoconnector(), "[CAutoConnector] Not connected. Going into popup-blocking mode.\n");
	if (p_sharedmem->AnyWindowAttached())	{
		// Only handle popups if at least one bot is connected to a table.
		// Especially stop popup-handling if the last table got closed
		// to allow "normal" human work again.
		assert(p_popup_handler != NULL);
		p_popup_handler->HandleAllWindows();
	} else {
    write_log(preferences.debug_autoconnector(), "[CAutoConnector] No table connected at all. No need for popup-blocking.\n");
  }
}

bool CAutoConnector::Connect(HWND targetHWnd) {
	int					line = 0, ret = 0;
	char				title[MAX_WINDOW_TITLE] = {0};
	int					SelectedItem = kUndefined;
	CString			current_path = "";
	BOOL				bFound = false;
	CFileFind   hFile;

  // Potential race-condition, as some objects
  // (especially GUI objects) get created by another thread.
  // We just skip connection if OH is not yet initialized.
  // http://www.maxinmontreal.com/forums/viewtopic.php?f=156&t=19706
  // 
  // We have to check and return very early, we must not do this
  // after locking the mutex, otherwiese we block other instances forever.
  // http://www.maxinmontreal.com/forums/viewtopic.php?f=110&t=19407&p=140417#p140417
  if (p_table_positioner == NULL) return false;
  if (p_autoplayer == NULL) return false;
  if (p_casino_interface == NULL) return false;
  if (p_engine_container == NULL) return false;
  if (p_flags_toolbar == NULL) return false;
  if (p_scraper == NULL) return false;
  if (p_sharedmem == NULL) return false;
  if (p_tablemap == NULL) return false;
  if (p_tablemap_loader == NULL) return false;
  if (p_table_state == NULL) return false;
  if (p_table_positioner == NULL) return false;
	write_log(preferences.debug_autoconnector(), "[CAutoConnector] Connect(..)\n");
  ASSERT(_autoconnector_mutex->m_hObject != NULL); 
	write_log(preferences.debug_autoconnector(), "[CAutoConnector] Locking autoconnector-mutex\n");
	if (!_autoconnector_mutex->Lock(500))	{
		write_log(preferences.debug_autoconnector(), "[CAutoConnector] Could not grab mutex; early exit\n");
		return false; 
	}
  // Clear global list for holding table candidates
	g_tlist.RemoveAll();
	write_log(preferences.debug_autoconnector(), "[CAutoConnector] Number of tablemaps loaded: %i\n",
    p_tablemap_loader->NumberOfTableMapsLoaded());
	for (int tablemap_index=0; tablemap_index<p_tablemap_loader->NumberOfTableMapsLoaded(); tablemap_index++) {
		write_log(preferences.debug_autoconnector(), "[CAutoConnector] Going to check TM nr. %d out of %d\n", 
			tablemap_index, p_tablemap_loader->NumberOfTableMapsLoaded());
		Check_TM_Against_All_Windows_Or_TargetHWND(tablemap_index, targetHWnd);
	}
	// Put global candidate table list in table select dialog variables
	int n_window_candidates = (int) g_tlist.GetSize();
  write_log(preferences.debug_autoconnector(), "[CAutoConnector] Number of table candidates: %i\n", 
    n_window_candidates);
	if (n_window_candidates == 0) {
		FailedToConnectBecauseNoWindowInList();
	}	else 	{
		SelectedItem = SelectTableMapAndWindowAutomatically();
		if (SelectedItem == kUndefined) {
			FailedToConnectProbablyBecauseAllTablesAlreadyServed();
		}	else {
			write_log(preferences.debug_autoconnector(), "[CAutoConnector] Window [%d] selected\n", g_tlist[SelectedItem].hwnd);
      // Load correct tablemap, and save hwnd/rect/numchairs of table that we are "attached" to
			set_attached_hwnd(g_tlist[SelectedItem].hwnd);
      CheckIfWindowMatchesMoreThanOneTablemap(attached_hwnd());
			assert(p_tablemap != NULL);
      CString tablemap_to_load = p_tablemap_loader->GetTablemapPathToLoad(g_tlist[SelectedItem].tablemap_index);
			write_log(preferences.debug_autoconnector(), "[CAutoConnector] Selected tablemap: %s\n", tablemap_to_load);
			p_tablemap->LoadTablemap(tablemap_to_load);
			write_log(preferences.debug_autoconnector(), "[CAutoConnector] Tablemap successfully loaded\n");
  		// Create bitmaps
			p_scraper->CreateBitmaps();
			write_log(preferences.debug_autoconnector(), "[CAutoConnector] Scraper-bitmaps created\n");
      // Clear scraper fields
			p_table_state->Reset();
      p_casino_interface->Reset();
			write_log(preferences.debug_autoconnector(), "[CAutoConnector] Table state cleared\n");
      // Reset symbols
			p_engine_container->UpdateOnConnection();
      write_log(preferences.debug_autoconnector(), "[CAutoConnector] UpdateOnConnection executed (during connection)\n");
			write_log(preferences.debug_autoconnector(), "[CAutoConnector] Going to continue with scraper output and scraper DLL\n");
      // Reset "ScraperOutput" dialog, if it is live
			if (m_ScraperOutputDlg) {
				m_ScraperOutputDlg->Reset();
			}
			p_flags_toolbar->ResetButtonsOnConnect();
      // The main GUI gets created by another thread.
      // This can be slowed down if there are popups (parse-errors).
      // Handle the race-condition
      WAIT_FOR_CONDITION(PMainframe() != NULL)
      assert(PMainframe() != NULL);
			// Reset display
			PMainframe()->ResetDisplay();
      // log OH title bar text and table reset
      WriteLogTableReset("NEW CONNECTION");
      p_table_positioner->ResizeToTargetSize();
			p_table_positioner->PositionMyWindow();
			p_autoplayer->EngageAutoPlayerUponConnectionIfNeeded();
		}
	}
	write_log(preferences.debug_autoconnector(), "[CAutoConnector] Unlocking autoconnector-mutex\n");
	_autoconnector_mutex->Unlock();
	return (SelectedItem != kUndefined);
}

void CAutoConnector::Disconnect(CString reason_for_disconnection) {
	write_log(preferences.debug_autoconnector(), "[CAutoConnector] Disconnect()\n");
  if (!IsConnected()) {
    // Be extra safe.
    // This stupid error happened, when OnTimer() only checked if the window 
    // still existed, but not if we were connected at all.
    // Then Diconnect() plus Connect() lead to freezing.
    write_log(k_always_log_errors, "[CAutoConnector] ERROR: Disconnect() called while not connected\n");
    return;
  }

  // First close scraper-output-dialog,
  // as an updating dialog without a connected table can crash.
  CDlgScraperOutput::DestroyWindowSafely();

  // Make sure autoplayer is off
  write_log(preferences.debug_autoconnector(), "[CAutoConnector] Stopping autoplayer\n");
  p_autoplayer->EngageAutoplayer(false);

	// Wait for mutex - "forever" if necessary, as we have to clean up.
	ASSERT(_autoconnector_mutex->m_hObject != NULL); 
	write_log(preferences.debug_autoconnector(), "[CAutoConnector] Locking autoconnector-mutex\n");
  _autoconnector_mutex->Lock(INFINITE); //!!!!!!! here freezing
  // I thread calling Connect() from heartbeat
  // 1 thread calling Disconnect from Ontimer()
	p_engine_container->UpdateOnDisconnection();
	// Clear "attached" info
	set_attached_hwnd(NULL);
	// Unattach OH.
	p_flags_toolbar->UnattachOHFromPokerWindow();
	p_flags_toolbar->ResetButtonsOnDisconnect();
	// Release mutex as soon as possible, after critical work is done
	write_log(preferences.debug_autoconnector(), "[CAutoConnector] Unlocking autoconnector-mutex\n");
	_autoconnector_mutex->Unlock();	

	// Delete bitmaps
	p_scraper->DeleteBitmaps();

	// Clear scraper fields
	p_table_state->Reset();
  p_casino_interface->Reset();

	// Reset symbols
	p_engine_container->UpdateOnConnection();

	write_log(preferences.debug_autoconnector(), "[CAutoConnector] UpdateOnConnection executed (disconnection)\n");
	write_log(preferences.debug_autoconnector(), "[CAutoConnector] Going to continue with window title\n");

	// Change window title
	p_openholdem_title->UpdateTitle();
	// Reset Display 
	PMainframe()->ResetDisplay();

	// Reset "ScraperOutput" dialog, if it is live
	if (m_ScraperOutputDlg)	{
		m_ScraperOutputDlg->Reset();
	}
  CString message;
  message.Format("DISCONNECTION -- %s", reason_for_disconnection);
	WriteLogTableReset(message);

	// Close OH, when table disappears and leaving enabled in preferences.//!!!!! can be removed due to OH-starter
	if (preferences.autoconnector_close_when_table_disappears()) {
		PostQuitMessage(0);
	}
	write_log(preferences.debug_autoconnector(), "[CAutoConnector] Disconnect done\n");
}

int CAutoConnector::SelectTableMapAndWindowAutomatically() {
	write_log(preferences.debug_autoconnector(), "[CAutoConnector] SelectTableMapAndWindowAutomatically(..)\n");
  int n_window_candidates = (int)g_tlist.GetSize();
	for (int i=0; i<n_window_candidates; ++i) {
		if (!p_sharedmem->PokerWindowAttached(g_tlist[i].hwnd))	{
			write_log(preferences.debug_autoconnector(), "[CAutoConnector] Chosen (table, TM)-pair in list: %d\n", i);
			return i;
		}
	}
	// No appropriate table found
	write_log(preferences.debug_autoconnector(), "[CAutoConnector] No appropriate table found.\n");
	return kUndefined;
}

double CAutoConnector::SecondsSinceLastFailedAttemptToConnect() {
	time_t last_failed_attempt_to_connect = p_sharedmem->GetTimeOfLastFailedAttemptToConnect(); 
	time_t CurrentTime;
	time(&CurrentTime);
	double _TimeSincelast_failed_attempt_to_connect = difftime(CurrentTime, last_failed_attempt_to_connect);
	write_log(preferences.debug_autoconnector(), "[CAutoConnector] TimeSincelast_failed_attempt_to_connect %f\n", _TimeSincelast_failed_attempt_to_connect);
	return _TimeSincelast_failed_attempt_to_connect;
}
