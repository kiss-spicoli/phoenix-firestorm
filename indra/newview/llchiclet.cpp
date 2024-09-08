/**
 * @file llchiclet.cpp
 * @brief LLChiclet class implementation
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h" // must be first include
#include "llchiclet.h"

#include "llchicletbar.h"
// <FS:Ansariel> [FS communication UI]
//#include "llfloaterimsession.h"
//#include "llfloaterimcontainer.h"
// </FS:Ansariel> [FS communication UI]
#include "llfloaterreg.h"
#include "lllocalcliprect.h"
#include "llscriptfloater.h"
#include "llsingleton.h"
#include "llsyswellwindow.h"
#include "llfloaternotificationstabbed.h"
#include "llviewermenu.h"
// [SL:KB] - Patch: UI-Notifications | Checked: 2013-05-09 (Catznip-3.5)
#include "llchannelmanager.h"
// [/SL:KB]
// <FS:PP> Unread IMs counter in window title
#include "llviewerwindow.h"
// </FS:PP>

// Firestorm includes
#include "fsfloaterim.h"
#include "llagent.h"
#include "llavataractions.h"
#include "llgroupactions.h"
#include "llgroupmgr.h"
#include "llmenugl.h"
#include "lloutputmonitorctrl.h"
#include "llspeakers.h"
#include "lltransientfloatermgr.h"
#include "llvoiceclient.h"

// <FS:PP> Unread IMs counter in window title
extern std::string gWindowTitle;
// </FS:PP>

static LLDefaultChildRegistry::Register<LLChicletPanel> t1("chiclet_panel");
static LLDefaultChildRegistry::Register<LLNotificationChiclet> t2("chiclet_notification");
static LLDefaultChildRegistry::Register<LLScriptChiclet> t6("chiclet_script");
static LLDefaultChildRegistry::Register<LLInvOfferChiclet> t7("chiclet_offer");

// <FS:Ansariel> [FS communication UI]
static LLDefaultChildRegistry::Register<LLIMWellChiclet> t2_0("fs_chiclet_im_well");
static LLDefaultChildRegistry::Register<LLIMP2PChiclet> t3("fs_chiclet_im_p2p");
static LLDefaultChildRegistry::Register<LLIMGroupChiclet> t4("fs_chiclet_im_group");
static LLDefaultChildRegistry::Register<LLAdHocChiclet> t5("fs_chiclet_im_adhoc");
// </FS:Ansariel> [FS communication UI]

boost::signals2::signal<LLChiclet* (const LLUUID&),
        LLIMChiclet::CollectChicletCombiner<std::list<LLChiclet*> > >
        LLIMChiclet::sFindChicletsSignal;
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

LLSysWellChiclet::Params::Params()
    : button("button")
    , unread_notifications("unread_notifications")
    , max_displayed_count("max_displayed_count", 99)
{
    button.name = "button";
    button.tab_stop = false;
    button.label = LLStringUtil::null;
}

LLSysWellChiclet::LLSysWellChiclet(const Params& p)
    : LLChiclet(p)
    , mButton(NULL)
    , mCounter(0)
    , mMaxDisplayedCount(p.max_displayed_count)
    , mIsNewMessagesState(false)
    , mFlashToLitTimer(NULL)
{
    LLButton::Params button_params = p.button;
    mButton = LLUICtrlFactory::create<LLButton>(button_params);
    addChild(mButton);

    mFlashToLitTimer = new LLFlashTimer(boost::bind(&LLSysWellChiclet::changeLitState, this, _1));
}

LLSysWellChiclet::~LLSysWellChiclet()
{
    mFlashToLitTimer->unset();
    LLContextMenu* menu = static_cast<LLContextMenu*>(mContextMenuHandle.get());
    if (menu)
    {
        menu->die();
        mContextMenuHandle.markDead();
    }
}

void LLSysWellChiclet::setCounter(S32 counter)
{
    // do nothing if the same counter is coming. EXT-3678.
    if (counter == mCounter) return;

    // note same code in LLChicletNotificationCounterCtrl::setCounter(S32 counter)
    std::string s_count;
    if(counter != 0)
    {
        static std::string more_messages_exist("+");
        std::string more_messages(counter > mMaxDisplayedCount ? more_messages_exist : "");
        s_count = llformat("%d%s"
            , llmin(counter, mMaxDisplayedCount)
            , more_messages.c_str()
            );
    }

    mButton->setLabel(s_count);

    mCounter = counter;
}

boost::signals2::connection LLSysWellChiclet::setClickCallback(
    const commit_callback_t& cb)
{
    return mButton->setClickedCallback(cb);
}

void LLSysWellChiclet::setToggleState(bool toggled) {
    mButton->setToggleState(toggled);
}

void LLSysWellChiclet::changeLitState(bool blink)
{
    setNewMessagesState(!mIsNewMessagesState);
}

void LLSysWellChiclet::setNewMessagesState(bool new_messages)
{
    /*
    Emulate 4 states of button by background images, see detains in EXT-3147
    xml attribute           Description
    image_unselected        "Unlit" - there are no new messages
    image_selected          "Unlit" + "Selected" - there are no new messages and the Well is open
    image_pressed           "Lit" - there are new messages
    image_pressed_selected  "Lit" + "Selected" - there are new messages and the Well is open
    */
    mButton->setForcePressedState(new_messages);

    mIsNewMessagesState = new_messages;
}

void LLSysWellChiclet::updateWidget(bool is_window_empty)
{
    mButton->setEnabled(!is_window_empty);

    if (LLChicletBar::instanceExists())
    {
        LLChicletBar::getInstance()->showWellButton(getName(), !is_window_empty);
    }
}
// virtual
bool LLSysWellChiclet::handleRightMouseDown(S32 x, S32 y, MASK mask)
{
    LLContextMenu* menu_avatar = mContextMenuHandle.get();
    if(!menu_avatar)
    {
        createMenu();
        menu_avatar = mContextMenuHandle.get();
    }
    if (menu_avatar)
    {
        menu_avatar->show(x, y);
        LLMenuGL::showPopup(this, menu_avatar, x, y);
    }
    return true;
}

// <FS:Ansariel> [FS communication UI]
/************************************************************************/
/*               LLIMWellChiclet implementation                         */
/************************************************************************/
LLIMWellChiclet::LLIMWellChiclet(const Params& p)
: LLSysWellChiclet(p)
{
    LLIMModel::instance().addNewMsgCallback(boost::bind(&LLIMWellChiclet::messageCountChanged, this, _1));
    LLIMModel::instance().addNoUnreadMsgsCallback(boost::bind(&LLIMWellChiclet::messageCountChanged, this, _1));

    LLIMMgr::getInstance()->addSessionObserver(this);

    LLIMWellWindow::getInstance()->setSysWellChiclet(this);

    gSavedSettings.getControl("FSShowMessageCountInWindowTitle")->getSignal()->connect(boost::bind(&LLIMWellChiclet::updateApplicationWindowTitle, this));
}

LLIMWellChiclet::~LLIMWellChiclet()
{
    LLContextMenu* menu = static_cast<LLContextMenu*>(mContextMenuHandle.get());
    if (menu)
    {
        menu->die();
        mContextMenuHandle.markDead();
    }

    LLIMWellWindow* im_well_window = LLIMWellWindow::findInstance();
    if (im_well_window)
    {
        im_well_window->setSysWellChiclet(NULL);
    }

    LLIMMgr::getInstance()->removeSessionObserver(this);
}

void LLIMWellChiclet::onMenuItemClicked(const LLSD& user_data)
{
    std::string action = user_data.asString();
    if("close all" == action)
    {
        LLIMWellWindow::getInstance()->closeAll();
    }
}

bool LLIMWellChiclet::enableMenuItem(const LLSD& user_data)
{
    std::string item = user_data.asString();
    if (item == "can close all")
    {
        return !LLIMWellWindow::getInstance()->isWindowEmpty();
    }
    return true;
}

void LLIMWellChiclet::createMenu()
{
    if(mContextMenuHandle.get())
    {
        LL_WARNS() << "Menu already exists" << LL_ENDL;
        return;
    }

    LLUICtrl::CommitCallbackRegistry::ScopedRegistrar registrar;
    registrar.add("IMWellChicletMenu.Action",
        boost::bind(&LLIMWellChiclet::onMenuItemClicked, this, _2));

    LLUICtrl::EnableCallbackRegistry::ScopedRegistrar enable_registrar;
    enable_registrar.add("IMWellChicletMenu.EnableItem",
        boost::bind(&LLIMWellChiclet::enableMenuItem, this, _2));

    LLContextMenu* menu = LLUICtrlFactory::getInstance()->createFromFile<LLContextMenu>
        ("menu_fs_im_well_button.xml",
         LLMenuGL::sMenuContainer,
         LLViewerMenuHolderGL::child_registry_t::instance());
    if (menu)
    {
        mContextMenuHandle = menu->getHandle();
    }
}

