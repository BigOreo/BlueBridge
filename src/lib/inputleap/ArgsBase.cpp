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

#include "inputleap/ArgsBase.h"
#include "base/Log.h"

#include <cctype>

namespace inputleap {

ArgsBase::ArgsBase() :
#if SYSAPI_WIN32
m_daemon(false), // daemon mode not supported on windows (use --service)
m_debugServiceWait(false),
m_pauseOnExit(false),
m_stopOnDeskSwitch(false),
#else
m_daemon(true), // backward compatibility for unix (daemon by default)
#endif
m_backend(false),
m_restartable(true),
m_noHooks(false),
m_logFilter(nullptr),
m_logFile(nullptr),
m_display(nullptr),
m_disableTray(false),
m_enableIpc(false),
m_enableDragDrop(false),
m_dropTarget(""),
m_shouldExit(false),
network_address(),
m_enableCrypto(true),
m_profileDirectory(),
m_pluginDirectory("")
{
}

ArgsBase::~ArgsBase()
{
}

bool is_bluetooth_host(const std::string& host)
{
    return host.size() >= 2 &&
           std::tolower(static_cast<unsigned char>(host[0])) == 'b' &&
           std::tolower(static_cast<unsigned char>(host[1])) == 't' &&
           (host.size() == 2 || host[2] == ':');
}

bool apply_machine_policy(ArgsBase& args, const std::string& host, bool also_bluetooth)
{
    args.m_policy = read_machine_policy();
    const MachinePolicy& policy = args.m_policy;
    if (!policy.any()) {
        return true;
    }
    LOG_NOTE("this computer is managed by your organization; applying its policy");

    if (policy.encryption_required() && !args.m_enableCrypto) {
        LOG_WARN("encryption is required by your organization's policy, ignoring --disable-crypto");
        args.m_enableCrypto = true;
    }
    if (policy.file_transfer_disabled() && args.m_enableDragDrop) {
        LOG_NOTE("file transfer is disabled by your organization's policy");
        args.m_enableDragDrop = false;
    }
    if (policy.clipboard_sharing_disabled()) {
        LOG_NOTE("clipboard sharing is disabled by your organization's policy");
    }

    bool bluetooth = is_bluetooth_host(host);
    if (bluetooth && !policy.bluetooth_allowed()) {
        LOG_ERR("connecting over Bluetooth is disabled by your organization's policy");
        return false;
    }
    if (!bluetooth && !policy.network_allowed()) {
        if (also_bluetooth && policy.bluetooth_allowed()) {
            LOG_NOTE("connecting over the network is disabled by your organization's policy, "
                     "accepting Bluetooth connections only");
            return true;
        }
        LOG_ERR("connecting over the network is disabled by your organization's policy");
        return false;
    }
    return true;
}

} // namespace inputleap
