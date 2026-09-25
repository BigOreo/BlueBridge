/*
 * InputLeap -- mouse and keyboard sharing utility
 * Copyright (C) 2012-2016 Symless Ltd.
 * Copyright (C) 2012 Nick Bolton
 *
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file LICENSE that should have accompanied this file.
 *
 * This package is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "common/Policy.h"
#include "io/filesystem.h"

#include <string>

namespace inputleap {

class ArgsBase {
public:
    ArgsBase();
    virtual ~ArgsBase();

public:
    bool m_daemon;
    bool m_backend;
    bool m_restartable;
    bool m_noHooks;
    std::string m_exename;
    const char* m_logFilter;
    const char* m_logFile;
    const char* m_display;
    std::string m_name;
    bool m_disableTray;
    bool m_enableIpc;
    bool m_enableDragDrop;
    std::string m_dropTarget;
#if SYSAPI_WIN32
    bool m_debugServiceWait;
    bool m_pauseOnExit;
    bool m_stopOnDeskSwitch;
#endif
    bool m_shouldExit;
    std::string network_address;
    bool m_enableCrypto;
    inputleap::fs::path m_profileDirectory;
    inputleap::fs::path m_pluginDirectory;
    bool use_x11 = false;
    bool use_ei = false;
    bool use_portal = true; // use the XDG portals for ei
    // what this computer's administrator enforces, see apply_machine_policy()
    MachinePolicy m_policy;
};

// Whether host (as given in an address, without the port) names a
// Bluetooth address rather than a network one.
bool is_bluetooth_host(const std::string& host);

// Reads this computer's policy into args.m_policy and applies what it
// enforces to the arguments (encryption, file transfer). host is the server
// or listen address' host, empty for all network interfaces. Returns false,
// after logging why, when the policy does not allow connecting that way.
bool apply_machine_policy(ArgsBase& args, const std::string& host);

} // namespace inputleap
