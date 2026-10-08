/*
    GlideKVM -- mouse and keyboard sharing utility
    Copyright (C) GlideKVM contributors

    This package is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    found in the file LICENSE that should have accompanied this file.

    This package is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

package io.github.bigoreo.glidekvm

import android.os.Handler
import android.os.Looper
import io.github.bigoreo.glidekvm.core.Fingerprint

// Where the connection stands, shared by the service and the screen.
sealed class Status {
    object Idle : Status()
    data class Connecting(val host: String) : Status()
    data class Connected(val host: String) : Status()
    // the connection dropped; the service tries again on its own
    data class Retrying(val host: String, val reason: String) : Status()
    // the main computer's certificate needs confirming before connecting
    data class Untrusted(val host: String, val port: Int, val fingerprint: Fingerprint) : Status()
    data class Failed(val reason: String) : Status()
}

object ConnectionState {
    private val main = Handler(Looper.getMainLooper())
    private val listeners = mutableListOf<(Status) -> Unit>()

    @Volatile
    var status: Status = Status.Idle
        private set

    // listeners are called on the main thread
    fun listen(listener: (Status) -> Unit) {
        listeners += listener
        listener(status)
    }

    fun unlisten(listener: (Status) -> Unit) {
        listeners -= listener
    }

    fun set(status: Status) {
        this.status = status
        main.post { listeners.toList().forEach { it(status) } }
    }
}
