/*
 *      Copyright (C) 2005-2013 Team XBMC
 *      http://www.xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "system.h"
#include "GUIUserMessages.h"
#include "Application.h"
#include "GUIDialogSubtitles.h"
#include "addons/AddonManager.h"
#include "cores/IPlayer.h"
#include "filesystem/File.h"
#include "filesystem/PluginDirectory.h"
#include "filesystem/SpecialProtocol.h"
#include "guilib/GUIImage.h"
#include "guilib/GUIWindowManager.h"
#include "guilib/GUIListContainer.h"
#include "interfaces/legacy/Control.h"
#ifdef HAS_PYTHON
#include "interfaces/python/XBPython.h"
#endif
#include "settings/Settings.h"
#include "settings/GUISettings.h"
#include "utils/log.h"
#include "utils/URIUtils.h"
#include "utils/StringUtils.h"
#include "utils/LangCodeExpander.h"

using namespace ADDON;
using namespace XFILE;

#define CONTROL_NAMELABEL            100
#define CONTROL_NAMELOGO             110
#define CONTROL_SUBLIST              120
#define CONTROL_SUBSEXIST            130
#define CONTROL_SUBSTATUS            140
#define CONTROL_SERVICELIST          150

CGUIDialogSubtitles::CGUIDialogSubtitles(void)
    : CGUIDialog(WINDOW_DIALOG_SUBTITLES, "DialogSubtitles.xml")
{
  m_loadType  = KEEP_IN_MEMORY;
  m_subtitles = new CFileItemList;
  
}

CGUIDialogSubtitles::~CGUIDialogSubtitles(void)
{
  delete m_subtitles;
}


bool CGUIDialogSubtitles::OnMessage(CGUIMessage& message)
{
  if  (message.GetMessage() == GUI_MSG_CLICKED)
  {
    int iControl = message.GetSenderId();
    
    if (iControl == CONTROL_SUBLIST)
    {
      CGUIListContainer *sList = (CGUIListContainer *)GetControl(CONTROL_SUBLIST);
      if (sList)
      {
        Download(sList->GetSelectedItem());
      }
    }
    else if (iControl == CONTROL_SERVICELIST)
    {
      CGUIListContainer *pList = (CGUIListContainer *)GetControl(CONTROL_SERVICELIST);

      if (pList)
      {
        if (m_service != m_serviceItems[pList->GetSelectedItem()])
        {
          m_service = m_serviceItems[pList->GetSelectedItem()];
          Search();
          CLog::Log(LOGDEBUG, "New Service [%s] ", m_service->Name().c_str());
        }
      }
    }
    else
      return false;
    return true;
  }
  else if (message.GetMessage() == GUI_MSG_WINDOW_DEINIT)
  {
    
    g_windowManager.ShowOverlay(OVERLAY_STATE_SHOWN);
    CGUIDialog::OnMessage(message);
    m_subtitles->Clear();
    CGUIMessage message(GUI_MSG_LABEL_RESET, GetID(), CONTROL_SUBLIST);
    OnMessage(message);
    return true;
  }
  return CGUIDialog::OnMessage(message);
}

void CGUIDialogSubtitles::OnInitWindow()
{
  m_pausedOnRun = g_application.m_pPlayer->IsPaused();
  
  Run();
  g_windowManager.ShowOverlay(OVERLAY_STATE_HIDDEN);
  CGUIWindow::OnInitWindow();
}

void CGUIDialogSubtitles::Run()
{
  AddonPtr addon;
  AddonPtr defaultService;
  CGUIListItemPtr items(new CFileItemList());
  m_serviceItems.clear();
  ADDON::CAddonMgr::Get().GetAddons(ADDON_SUBTITLE_MODULE,m_serviceItems,true);
  
  for (VECADDONS::const_iterator addonIt = m_serviceItems.begin(); addonIt != m_serviceItems.end(); addonIt++)
  {
    CAddonMgr::Get().GetAddon((*addonIt)->ID(), addon, ADDON_SUBTITLE_MODULE);
    
    CFileItemPtr item(new CFileItem());
    item->SetLabel(addon->Name());
    ((CFileItemList*)items.get())->Add(item);
    
    CLog::Log(LOGDEBUG, "Subtitle Service [%s] Found", addon->Name().c_str());
  }
    
  CGUIMessage msg(GUI_MSG_LABEL_BIND, GetID(), CONTROL_SERVICELIST, 0, 0, items);
  msg.SetPointer(items.get());
  g_windowManager.SendThreadMessage(msg, GetID());

  if (defaultService)
    m_service = defaultService;    
  else
    m_service = m_serviceItems[0];
 
  Search();
}

void CGUIDialogSubtitles::Search()
{
  if (g_guiSettings.GetBool("subtitles.pauseonsearch"))
    g_application.m_pPlayer->Pause();
  
  ClearListItems();
  SetNameLogo();
  SetNameLabel();
  CheckExistingSubs();
  
  CStdString s;
  s.Format("plugin://%s/?action=search"
           "&languages=%s",m_service->ID(),
                           "English,SerbianLatin");
  CDirectory::GetDirectory(s, *m_subtitles);

  
  CGUIMessage message(GUI_MSG_LABEL_BIND, GetID(), CONTROL_SUBLIST, 0, 0, m_subtitles);
  OnMessage(message);

}

void CGUIDialogSubtitles::Download(const int &iPos)
{
  CStdString s;
  CFileItemList vecItems;
  s.Format("plugin://%s/?action=download"
             "&properties=%s", m_service->ID(),
                               (*m_subtitles)[iPos]->GetProperty("Download").asString());
  CDirectory::GetDirectory(s, vecItems);
  
  if (g_application.CurrentFileItem().IsStack())
  {
    for (int i = 0; i < vecItems.Size(); i++)
    {
//      check for all stack items and match to given subs, item [0] == CD1, item [1] == CD2 
//      CLog::Log(LOGDEBUG, "Stack Subs [%s} Found", vecItems[i]->GetLabel().c_str());
    }
  }
  else if (StringUtils::StartsWith(g_application.CurrentFile(), "http://"))
  {
    CStdString strSubName;
    CStdString strFileName = URIUtils::GetFileName(g_application.CurrentFile());
    CStdString strSubExt = URIUtils::GetExtension(vecItems[0]->GetLabel());
    strSubName.Format("TemporarySubs%s",strSubExt);
    CStdString srcSubPath = URIUtils::AddFileToFolder("special://temp", strSubName);
    CFile::Cache(vecItems[0]->GetLabel(), srcSubPath);
    setSubtitles(srcSubPath);
  }
  else
  {
    CStdString srcSubPath;
    CStdString strSubName;
    CStdString strSubLang;
    CStdString strFileName;
    CStdString strFileParentPath;
    CStdString srcDestPath;
    g_LangCodeExpander.ConvertToThreeCharCode(strSubLang,(*m_subtitles)[iPos]->GetLabel());
    CStdString strFilePath = g_application.CurrentFile();
    URIUtils::Split(strFilePath,strFileParentPath,strFileName);
    CStdString strFileExt = URIUtils::GetExtension(vecItems[0]->GetLabel());
    
    URIUtils::RemoveExtension(strFileName);
    
    strSubName.Format("%s.%s%s",strFileName,strSubLang,strFileExt);
    
    if (g_guiSettings.GetBool("subtitles.savetomoviefolder"))
      srcDestPath = strFileParentPath;
    else
      if (CSpecialProtocol::TranslatePath("special://subtitles").IsEmpty())
        srcDestPath = "special://temp";
      else
        srcDestPath = "special://subtitles";
    
    srcSubPath = URIUtils::AddFileToFolder(srcDestPath, strSubName);
    
    CFile::Cache(vecItems[0]->GetLabel(), srcSubPath);
    setSubtitles(srcSubPath);
  }
  
  if (g_application.m_pPlayer->IsPaused() && !m_pausedOnRun)
      g_application.m_pPlayer->Pause();
  
  // Close the window
  Close();
  
}

void CGUIDialogSubtitles::ClearListItems()
{
  CGUIMessage msg(GUI_MSG_LABEL_RESET, GetID(), CONTROL_SUBLIST);
  g_windowManager.SendMessage(msg);
  
  m_subtitles->Clear();
}

void CGUIDialogSubtitles::SetNameLabel()
{
  SET_CONTROL_LABEL(CONTROL_NAMELABEL,m_service->Name());
}

void CGUIDialogSubtitles::SetNameLogo()
{
  CGUIControl* pGUIControl = (CGUIControl*)GetControl(CONTROL_NAMELOGO);
  ((CGUIImage*)pGUIControl)->SetFileName(m_service->Icon());
}

void CGUIDialogSubtitles::CheckExistingSubs()
{
  if (g_application.m_pPlayer->GetSubtitleCount() == 0)
    SET_CONTROL_HIDDEN(CONTROL_SUBSEXIST);
  else
    SET_CONTROL_VISIBLE(CONTROL_SUBSEXIST);
}


void CGUIDialogSubtitles::setSubtitles(const char* cLine)
{
  if (g_application.m_pPlayer)
  {
    int nStream = g_application.m_pPlayer->AddSubtitle(cLine);
    if(nStream >= 0)
    {
      g_application.m_pPlayer->SetSubtitle(nStream);
      g_application.m_pPlayer->SetSubtitleVisible(true);
      g_settings.m_currentVideoSettings.m_SubtitleDelay = 0.0f;
      g_application.m_pPlayer->SetSubTitleDelay(g_settings.m_currentVideoSettings.m_SubtitleDelay);
    }
  }
}