void LLIMWellChiclet::messageCountChanged(const LLSD& session_data)
{
    // The singleton class LLChicletBar instance might be already deleted
    // so don't create a new one.
    if (!LLChicletBar::instanceExists())
    {
        return;
    }

    const LLUUID& session_id = session_data["session_id"];
    const S32 counter = LLChicletBar::getInstance()->getTotalUnreadIMCount();
    const bool im_not_visible = !LLFloaterReg::instanceVisible("fs_im_container")
        && !LLFloaterReg::instanceVisible("fs_impanel", session_id);

    setNewMessagesState(counter > mCounter  && im_not_visible);

    // we have to flash to 'Lit' state each time new unread message is coming.
    if (counter > mCounter && im_not_visible)
    {
        mFlashToLitTimer->startFlashing();
    }
    else if (counter == 0)
    {
        // if notification is resolved while well is flashing it can leave in the 'Lit' state
        // when flashing finishes itself. Let break flashing here.
        mFlashToLitTimer->stopFlashing();
    }

    setCounter(counter);

    updateApplicationWindowTitle();
}

void LLIMWellChiclet::updateApplicationWindowTitle()
{
    std::string window_title = gWindowTitle;

    if (gSavedSettings.getBOOL("FSShowMessageCountInWindowTitle") && mCounter > 0)
    {
        window_title = "(" + mButton->getLabelUnselected() + ") " + gWindowTitle;
    }

    LLStringUtil::truncate(window_title, 255);
    gViewerWindow->getWindow()->setTitle(window_title);
}
// </FS:Ansariel> [FS communication UI]

/************************************************************************/
/*               LLNotificationChiclet implementation                   */
/************************************************************************/
LLNotificationChiclet::LLNotificationChiclet(const Params& p)
:   LLSysWellChiclet(p),
    mUreadSystemNotifications(0)
{
    mNotificationChannel.reset(new ChicletNotificationChannel(this));
    // ensure that notification well window exists, to synchronously
    // handle toast add/delete events.
    // <FS:Ansariel> Optional legacy notification well
    //LLFloaterNotificationsTabbed::getInstance()->setSysWellChiclet(this);
    if (!gSavedSettings.getBOOL("FSInternalLegacyNotificationWell"))
    {
        LLFloaterNotificationsTabbed::getInstance()->setSysWellChiclet(this);
    }
    else
    {
        LLNotificationWellWindow::getInstance()->setSysWellChiclet(this);
    }
}

LLNotificationChiclet::~LLNotificationChiclet()
{
    mNotificationChannel.reset();
}

void LLNotificationChiclet::onMenuItemClicked(const LLSD& user_data)
{
    std::string action = user_data.asString();
    if("close all" == action)
    {
        // <FS:Ansariel> Optional legacy notification well
        //LLFloaterNotificationsTabbed::getInstance()->closeAll();
        if (!gSavedSettings.getBOOL("FSInternalLegacyNotificationWell"))
        {
            LLFloaterNotificationsTabbed::getInstance()->closeAll();
        }
        else
        {
            LLNotificationWellWindow::getInstance()->closeAll();
        }
        // <FS:Ansariel> [FS communication UI] - We have our own IM well button again
        //LLIMWellWindow::getInstance()->closeAll();
    }
}

bool LLNotificationChiclet::enableMenuItem(const LLSD& user_data)
{
    std::string item = user_data.asString();
    if (item == "can close all")
    {
        return mUreadSystemNotifications != 0;
    }
    return true;
}

void LLNotificationChiclet::createMenu()
{
    if(mContextMenuHandle.get())
    {
        LL_WARNS() << "Menu already exists" << LL_ENDL;
        return;
    }

    LLUICtrl::CommitCallbackRegistry::ScopedRegistrar registrar;
    registrar.add("NotificationWellChicletMenu.Action",
        boost::bind(&LLNotificationChiclet::onMenuItemClicked, this, _2));

    LLUICtrl::EnableCallbackRegistry::ScopedRegistrar enable_registrar;
    enable_registrar.add("NotificationWellChicletMenu.EnableItem",
        boost::bind(&LLNotificationChiclet::enableMenuItem, this, _2));

    llassert(LLMenuGL::sMenuContainer != NULL);
    LLContextMenu* menu = LLUICtrlFactory::getInstance()->createFromFile<LLContextMenu>
        ("menu_notification_well_button.xml",
         LLMenuGL::sMenuContainer,
         LLViewerMenuHolderGL::child_registry_t::instance());
    if (menu)
    {
        mContextMenuHandle = menu->getHandle();
    }
}

/*virtual*/
void LLNotificationChiclet::setCounter(S32 counter)
{
    LLSysWellChiclet::setCounter(counter);
    updateWidget(getCounter() == 0);

}

