//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2019 SuperTuxKart-Team
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 3
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

#ifdef MOBILE_STK

#include "states_screens/dialogs/download_assets.hpp"

#include "config/user_config.hpp"
#include "states_screens/dialogs/message_dialog.hpp"
#include "io/file_manager.hpp"
#include "online/http_request.hpp"
#include "states_screens/state_manager.hpp"
#include "utils/extract_mobile_assets.hpp"
#include "utils/download_assets_size.hpp"
#include "utils/string_utils.hpp"
#include "utils/translation.hpp"

using namespace GUIEngine;
using namespace Online;
using namespace irr::gui;

// ----------------------------------------------------------------------------
class DownloadAssetsRequest : public HTTPRequest
{
private:
    bool m_extraction_error;
    virtual void afterOperation()
    {
        Online::HTTPRequest::afterOperation();
        if (isCancelled())
            return;
        m_extraction_error =
            !ExtractMobileAssets::extract(getFileName(),
            file_manager->getSTKAssetsDownloadDir());
    }
public:
    DownloadAssetsRequest()
    : HTTPRequest("stk-assets.zip", /*manage mem*/false, /*priority*/5)
    {
        m_extraction_error = true;
        std::string download_url = stk_config->m_assets_download_url;
        download_url += STK_VERSION;
        download_url += "/stk-assets.zip";
        setURL(download_url);
        setDownloadAssetsRequest(true);
    }
    ~DownloadAssetsRequest()
    {
        if (isCancelled())
        {
            const std::string& zip = getFileName();
            const std::string zip_part = zip + ".part";
            if (file_manager->fileExists(zip))
                file_manager->removeFile(zip);
            if (file_manager->fileExists(zip_part))
                file_manager->removeFile(zip_part);
            file_manager->removeDirectory(
                file_manager->getSTKAssetsDownloadDir());
        }
    }
    bool hadError() const { return hadDownloadError() || m_extraction_error; }
};   // DownloadAssetsRequest

// ----------------------------------------------------------------------------
/** Creates a modal dialog with given percentage of screen width and height
*/
DownloadAssets::DownloadAssets()
              : ModalDialog(0.8f, 0.8f)
{
    m_download_request = NULL;

    loadFromFile("addons_loading.stkgui");
    m_install_button   = getWidget<IconButtonWidget> ("install" );
    m_progress         = getWidget<ProgressBarWidget>("progress");

    RibbonWidget* actions = getWidget<RibbonWidget>("actions");
    actions->setFocusForPlayer(PLAYER_ID_GAME_MASTER);
    actions->select("back", PLAYER_ID_GAME_MASTER);

    if(m_progress)
        m_progress->setVisible(false);

    IconButtonWidget* icon = getWidget<IconButtonWidget>("icon");
    icon->setImage(file_manager->getAsset(FileManager::GUI_ICON, "logo.png"),
        IconButtonWidget::ICON_PATH_TYPE_ABSOLUTE);

    core::stringw unit = "";
    unsigned n = getDownloadAssetsSize();
    float f = ((int)(n/1024.0f/1024.0f*10.0f+0.5f))/10.0f;
    char s[32];
    sprintf(s, "%.1f", f);
    unit = _("%s MB", s);
    // I18N: File size of game assets or addons downloading
    core::stringw size = _("Size: %s", unit.c_str());
    getWidget<LabelWidget>("size")->setText(size, false);

    // I18N: In download assets dialog
    core::stringw msg = _("SuperTuxKart will download full assets "
        "(including all tracks, high quality textures and music) for better "
        "gaming experience, this will use your mobile data if you don't have "
        "a wifi connection.");
    getWidget<BubbleWidget>("description")->setText(msg);
}   // DownloadAssets

// ----------------------------------------------------------------------------
void DownloadAssets::beforeAddingWidgets()
{
    getWidget("uninstall")->setVisible(false);
}   // beforeAddingWidgets

// ----------------------------------------------------------------------------
void DownloadAssets::init()
{
    getWidget("rating")->setVisible(false);
}   // init

// ----------------------------------------------------------------------------
bool DownloadAssets::onEscapePressed()
{
    stopDownload();
    ModalDialog::dismiss();
    return true;
}   // onEscapePressed

// ----------------------------------------------------------------------------
GUIEngine::EventPropagation DownloadAssets::processEvent(const std::string& event_source)
{
    GUIEngine::RibbonWidget* actions_ribbon =
            getWidget<GUIEngine::RibbonWidget>("actions");

    if (event_source == "actions")
    {
        const std::string& selection =
            actions_ribbon->getSelectionIDString(PLAYER_ID_GAME_MASTER);
        if (selection == "back")
        {
            stopDownload();
            dismiss();
            return GUIEngine::EVENT_BLOCK;
        }
        else if (selection == "install")
        {
            m_progress->setValue(0);
            m_progress->setVisible(true);

            actions_ribbon->setVisible(false);

            startDownload();
            return GUIEngine::EVENT_BLOCK;
        }
    }
    return GUIEngine::EVENT_LET;
}   // processEvent

// ----------------------------------------------------------------------------
void DownloadAssets::onUpdate(float delta)
{
    if (m_download_request)
    {
        float progress = m_download_request->getProgress();
        // Last 1% for unzipping
        m_progress->setValue(progress * 99.0f);
        if (progress < 0)
        {
            // Avoid displaying '-100%' in case of an error.
            m_progress->setVisible(false);
            dismiss();
            new MessageDialog(_("Sorry, downloading the add-on failed"));
            return;
        }
        else if (m_download_request->isDone())
        {
            // No sense to update state text, since it all
            // happens before the GUI is refrehsed.
            doInstall();
            return;
        }
    }   // if (m_progress->isVisible())
}   // onUpdate

// ----------------------------------------------------------------------------
/** This function is called when the user click on 'Install', 'Uninstall', or
 *  'Update'.
 **/
void DownloadAssets::startDownload()
{
    m_download_request = new DownloadAssetsRequest();
    m_download_request->queue();
}   // startDownload

// ----------------------------------------------------------------------------
/** This function is called when the user click on 'Back', 'Cancel' or press
 *  escape.
 **/
void DownloadAssets::stopDownload()
{
    // Cancel a download only if we are installing/upgrading one
    // (and not uninstalling an installed one):
    if (m_download_request)
    {
        // In case of a cancel we can't free the memory, since the
        // request manager thread is potentially working on this request. So
        // in order to avoid a memory leak, we let the request manager
        // free the data. This is thread safe since freeing the data is done
        // when the request manager handles the result queue - and this is
        // done by the main thread (i.e. this thread).
        m_download_request->setManageMemory(true);
        m_download_request->cancel();
        m_download_request = NULL;
    }
}   // startDownload


// ----------------------------------------------------------------------------
/** Called when the asynchronous download of the addon finished.
 */
void DownloadAssets::doInstall()
{
    core::stringw msg;
    if (m_download_request->hadError())
    {
        // Reset the download buttons so user can redownload if needed
        // I18N: Shown when there is download error for assets download
        // in the first run
        msg = _("Failed to download assets, check your storage space or internet connection and try again later.");
    }
    delete m_download_request;
    m_download_request = NULL;
    if (!msg.empty())
    {
        getWidget<BubbleWidget>("description")->setText(msg);
    }

    if (!msg.empty())
    {
        m_progress->setVisible(false);

        RibbonWidget* r = getWidget<RibbonWidget>("actions");
        r->setVisible(true);

        m_install_button->setLabel(_("Try again"));
    }
    else
    {
        dismiss();
        ExtractMobileAssets::reinit();
    }
}   // doInstall

#endif