bool LLNotificationChiclet::ChicletNotificationChannel::filterNotification( LLNotificationPtr notification )
{
    bool displayNotification;
    if (   (notification->getName() == "ScriptDialog") // special case for scripts
        // if there is no toast window for the notification, filter it
        //|| (!LLNotificationWellWindow::getInstance()->findItemByID(notification->getID()))
        // <FS:Ansariel> Optional legacy notification well
        //|| (!LLFloaterNotificationsTabbed::getInstance()->findItemByID(notification->getID(), notification->getName()))
        || ((!gSavedSettings.getBOOL("FSInternalLegacyNotificationWell") && !LLFloaterNotificationsTabbed::getInstance()->findItemByID(notification->getID(), notification->getName()))
        || (gSavedSettings.getBOOL("FSInternalLegacyNotificationWell") && !LLNotificationWellWindow::getInstance()->findItemByID(notification->getID())))
        // </FS:Ansariel>
        )
    {
        displayNotification = false;
    }
    else if( !(notification->canLogToIM() && notification->hasFormElements())
            && (!notification->getPayload().has("give_inventory_notification")
                || notification->getPayload()["give_inventory_notification"]))
    {
        displayNotification = true;
    }
// [SL:KB] - Patch: UI-Notifications | Checked: 2013-05-09 (Catznip-3.5)
    else if ("offer" == notification->getType())
    {
        // Assume that any offer notification with "getCanBeStored() == true" is the result of RLVa routing it to the notifcation syswell
        /*const*/ LLNotificationsUI::LLScreenChannel* pChannel = LLNotificationsUI::LLChannelManager::instance().getNotificationScreenChannel();
        /*const*/ LLNotificationsUI::LLToast* pToast = (pChannel) ? pChannel->getToastByNotificationID(notification->getID()) : NULL;
        displayNotification = (pToast) && (pToast->getCanBeStored());
    }
// [/SL:KB]
    else
    {
        displayNotification = false;
    }
    return displayNotification;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

LLChiclet::Params::Params()
 : show_counter("show_counter", true)
 , enable_counter("enable_counter", false)
{
}

LLChiclet::LLChiclet(const Params& p)
: LLUICtrl(p)
, mSessionId(LLUUID::null)
, mShowCounter(p.show_counter)
{
}

boost::signals2::connection LLChiclet::setLeftButtonClickCallback(
    const commit_callback_t& cb)
{
    return setCommitCallback(cb);
}

bool LLChiclet::handleMouseDown(S32 x, S32 y, MASK mask)
{
    onCommit();
    childrenHandleMouseDown(x,y,mask);
    return true;
}

boost::signals2::connection LLChiclet::setChicletSizeChangedCallback(
    const chiclet_size_changed_callback_t& cb)
{
    return mChicletSizeChangedSignal.connect(cb);
}

void LLChiclet::onChicletSizeChanged()
{
    mChicletSizeChangedSignal(this, getValue());
}

LLSD LLChiclet::getValue() const
{
    return LLSD(getSessionId());
}

void LLChiclet::setValue(const LLSD& value)
{
    if(value.isUUID())
    {
        setSessionId(value.asUUID());
    }
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

LLIMChiclet::LLIMChiclet(const LLIMChiclet::Params& p)
: LLChiclet(p)
, mShowSpeaker(false)
, mDefaultWidth(p.rect().getWidth())
, mNewMessagesIcon(NULL)
// <FS:Ansariel> [FS communication UI]
, mSpeakerCtrl(NULL)
, mCounterCtrl(NULL)
// </FS:Ansariel> [FS communication UI]
, mChicletButton(NULL)
{
    // <FS:Ansariel> [FS communication UI]
    enableCounterControl(p.enable_counter);
}

LLIMChiclet::~LLIMChiclet()
{
    auto menu = mPopupMenuHandle.get();
    if (menu)
    {
        menu->die();
        mPopupMenuHandle.markDead();
    }
}

/* virtual*/
bool LLIMChiclet::postBuild()
{
    mChicletButton = getChild<LLButton>("chiclet_button");
    mChicletButton->setCommitCallback(boost::bind(&LLIMChiclet::onMouseDown, this));
    mChicletButton->setDoubleClickCallback(boost::bind(&LLIMChiclet::onMouseDown, this));
    return true;
}

void LLIMChiclet::enableCounterControl(bool enable)
{
    mCounterEnabled = enable;
    if(!enable)
    {
        LLChiclet::setShowCounter(false);
    }
}

void LLIMChiclet::setRequiredWidth()
{
    S32 required_width = mDefaultWidth;

    // <FS:Ansariel> [FS communication UI]
    if (getShowCounter())
    {
        required_width += mCounterCtrl->getRect().getWidth();
    }
    if (getShowSpeaker())
    {
        required_width += mSpeakerCtrl->getRect().getWidth();
    }
    // </FS:Ansariel> [FS communication UI]

    reshape(required_width, getRect().getHeight());
    onChicletSizeChanged();
}

void LLIMChiclet::setShowNewMessagesIcon(bool show)
{
    if(mNewMessagesIcon)
    {
        mNewMessagesIcon->setVisible(show);
    }
    setRequiredWidth();
}

bool LLIMChiclet::getShowNewMessagesIcon()
{
    return mNewMessagesIcon->getVisible();
}

void LLIMChiclet::onMouseDown()
{
    // <FS:Ansariel> [FS communication UI]
    //LLFloaterIMSession::toggle(getSessionId());
    FSFloaterIM::toggle(getSessionId());
    setCounter(0);
    // </FS:Ansariel> [FS communication UI]
}

void LLIMChiclet::setToggleState(bool toggle)
{
    mChicletButton->setToggleState(toggle);
}

// <FS:Ansariel> [FS communication UI]
void LLIMChiclet::setShowSpeaker(bool show)
{
    bool needs_resize = getShowSpeaker() != show;
    if(needs_resize)
    {
        mShowSpeaker = show;
    }

    toggleSpeakerControl();
}

void LLIMChiclet::setShowCounter(bool show)
{
    if(!mCounterEnabled)
    {
        return;
    }

    bool needs_resize = getShowCounter() != show;
    if(needs_resize)
    {
        LLChiclet::setShowCounter(show);
        toggleCounterControl();
    }
}

void LLIMChiclet::initSpeakerControl()
{
    // virtual
}

void LLIMChiclet::draw()
{
    LLUICtrl::draw();
}

void LLIMChiclet::toggleSpeakerControl()
{
    if(getShowSpeaker())
    {
        // move speaker to the right of chiclet icon
        LLRect speaker_rc = mSpeakerCtrl->getRect();
        speaker_rc.setLeftTopAndSize(mDefaultWidth, speaker_rc.mTop, speaker_rc.getWidth(), speaker_rc.getHeight());
        mSpeakerCtrl->setRect(speaker_rc);

        if(getShowCounter())
        {
            // move speaker to the right of counter
            mSpeakerCtrl->translate(mCounterCtrl->getRect().getWidth(), 0);
        }

        initSpeakerControl();
    }
    else
    {
        mSpeakerCtrl->setSpeakerId(LLUUID::null);
    }

    setRequiredWidth();
    mSpeakerCtrl->setVisible(getShowSpeaker());
}

void LLIMChiclet::setCounter(S32 counter)
{
    if (mCounterCtrl->getCounter() == counter)
    {
        return;
    }

    mCounterCtrl->setCounter(counter);
    setShowCounter(counter);
    setShowNewMessagesIcon(counter);
}

void LLIMChiclet::toggleCounterControl()
{
    setRequiredWidth();
    mCounterCtrl->setVisible(getShowCounter());
}

// static
LLIMChiclet::EType LLIMChiclet::getIMSessionType(const LLUUID& session_id)
{
    EType               type    = TYPE_UNKNOWN;

    if(session_id.isNull())
        return type;

    EInstantMessage im_type = LLIMModel::getInstance()->getType(session_id);
    if (IM_COUNT == im_type)
    {
        llassert_always(0 && "IM session not found"); // should never happen
        return type;
    }

    switch(im_type)
    {
    case IM_NOTHING_SPECIAL:
    case IM_SESSION_P2P_INVITE:
        type = TYPE_IM;
        break;
    case IM_SESSION_GROUP_START:
    case IM_SESSION_INVITE:
        if (gAgent.isInGroup(session_id))
        {
            type = TYPE_GROUP;
        }
        else
        {
            type = TYPE_AD_HOC;
        }
        break;
    case IM_SESSION_CONFERENCE_START:
        type = TYPE_AD_HOC;
    default:
        break;
    }

    return type;
}
// </FS:Ansariel> [FS communication UI]

bool LLIMChiclet::handleRightMouseDown(S32 x, S32 y, MASK mask)
{
    auto menu = static_cast<LLMenuGL*>(mPopupMenuHandle.get());
    if(!menu)
    {
        createPopupMenu();
        menu = static_cast<LLMenuGL*>(mPopupMenuHandle.get());
    }

    if (menu)
    {
        updateMenuItems();
        menu->arrangeAndClear();
        LLMenuGL::showPopup(this, menu, x, y);
    }

    return true;
}

void LLIMChiclet::hidePopupMenu()
{
    auto menu = mPopupMenuHandle.get();
    if (menu)
    {
        menu->setVisible(false);
    }
}

bool LLIMChiclet::canCreateMenu()
{
    if(mPopupMenuHandle.get())
    {
        LL_WARNS() << "Menu already exists" << LL_ENDL;
        return false;
    }
    if(getSessionId().isNull())
    {
        return false;
    }
    return true;
}

// <FS:Ansariel> [FS communication UI]
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

LLIMP2PChiclet::Params::Params()
: avatar_icon("avatar_icon")
, chiclet_button("chiclet_button")
, unread_notifications("unread_notifications")
, speaker("speaker")
, new_message_icon("new_message_icon")
, show_speaker("show_speaker")
{
}

LLIMP2PChiclet::LLIMP2PChiclet(const Params& p)
: LLIMChiclet(p)
, mChicletIconCtrl(NULL)
{
    LLButton::Params button_params = p.chiclet_button;
    mChicletButton = LLUICtrlFactory::create<LLButton>(button_params);
    addChild(mChicletButton);

    LLIconCtrl::Params new_msg_params = p.new_message_icon;
    mNewMessagesIcon = LLUICtrlFactory::create<LLIconCtrl>(new_msg_params);
    addChild(mNewMessagesIcon);

    LLChicletAvatarIconCtrl::Params avatar_params = p.avatar_icon;
    mChicletIconCtrl = LLUICtrlFactory::create<LLChicletAvatarIconCtrl>(avatar_params);
    addChild(mChicletIconCtrl);

    LLChicletNotificationCounterCtrl::Params unread_params = p.unread_notifications;
    mCounterCtrl = LLUICtrlFactory::create<LLChicletNotificationCounterCtrl>(unread_params);
    addChild(mCounterCtrl);

    setCounter(getCounter());
    setShowCounter(getShowCounter());

    LLChicletSpeakerCtrl::Params speaker_params = p.speaker;
    mSpeakerCtrl = LLUICtrlFactory::create<LLChicletSpeakerCtrl>(speaker_params);
    addChild(mSpeakerCtrl);

    sendChildToFront(mNewMessagesIcon);
    setShowSpeaker(p.show_speaker);
}

void LLIMP2PChiclet::initSpeakerControl()
{
    mSpeakerCtrl->setSpeakerId(getOtherParticipantId());
}

void LLIMP2PChiclet::setOtherParticipantId(const LLUUID& other_participant_id)
{
    LLIMChiclet::setOtherParticipantId(other_participant_id);
    mChicletIconCtrl->setValue(getOtherParticipantId());
}

void LLIMP2PChiclet::updateMenuItems()
{
    if(!mPopupMenuHandle.get())
        return;
    if(getSessionId().isNull())
        return;

    FSFloaterIM* open_im_floater = FSFloaterIM::findInstance(getSessionId());
    bool open_window_exists = open_im_floater && open_im_floater->getVisible();
    mPopupMenuHandle.get()->getChild<LLUICtrl>("Send IM")->setEnabled(!open_window_exists);

    bool is_friend = LLAvatarActions::isFriend(getOtherParticipantId());
    mPopupMenuHandle.get()->getChild<LLUICtrl>("Add Friend")->setEnabled(!is_friend);
}

void LLIMP2PChiclet::createPopupMenu()
{
    if(!canCreateMenu())
        return;

    LLUICtrl::CommitCallbackRegistry::ScopedRegistrar registrar;
    registrar.add("IMChicletMenu.Action", boost::bind(&LLIMP2PChiclet::onMenuItemClicked, this, _2));

    LLMenuGL* menu = LLUICtrlFactory::getInstance()->createFromFile<LLMenuGL>
        ("menu_fs_imchiclet_p2p.xml", gMenuHolder, LLViewerMenuHolderGL::child_registry_t::instance());
    if (menu)
    {
        mPopupMenuHandle = menu->getHandle();
    }
}

void LLIMP2PChiclet::onMenuItemClicked(const LLSD& user_data)
{
    std::string level = user_data.asString();
    LLUUID other_participant_id = getOtherParticipantId();

    if("profile" == level)
    {
        LLAvatarActions::showProfile(other_participant_id);
    }
    else if("im" == level)
    {
        LLAvatarActions::startIM(other_participant_id);
    }
    else if("add" == level)
    {
        LLAvatarActions::requestFriendshipDialog(other_participant_id);
    }
    else if("end" == level)
    {
        LLAvatarActions::endIM(other_participant_id);
    }
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

LLAdHocChiclet::Params::Params()
: avatar_icon("avatar_icon")
, chiclet_button("chiclet_button")
, unread_notifications("unread_notifications")
, speaker("speaker")
, new_message_icon("new_message_icon")
, show_speaker("show_speaker")
, avatar_icon_color("avatar_icon_color", LLColor4::green)
{
}

LLAdHocChiclet::LLAdHocChiclet(const Params& p)
: LLIMChiclet(p)
, mChicletIconCtrl(NULL)
{
    LLButton::Params button_params = p.chiclet_button;
    mChicletButton = LLUICtrlFactory::create<LLButton>(button_params);
    addChild(mChicletButton);

    LLIconCtrl::Params new_msg_params = p.new_message_icon;
    mNewMessagesIcon = LLUICtrlFactory::create<LLIconCtrl>(new_msg_params);
    addChild(mNewMessagesIcon);

    LLChicletAvatarIconCtrl::Params avatar_params = p.avatar_icon;
    mChicletIconCtrl = LLUICtrlFactory::create<LLChicletAvatarIconCtrl>(avatar_params);
    //Make the avatar modified
    mChicletIconCtrl->setColor(p.avatar_icon_color);
    addChild(mChicletIconCtrl);

    LLChicletNotificationCounterCtrl::Params unread_params = p.unread_notifications;
    mCounterCtrl = LLUICtrlFactory::create<LLChicletNotificationCounterCtrl>(unread_params);
    addChild(mCounterCtrl);

    setCounter(getCounter());
    setShowCounter(getShowCounter());

    LLChicletSpeakerCtrl::Params speaker_params = p.speaker;
    mSpeakerCtrl = LLUICtrlFactory::create<LLChicletSpeakerCtrl>(speaker_params);
    addChild(mSpeakerCtrl);

    sendChildToFront(mNewMessagesIcon);
    setShowSpeaker(p.show_speaker);
}

void LLAdHocChiclet::setSessionId(const LLUUID& session_id)
{
    LLChiclet::setSessionId(session_id);
    LLIMModel::LLIMSession* im_session = LLIMModel::getInstance()->findIMSession(session_id);
    mChicletIconCtrl->setValue(im_session->mOtherParticipantID);
}

void LLAdHocChiclet::draw()
{
    switchToCurrentSpeaker();
    LLIMChiclet::draw();
}

void LLAdHocChiclet::initSpeakerControl()
{
    switchToCurrentSpeaker();
}

void LLAdHocChiclet::switchToCurrentSpeaker()
{
    LLUUID speaker_id;
    LLSpeakerMgr::speaker_list_t speaker_list;

    LLIMModel::getInstance()->findIMSession(getSessionId())->mSpeakers->getSpeakerList(&speaker_list, false);
    for (LLSpeakerMgr::speaker_list_t::iterator i = speaker_list.begin(); i != speaker_list.end(); ++i)
    {
        LLPointer<LLSpeaker> s = *i;
        if (s->mSpeechVolume > 0 || s->mStatus == LLSpeaker::STATUS_SPEAKING)
        {
            speaker_id = s->mID;
            break;
        }
    }

    mSpeakerCtrl->setSpeakerId(speaker_id);
}

void LLAdHocChiclet::createPopupMenu()
{
    if(!canCreateMenu())
        return;

    LLUICtrl::CommitCallbackRegistry::ScopedRegistrar registrar;
    registrar.add("IMChicletMenu.Action", boost::bind(&LLAdHocChiclet::onMenuItemClicked, this, _2));

    auto menu = LLUICtrlFactory::getInstance()->createFromFile<LLMenuGL>
        ("menu_fs_imchiclet_adhoc.xml", gMenuHolder, LLViewerMenuHolderGL::child_registry_t::instance());
    if (menu)
    {
        mPopupMenuHandle = menu->getHandle();
    }
}

void LLAdHocChiclet::onMenuItemClicked(const LLSD& user_data)
{
    std::string level = user_data.asString();
    LLUUID group_id = getSessionId();

    if("end" == level)
    {
        LLGroupActions::endIM(group_id);
    }
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

LLIMGroupChiclet::Params::Params()
: group_icon("group_icon")
, chiclet_button("chiclet_button")
, unread_notifications("unread_notifications")
, speaker("speaker")
, new_message_icon("new_message_icon")
, show_speaker("show_speaker")
{
}

LLIMGroupChiclet::LLIMGroupChiclet(const Params& p)
: LLIMChiclet(p)
, LLGroupMgrObserver(LLUUID::null)
, mChicletIconCtrl(NULL)
{
    LLButton::Params button_params = p.chiclet_button;
    mChicletButton = LLUICtrlFactory::create<LLButton>(button_params);
    addChild(mChicletButton);

    LLIconCtrl::Params new_msg_params = p.new_message_icon;
    mNewMessagesIcon = LLUICtrlFactory::create<LLIconCtrl>(new_msg_params);
    addChild(mNewMessagesIcon);

    LLChicletGroupIconCtrl::Params avatar_params = p.group_icon;
    mChicletIconCtrl = LLUICtrlFactory::create<LLChicletGroupIconCtrl>(avatar_params);
    addChild(mChicletIconCtrl);

    LLChicletNotificationCounterCtrl::Params unread_params = p.unread_notifications;
    mCounterCtrl = LLUICtrlFactory::create<LLChicletNotificationCounterCtrl>(unread_params);
    addChild(mCounterCtrl);

    setCounter(getCounter());
    setShowCounter(getShowCounter());

    LLChicletSpeakerCtrl::Params speaker_params = p.speaker;
    mSpeakerCtrl = LLUICtrlFactory::create<LLChicletSpeakerCtrl>(speaker_params);
    addChild(mSpeakerCtrl);

    sendChildToFront(mNewMessagesIcon);
    setShowSpeaker(p.show_speaker);
}

LLIMGroupChiclet::~LLIMGroupChiclet()
{
    LLGroupMgr::getInstance()->removeObserver(this);
}

void LLIMGroupChiclet::draw()
{
    if(getShowSpeaker())
    {
        switchToCurrentSpeaker();
    }
    LLIMChiclet::draw();
}

void LLIMGroupChiclet::initSpeakerControl()
{
    switchToCurrentSpeaker();
}

void LLIMGroupChiclet::switchToCurrentSpeaker()
{
    LLUUID speaker_id;
    LLSpeakerMgr::speaker_list_t speaker_list;

    LLIMModel::getInstance()->findIMSession(getSessionId())->mSpeakers->getSpeakerList(&speaker_list, false);
    for (LLSpeakerMgr::speaker_list_t::iterator i = speaker_list.begin(); i != speaker_list.end(); ++i)
    {
        LLPointer<LLSpeaker> s = *i;
        if (s->mSpeechVolume > 0 || s->mStatus == LLSpeaker::STATUS_SPEAKING)
        {
            speaker_id = s->mID;
            break;
        }
    }

    mSpeakerCtrl->setSpeakerId(speaker_id);
}

void LLIMGroupChiclet::setSessionId(const LLUUID& session_id)
{
    LLChiclet::setSessionId(session_id);

    LLGroupMgr* grp_mgr = LLGroupMgr::getInstance();
    LLGroupMgrGroupData* group_data = grp_mgr->getGroupData(session_id);
    if (group_data && group_data->mInsigniaID.notNull())
    {
        mChicletIconCtrl->setValue(group_data->mInsigniaID);
    }
    else
    {
        if(getSessionId() != mID)
        {
            grp_mgr->removeObserver(this);
            mID = getSessionId();
            grp_mgr->addObserver(this);
        }
        grp_mgr->sendGroupPropertiesRequest(session_id);
    }
}

void LLIMGroupChiclet::changed(LLGroupChange gc)
{
    if (GC_PROPERTIES == gc)
    {
        LLGroupMgrGroupData* group_data = LLGroupMgr::getInstance()->getGroupData(getSessionId());
        if (group_data)
        {
            mChicletIconCtrl->setValue(group_data->mInsigniaID);
        }
    }
}

void LLIMGroupChiclet::updateMenuItems()
{
    if(!mPopupMenuHandle.get())
        return;
    if(getSessionId().isNull())
        return;

    FSFloaterIM* open_im_floater = FSFloaterIM::findInstance(getSessionId());
    bool open_window_exists = open_im_floater && open_im_floater->getVisible();
    mPopupMenuHandle.get()->getChild<LLUICtrl>("Chat")->setEnabled(!open_window_exists);
}

void LLIMGroupChiclet::createPopupMenu()
{
    if(!canCreateMenu())
        return;

    LLUICtrl::CommitCallbackRegistry::ScopedRegistrar registrar;
    registrar.add("IMChicletMenu.Action", boost::bind(&LLIMGroupChiclet::onMenuItemClicked, this, _2));

    auto menu = LLUICtrlFactory::getInstance()->createFromFile<LLMenuGL>
        ("menu_fs_imchiclet_group.xml", gMenuHolder, LLViewerMenuHolderGL::child_registry_t::instance());
    if (menu)
    {
        mPopupMenuHandle = menu->getHandle();
    }
}

void LLIMGroupChiclet::onMenuItemClicked(const LLSD& user_data)
{
    std::string level = user_data.asString();
    LLUUID group_id = getSessionId();

    if("group chat" == level)
    {
        LLGroupActions::startIM(group_id);
    }
    else if("info" == level)
    {
        LLGroupActions::show(group_id);
    }
// [SL:KB] - Patch: Chat-GroupSnooze | Checked: 2012-06-17 (Catznip-3.3)
    else if("snooze" == level)
    {
        LLGroupActions::snoozeIM(group_id);
    }
    else if("leave" == level)
    {
        LLGroupActions::leaveIM(group_id);
    }
// [/SL:KB]
    else if("end" == level)
    {
        LLGroupActions::endIM(group_id);
    }
}
// <FS:Ansariel> [FS communication UI]

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

LLChicletPanel::Params::Params()
: chiclet_padding("chiclet_padding")
, scrolling_offset("scrolling_offset")
, scroll_button_hpad("scroll_button_hpad")
, scroll_ratio("scroll_ratio")
, min_width("min_width")
{
};

LLChicletPanel::LLChicletPanel(const Params&p)
: LLPanel(p)
, mScrollArea(NULL)
, mLeftScrollButton(NULL)
, mRightScrollButton(NULL)
, mChicletPadding(p.chiclet_padding)
, mScrollingOffset(p.scrolling_offset)
, mScrollButtonHPad(p.scroll_button_hpad)
, mScrollRatio(p.scroll_ratio)
, mMinWidth(p.min_width)
, mShowControls(true)
{
    LLPanel::Params panel_params;
    panel_params.follows.flags(FOLLOWS_LEFT | FOLLOWS_RIGHT);
    mScrollArea = LLUICtrlFactory::create<LLPanel>(panel_params,this);

    // important for Show/Hide Camera and Move controls menu in bottom tray to work properly
    mScrollArea->setMouseOpaque(false);

    addChild(mScrollArea);
}

LLChicletPanel::~LLChicletPanel()
{
    if(LLTransientFloaterMgr::instanceExists())
    {
        LLTransientFloaterMgr::getInstance()->removeControlView(mLeftScrollButton);
        LLTransientFloaterMgr::getInstance()->removeControlView(mRightScrollButton);
    }
}

void LLChicletPanel::onMessageCountChanged(const LLSD& data)
{
    // *TODO : we either suppress this method or return a value. Right now, it servers no purpose.
    /*

    //LLFloaterIMSession* im_floater = LLFloaterIMSession::findInstance(session_id);
    //if (im_floater && im_floater->getVisible() && im_floater->hasFocus())
    //{
    //  unread = 0;
    //}
    */

    // <FS:Ansariel> [FS communication UI]
    LLUUID session_id = data["session_id"].asUUID();
    S32 unread = data["participant_unread"].asInteger();

    FSFloaterIM* im_floater = FSFloaterIM::findInstance(session_id);
    if (im_floater && im_floater->getVisible() && im_floater->hasFocus())
    {
        unread = 0;
    }

    std::list<LLChiclet*> chiclets = LLIMChiclet::sFindChicletsSignal(session_id);
    std::list<LLChiclet *>::iterator iter;
    for (iter = chiclets.begin(); iter != chiclets.end(); iter++)
    {
        LLChiclet* chiclet = *iter;
        if (chiclet != NULL)
        {
            chiclet->setCounter(unread);
        }
        else
        {
            LL_WARNS() << "Unable to set counter for chiclet " << session_id << LL_ENDL;
        }
    }
    // </FS:Ansariel> [FS communication UI]
}

void LLChicletPanel::objectChicletCallback(const LLSD& data)
{
    LLUUID notification_id = data["notification_id"];
    bool new_message = data["new_message"];

    std::list<LLChiclet*> chiclets = LLIMChiclet::sFindChicletsSignal(notification_id);
    std::list<LLChiclet *>::iterator iter;
    for (iter = chiclets.begin(); iter != chiclets.end(); iter++)
    {
        LLIMChiclet* chiclet = dynamic_cast<LLIMChiclet*>(*iter);
        if (chiclet != NULL)
        {
            // <FS:Ansariel> [FS communication UI]
            if(data.has("unread"))
            {
                chiclet->setCounter(data["unread"]);
            }
            // </FS:Ansariel> [FS communication UI]
            chiclet->setShowNewMessagesIcon(new_message);
        }
    }
}

bool LLChicletPanel::postBuild()
{
    LLPanel::postBuild();
    LLIMModel::instance().addNewMsgCallback(boost::bind(&LLChicletPanel::onMessageCountChanged, this, _1));
    LLIMModel::instance().addNoUnreadMsgsCallback(boost::bind(&LLChicletPanel::onMessageCountChanged, this, _1));
    LLScriptFloaterManager::getInstance()->addNewObjectCallback(boost::bind(&LLChicletPanel::objectChicletCallback, this, _1));
    LLScriptFloaterManager::getInstance()->addToggleObjectFloaterCallback(boost::bind(&LLChicletPanel::objectChicletCallback, this, _1));
    LLIMChiclet::sFindChicletsSignal.connect(boost::bind(&LLChicletPanel::findChiclet<LLChiclet>, this, _1));
    LLVoiceChannel::setCurrentVoiceChannelChangedCallback(boost::bind(&LLChicletPanel::onCurrentVoiceChannelChanged, this, _1));

    mLeftScrollButton=getChild<LLButton>("chicklet_left_scroll_button");
    LLTransientFloaterMgr::getInstance()->addControlView(mLeftScrollButton);
    mLeftScrollButton->setMouseDownCallback(boost::bind(&LLChicletPanel::onLeftScrollClick,this));
    mLeftScrollButton->setHeldDownCallback(boost::bind(&LLChicletPanel::onLeftScrollHeldDown,this));
    mLeftScrollButton->setEnabled(false);

    mRightScrollButton=getChild<LLButton>("chicklet_right_scroll_button");
    LLTransientFloaterMgr::getInstance()->addControlView(mRightScrollButton);
    mRightScrollButton->setMouseDownCallback(boost::bind(&LLChicletPanel::onRightScrollClick,this));
    mRightScrollButton->setHeldDownCallback(boost::bind(&LLChicletPanel::onRightScrollHeldDown,this));
    mRightScrollButton->setEnabled(false);

    return true;
}

void LLChicletPanel::onCurrentVoiceChannelChanged(const LLUUID& session_id)
{
    static LLUUID s_previous_active_voice_session_id;

    std::list<LLChiclet*> chiclets = LLIMChiclet::sFindChicletsSignal(session_id);

    for(std::list<LLChiclet *>::iterator it = chiclets.begin(); it != chiclets.end(); ++it)
    {
        LLIMChiclet* chiclet = dynamic_cast<LLIMChiclet*>(*it);
        if(chiclet)
        {
            // <FS:Ansariel> [FS communication UI]
            chiclet->setShowSpeaker(true);
            // </FS:Ansariel> [FS communication UI]
            if (gSavedSettings.getBOOL("OpenIMOnVoice"))
            {
                // <FS:Ansariel> [FS communication UI]
                //LLFloaterIMContainer::getInstance()->showConversation(session_id);
                FSFloaterIM::show(chiclet->getSessionId());
                // </FS:Ansariel> [FS communication UI]
            }
        }
    }

    // <FS:Ansariel> [FS communication UI]
    if(s_previous_active_voice_session_id.notNull() && s_previous_active_voice_session_id != session_id)
    {
        chiclets = LLIMChiclet::sFindChicletsSignal(s_previous_active_voice_session_id);

        for(std::list<LLChiclet *>::iterator it = chiclets.begin(); it != chiclets.end(); ++it)
        {
            LLIMChiclet* chiclet = dynamic_cast<LLIMChiclet*>(*it);
            if(chiclet)
            {
                chiclet->setShowSpeaker(false);
            }
        }
    }
    // </FS:Ansariel> [FS communication UI]

    s_previous_active_voice_session_id = session_id;
}

bool LLChicletPanel::addChiclet(LLChiclet* chiclet, S32 index)
{
    if(mScrollArea->addChild(chiclet))
    {
        // chiclets should be aligned to right edge of scroll panel
        S32 left_shift = 0;

        if (!canScrollLeft())
        {
            // init left shift for the first chiclet in the list...
            if (mChicletList.empty())
            {
                // ...start from the right border of the scroll area for the first added chiclet
                left_shift = mScrollArea->getRect().getWidth();
            }
            else
            {
                // ... start from the left border of the first chiclet minus padding
                left_shift = getChiclet(0)->getRect().mLeft - getChicletPadding();
            }

            // take into account width of the being added chiclet
            left_shift -= chiclet->getRequiredRect().getWidth();

            // if we overflow the scroll area we do not need to shift chiclets
            if (left_shift < 0)
            {
                left_shift = 0;
            }
        }

        mChicletList.insert(mChicletList.begin() + index, chiclet);

        // shift first chiclet to place it in correct position.
        // rest ones will be placed in arrange()
        if (!canScrollLeft())
        {
            getChiclet(0)->translate(left_shift - getChiclet(0)->getRect().mLeft, 0);
        }

        chiclet->setLeftButtonClickCallback(boost::bind(&LLChicletPanel::onChicletClick, this, _1, _2));
        chiclet->setChicletSizeChangedCallback(boost::bind(&LLChicletPanel::onChicletSizeChanged, this, _1, index));

        arrange();
        LLTransientFloaterMgr::getInstance()->addControlView(LLTransientFloaterMgr::IM, chiclet);

        return true;
    }

    return false;
}

void LLChicletPanel::onChicletSizeChanged(LLChiclet* ctrl, const LLSD& param)
{
    arrange();
}

void LLChicletPanel::onChicletClick(LLUICtrl*ctrl,const LLSD&param)
{
    if (mCommitSignal)
    {
        (*mCommitSignal)(ctrl,param);
    }
}

void LLChicletPanel::removeChiclet(chiclet_list_t::iterator it)
{
    LLChiclet* chiclet = *it;
    mScrollArea->removeChild(chiclet);
    mChicletList.erase(it);

    arrange();
    LLTransientFloaterMgr::getInstance()->removeControlView(LLTransientFloaterMgr::IM, chiclet);
    chiclet->die();
}

void LLChicletPanel::removeChiclet(S32 index)
{
    if(index >= 0 && index < getChicletCount())
    {
        removeChiclet(mChicletList.begin() + index);
    }
}

S32 LLChicletPanel::getChicletIndex(const LLChiclet* chiclet)
{
    if(mChicletList.empty())
        return -1;

    S32 size = getChicletCount();
    for(int n = 0; n < size; ++n)
    {
        if(chiclet == mChicletList[n])
            return n;
    }

    return -1;
}

// [SL:KB] - Patch: UI-TabRearrange | Checked: 2012-05-05 (Catznip-3.3.0)
void LLChicletPanel::setChicletIndex(const LLChiclet* chiclet, S32 index)
{
    if ( (index >= mChicletList.size()) || (index < 0) )
        return;

    S32 cur_index = getChicletIndex(chiclet);
    if ( (-1 == cur_index) && (cur_index != index) )
        return;

    mChicletList.erase(mChicletList.begin() + cur_index);
    mChicletList.insert(mChicletList.begin() + index, const_cast<LLChiclet*>(chiclet));
    arrange();
}
// [/SL:KB]

void LLChicletPanel::removeChiclet(LLChiclet*chiclet)
{
    chiclet_list_t::iterator it = mChicletList.begin();
    for( ; mChicletList.end() != it; ++it)
    {
        LLChiclet* temp = *it;
        if(temp == chiclet)
        {
            removeChiclet(it);
            return;
        }
    }
}

void LLChicletPanel::removeChiclet(const LLUUID& im_session_id)
{
    chiclet_list_t::iterator it = mChicletList.begin();
    for( ; mChicletList.end() != it; ++it)
    {
        LLIMChiclet* chiclet = dynamic_cast<LLIMChiclet*>(*it);

        if(chiclet->getSessionId() == im_session_id)
        {
            removeChiclet(it);
            return;
        }
    }
}

void LLChicletPanel::removeAll()
{
    S32 size = getChicletCount();
    for(S32 n = 0; n < size; ++n)
    {
        mScrollArea->removeChild(mChicletList[n]);
    }

    mChicletList.erase(mChicletList.begin(), mChicletList.end());

    showScrollButtonsIfNeeded();
}

void LLChicletPanel::scrollToChiclet(const LLChiclet* chiclet)
{
    const LLRect& rect = chiclet->getRect();

    if (rect.mLeft < 0)
    {
        scroll(llabs(rect.mLeft));
        showScrollButtonsIfNeeded();
    }
    else
    {
        S32 scrollWidth = mScrollArea->getRect().getWidth();

        if (rect.mRight > scrollWidth)
        {
            scroll(-llabs(rect.mRight - scrollWidth));
            showScrollButtonsIfNeeded();
        }
    }
}

void LLChicletPanel::reshape(S32 width, S32 height, bool called_from_parent )
{
    LLPanel::reshape(width,height,called_from_parent);

    //Needed once- to avoid error at first call of reshape() before postBuild()
    if(!mLeftScrollButton||!mRightScrollButton)
        return;

    LLRect scroll_button_rect = mLeftScrollButton->getRect();
    mLeftScrollButton->setRect(LLRect(0,scroll_button_rect.mTop,scroll_button_rect.getWidth(),
        scroll_button_rect.mBottom));
    scroll_button_rect = mRightScrollButton->getRect();
    mRightScrollButton->setRect(LLRect(width - scroll_button_rect.getWidth(),scroll_button_rect.mTop,
        width, scroll_button_rect.mBottom));


    bool need_show_scroll = needShowScroll();
    if(need_show_scroll)
    {
        mScrollArea->setRect(LLRect(scroll_button_rect.getWidth() + mScrollButtonHPad,
            height, width - scroll_button_rect.getWidth() - mScrollButtonHPad, 0));
    }
    else
    {
        mScrollArea->setRect(LLRect(0,height, width, 0));
    }

    mShowControls = width >= mMinWidth;

    mScrollArea->setVisible(mShowControls);

    trimChiclets();
    showScrollButtonsIfNeeded();

}

S32 LLChicletPanel::notifyParent(const LLSD& info)
{
    if(info.has("notification"))
    {
        std::string str_notification = info["notification"];
        if(str_notification == "size_changes")
        {
            arrange();
            return 1;
        }
    }
    return LLPanel::notifyParent(info);
}

void LLChicletPanel::setChicletToggleState(const LLUUID& session_id, bool toggle)
{
    if(session_id.isNull())
    {
        LL_WARNS() << "Null Session ID" << LL_ENDL;
    }

    // toggle off all chiclets, except specified
    S32 size = getChicletCount();
    for(int n = 0; n < size; ++n)
    {
        LLIMChiclet* chiclet = getChiclet<LLIMChiclet>(n);
        if(chiclet && chiclet->getSessionId() != session_id)
        {
            chiclet->setToggleState(false);
        }
    }

    // toggle specified chiclet
    LLIMChiclet* chiclet = findChiclet<LLIMChiclet>(session_id);
    if(chiclet)
    {
        chiclet->setToggleState(toggle);
    }
}

void LLChicletPanel::arrange()
{
    if(mChicletList.empty())
        return;

    //initial arrange of chicklets positions
    S32 chiclet_left = getChiclet(0)->getRect().mLeft;
    S32 size = getChicletCount();
    for( int n = 0; n < size; ++n)
    {
        LLChiclet* chiclet = getChiclet(n);

        S32 chiclet_width = chiclet->getRequiredRect().getWidth();
        LLRect rect = chiclet->getRect();
        rect.set(chiclet_left, rect.mTop, chiclet_left + chiclet_width, rect.mBottom);

        chiclet->setRect(rect);

        chiclet_left += chiclet_width + getChicletPadding();
    }

    //reset size and pos on mScrollArea
    LLRect rect = getRect();
    LLRect scroll_button_rect = mLeftScrollButton->getRect();

    bool need_show_scroll = needShowScroll();
    if(need_show_scroll)
    {
        mScrollArea->setRect(LLRect(scroll_button_rect.getWidth() + mScrollButtonHPad,
            rect.getHeight(), rect.getWidth() - scroll_button_rect.getWidth() - mScrollButtonHPad, 0));
    }
    else
    {
        mScrollArea->setRect(LLRect(0,rect.getHeight(), rect.getWidth(), 0));
    }

    trimChiclets();
    showScrollButtonsIfNeeded();
}

void LLChicletPanel::trimChiclets()
{
    // trim right
    if(!mChicletList.empty())
    {
        S32 last_chiclet_right = (*mChicletList.rbegin())->getRect().mRight;
        S32 first_chiclet_left = getChiclet(0)->getRect().mLeft;
        S32 scroll_width = mScrollArea->getRect().getWidth();
        if(last_chiclet_right < scroll_width || first_chiclet_left > 0)
        {
            shiftChiclets(scroll_width - last_chiclet_right);
        }
    }
}

bool LLChicletPanel::needShowScroll()
{
    if(mChicletList.empty())
        return false;

    S32 chicklet_width  = (*mChicletList.rbegin())->getRect().mRight - (*mChicletList.begin())->getRect().mLeft;

    return chicklet_width>getRect().getWidth();
}


void LLChicletPanel::showScrollButtonsIfNeeded()
{
    bool can_scroll_left = canScrollLeft();
    bool can_scroll_right = canScrollRight();

    mLeftScrollButton->setEnabled(can_scroll_left);
    mRightScrollButton->setEnabled(can_scroll_right);

    bool show_scroll_buttons = (can_scroll_left || can_scroll_right) && mShowControls;

    mLeftScrollButton->setVisible(show_scroll_buttons);
    mRightScrollButton->setVisible(show_scroll_buttons);
}

void LLChicletPanel::draw()
{
    child_list_const_iter_t it = getChildList()->begin();
    for( ; getChildList()->end() != it; ++it)
    {
        LLView* child = *it;
        if(child == dynamic_cast<LLView*>(mScrollArea))
        {
            LLLocalClipRect clip(mScrollArea->getRect());
            drawChild(mScrollArea);
        }
        else
        {
            drawChild(child);
        }
    }
}

bool LLChicletPanel::canScrollRight()
{
    if(mChicletList.empty())
        return false;

    S32 scroll_width = mScrollArea->getRect().getWidth();
    S32 last_chiclet_right = (*mChicletList.rbegin())->getRect().mRight;

    if(last_chiclet_right > scroll_width)
        return true;

    return false;
}

bool LLChicletPanel::canScrollLeft()
{
    if(mChicletList.empty())
        return false;

    return getChiclet(0)->getRect().mLeft < 0;
}

void LLChicletPanel::scroll(S32 offset)
{
    shiftChiclets(offset);
}

void LLChicletPanel::shiftChiclets(S32 offset, S32 start_index /* = 0 */)
{
    if(start_index < 0 || start_index >= getChicletCount())
    {
        return;
    }

    chiclet_list_t::const_iterator it = mChicletList.begin() + start_index;
    for(;mChicletList.end() != it; ++it)
    {
        LLChiclet* chiclet = *it;
        chiclet->translate(offset,0);
    }
}

void LLChicletPanel::scrollLeft()
{
    if(canScrollLeft())
    {
        S32 offset = getScrollingOffset();
        LLRect first_chiclet_rect = getChiclet(0)->getRect();

        // shift chiclets in case first chiclet is partially visible
        if(first_chiclet_rect.mLeft < 0 && first_chiclet_rect.mRight > 0)
        {
            offset = llabs(first_chiclet_rect.mLeft);
        }

        scroll(offset);

        showScrollButtonsIfNeeded();
    }
}

void LLChicletPanel::scrollRight()
{
    if(canScrollRight())
    {
        S32 offset = - getScrollingOffset();

        S32 last_chiclet_right = (*mChicletList.rbegin())->getRect().mRight;
        S32 scroll_rect_width = mScrollArea->getRect().getWidth();
        // if after scrolling, the last chiclet will not be aligned to
        // scroll area right side - align it.
        if( last_chiclet_right + offset < scroll_rect_width )
        {
            offset = scroll_rect_width - last_chiclet_right;
        }

        scroll(offset);

        showScrollButtonsIfNeeded();
    }
}

void LLChicletPanel::onLeftScrollClick()
{
    scrollLeft();
}

void LLChicletPanel::onRightScrollClick()
{
    scrollRight();
}

void LLChicletPanel::onLeftScrollHeldDown()
{
    S32 offset = mScrollingOffset;
    mScrollingOffset = mScrollingOffset / mScrollRatio;
    scrollLeft();
    mScrollingOffset = offset;
}

void LLChicletPanel::onRightScrollHeldDown()
{
    S32 offset = mScrollingOffset;
    mScrollingOffset = mScrollingOffset / mScrollRatio;
    scrollRight();
    mScrollingOffset = offset;
}

boost::signals2::connection LLChicletPanel::setChicletClickedCallback(
    const commit_callback_t& cb)
{
    return setCommitCallback(cb);
}

bool LLChicletPanel::handleScrollWheel(S32 x, S32 y, S32 clicks)
{
    if(clicks > 0)
    {
        scrollRight();
    }
    else
    {
        scrollLeft();
    }
    return true;
}

bool LLChicletPanel::isAnyIMFloaterDoked()
{
    bool res = false;
    for (chiclet_list_t::iterator it = mChicletList.begin(); it
            != mChicletList.end(); it++)
    {
        // <FS:Ansariel> [FS communication UI]
        //LLFloaterIMSession* im_floater = LLFloaterReg::findTypedInstance<LLFloaterIMSession>(
        //      "impanel", (*it)->getSessionId());
        FSFloaterIM* im_floater = LLFloaterReg::findTypedInstance<FSFloaterIM>(
                "fs_impanel", (*it)->getSessionId());
        // </FS:Ansariel> [FS communication UI]
        if (im_floater != NULL && im_floater->getVisible()
                && !im_floater->isMinimized() && im_floater->isDocked())
        {
            res = true;
            break;
        }
    }

    return res;
}

// <FS:Ansariel> [FS communication UI]
S32 LLChicletPanel::getTotalUnreadIMCount()
{
    S32 count = 0;
    chiclet_list_t::const_iterator it = mChicletList.begin();
    for( ; mChicletList.end() != it; ++it)
    {
        LLIMChiclet* chiclet = dynamic_cast<LLIMChiclet*>(*it);
        if(chiclet)
        {
            count += chiclet->getCounter();
        }
    }
    return count;
}
// </FS:Ansariel> [FS communication UI]

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
LLChicletNotificationCounterCtrl::Params::Params()
    : max_displayed_count("max_displayed_count", 99)
{
}

// <FS:Ansariel> [FS communication UI]
LLChicletNotificationCounterCtrl::LLChicletNotificationCounterCtrl(const Params& p)
 : LLTextBox(p)
 , mCounter(0)
 , mInitialWidth(0)
 , mMaxDisplayedCount(p.max_displayed_count)
{
    mInitialWidth = getRect().getWidth();
}

void LLChicletNotificationCounterCtrl::setCounter(S32 counter)
{
    mCounter = counter;

    // note same code in LLSysWellChiclet::setCounter(S32 counter)
    std::string s_count;
    if(counter != 0)
    {
        static std::string more_messages_exist("+");
        std::string more_messages(counter > mMaxDisplayedCount ? more_messages_exist : "");
        s_count = llformat("%d%s"
            , llmin(counter, mMaxDisplayedCount)
            , more_messages.c_str()
            );
    }

    if(mCounter != 0)
    {
        setText(s_count);
    }
    else
    {
        setText(std::string(""));
    }
}

LLRect LLChicletNotificationCounterCtrl::getRequiredRect()
{
    LLRect rc;
    S32 text_width = getTextPixelWidth();

    rc.mRight = rc.mLeft + llmax(text_width, mInitialWidth);

    return rc;
}

void LLChicletNotificationCounterCtrl::setValue(const LLSD& value)
{
    if(value.isInteger())
        setCounter(value.asInteger());
}

LLSD LLChicletNotificationCounterCtrl::getValue() const
{
    return LLSD(getCounter());
}
// </FS:Ansariel> [FS communication UI]

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
LLChicletAvatarIconCtrl::LLChicletAvatarIconCtrl(const Params& p)
 : LLAvatarIconCtrl(p)
{
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

// <FS:Ansariel> [FS communication UI]
LLChicletGroupIconCtrl::LLChicletGroupIconCtrl(const Params& p)
: LLIconCtrl(p)
, mDefaultIcon(p.default_icon)
{
    setValue(LLUUID::null);
}

void LLChicletGroupIconCtrl::setValue(const LLSD& value )
{
    if(value.asUUID().isNull())
    {
        LLIconCtrl::setValue(mDefaultIcon);
    }
    else
    {
        LLIconCtrl::setValue(value);
    }
}
// </FS:Ansariel> [FS communication UI]

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

LLChicletInvOfferIconCtrl::LLChicletInvOfferIconCtrl(const Params& p)
: LLChicletAvatarIconCtrl(p)
 , mDefaultIcon(p.default_icon)
{
}

void LLChicletInvOfferIconCtrl::setValue(const LLSD& value )
{
    if(value.asUUID().isNull())
    {
        LLIconCtrl::setValue(mDefaultIcon);
    }
    else
    {
        LLChicletAvatarIconCtrl::setValue(value);
    }
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

// <FS:Ansariel> [FS communication UI]
LLChicletSpeakerCtrl::LLChicletSpeakerCtrl(const Params&p)
 : LLOutputMonitorCtrl(p)
{
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
// </FS:Ansariel> [FS communication UI]

LLScriptChiclet::Params::Params()
 : icon("icon")
 , chiclet_button("chiclet_button")
 , new_message_icon("new_message_icon")
{
}

LLScriptChiclet::LLScriptChiclet(const Params&p)
 : LLIMChiclet(p)
 , mChicletIconCtrl(NULL)
{
    LLButton::Params button_params = p.chiclet_button;
    mChicletButton = LLUICtrlFactory::create<LLButton>(button_params);
    addChild(mChicletButton);

    LLIconCtrl::Params new_msg_params = p.new_message_icon;
    mNewMessagesIcon = LLUICtrlFactory::create<LLIconCtrl>(new_msg_params);
    addChild(mNewMessagesIcon);

    LLIconCtrl::Params icon_params = p.icon;
    mChicletIconCtrl = LLUICtrlFactory::create<LLIconCtrl>(icon_params);
    addChild(mChicletIconCtrl);

    sendChildToFront(mNewMessagesIcon);
}

void LLScriptChiclet::setSessionId(const LLUUID& session_id)
{
    setShowNewMessagesIcon( getSessionId() != session_id );

    LLIMChiclet::setSessionId(session_id);

    setToolTip(LLScriptFloaterManager::getObjectName(session_id));
}

// <FS:Ansariel> [FS communication UI]
void LLScriptChiclet::setCounter(S32 counter)
{
    setShowNewMessagesIcon( counter > 0 );
}
// </FS:Ansariel> [FS communication UI]

void LLScriptChiclet::onMouseDown()
{
    LLScriptFloaterManager::getInstance()->toggleScriptFloater(getSessionId());
}

void LLScriptChiclet::onMenuItemClicked(const LLSD& user_data)
{
    std::string action = user_data.asString();

    if("end" == action)
    {
        LLScriptFloaterManager::instance().removeNotification(getSessionId());
    }
    else if ("close all" == action)
    {
        LLIMWellWindow::getInstance()->closeAll();
    }
}

void LLScriptChiclet::createPopupMenu()
{
    if(!canCreateMenu())
        return;

    LLUICtrl::CommitCallbackRegistry::ScopedRegistrar registrar;
    registrar.add("ScriptChiclet.Action", boost::bind(&LLScriptChiclet::onMenuItemClicked, this, _2));

    LLMenuGL* menu = LLUICtrlFactory::getInstance()->createFromFile<LLMenuGL>
        ("menu_script_chiclet.xml", gMenuHolder, LLViewerMenuHolderGL::child_registry_t::instance());
    if (menu)
    {
        mPopupMenuHandle = menu->getHandle();
    }

}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static const std::string INVENTORY_USER_OFFER   ("UserGiveItem");

LLInvOfferChiclet::Params::Params()
 : icon("icon")
 , chiclet_button("chiclet_button")
 , new_message_icon("new_message_icon")
{
}

LLInvOfferChiclet::LLInvOfferChiclet(const Params&p)
 : LLIMChiclet(p)
 , mChicletIconCtrl(NULL)
{
    LLButton::Params button_params = p.chiclet_button;
    mChicletButton = LLUICtrlFactory::create<LLButton>(button_params);
    addChild(mChicletButton);

    LLIconCtrl::Params new_msg_params = p.new_message_icon;
    mNewMessagesIcon = LLUICtrlFactory::create<LLIconCtrl>(new_msg_params);
    addChild(mNewMessagesIcon);

    LLChicletInvOfferIconCtrl::Params icon_params = p.icon;
    mChicletIconCtrl = LLUICtrlFactory::create<LLChicletInvOfferIconCtrl>(icon_params);
    addChild(mChicletIconCtrl);

    sendChildToFront(mNewMessagesIcon);
}

void LLInvOfferChiclet::setSessionId(const LLUUID& session_id)
{
    setShowNewMessagesIcon( getSessionId() != session_id );

    setToolTip(LLScriptFloaterManager::getObjectName(session_id));

    LLIMChiclet::setSessionId(session_id);
    LLNotificationPtr notification = LLNotifications::getInstance()->find(session_id);

    if ( notification && notification->getName() == INVENTORY_USER_OFFER )
    {
        mChicletIconCtrl->setValue(notification->getPayload()["from_id"]);
    }
    else
    {
        mChicletIconCtrl->setValue(LLUUID::null);
    }
}

// <FS:Ansariel> [FS communication UI]
void LLInvOfferChiclet::setCounter(S32 counter)
{
    setShowNewMessagesIcon( counter > 0 );
}
// </FS:Ansariel> [FS communication UI]

void LLInvOfferChiclet::onMouseDown()
{
    LLScriptFloaterManager::instance().toggleScriptFloater(getSessionId());
}

void LLInvOfferChiclet::onMenuItemClicked(const LLSD& user_data)
{
    std::string action = user_data.asString();

    if("end" == action)
    {
        LLScriptFloaterManager::instance().removeNotification(getSessionId());
    }
}

void LLInvOfferChiclet::createPopupMenu()
{
    if(!canCreateMenu())
        return;

    LLUICtrl::CommitCallbackRegistry::ScopedRegistrar registrar;
    registrar.add("InvOfferChiclet.Action", boost::bind(&LLInvOfferChiclet::onMenuItemClicked, this, _2));

    LLMenuGL* menu = LLUICtrlFactory::getInstance()->createFromFile<LLMenuGL>
        ("menu_inv_offer_chiclet.xml", gMenuHolder, LLViewerMenuHolderGL::child_registry_t::instance());
    if (menu)
    {
        mPopupMenuHandle = menu->getHandle();
    }
}

// EOF
