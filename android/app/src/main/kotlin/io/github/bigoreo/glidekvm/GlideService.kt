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

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.ComponentCallbacks
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.content.res.Configuration
import android.graphics.Point
import android.hardware.display.DisplayManager
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.view.Display
import io.github.bigoreo.glidekvm.core.Connection
import io.github.bigoreo.glidekvm.core.DEFAULT_PORT
import io.github.bigoreo.glidekvm.core.ScreenShape
import io.github.bigoreo.glidekvm.core.UntrustedServerException
import java.io.IOException

// Keeps the connection to the main computer while the app is in the
// background, and tries again when it drops.
class GlideService : Service() {
    private val main = Handler(Looper.getMainLooper())
    private lateinit var overlay: CursorOverlay
    @Volatile private var running = false
    @Volatile private var connection: Connection? = null
    private var worker: Thread? = null

    private val rotation = object : ComponentCallbacks {
        override fun onConfigurationChanged(newConfig: Configuration) {
            connection?.screenChanged()
        }

        @Deprecated("Deprecated in Java")
        override fun onLowMemory() {}
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        overlay = CursorOverlay(this)
        registerComponentCallbacks(rotation)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_STOP) {
            stopConnection()
            ConnectionState.set(Status.Idle)
            stopSelf()
            return START_NOT_STICKY
        }
        val address = intent?.getStringExtra(EXTRA_ADDRESS) ?: return START_NOT_STICKY
        val name = intent.getStringExtra(EXTRA_NAME) ?: DeviceIdentity.defaultName(this)
        startInForeground(address)
        stopConnection()
        Injector.bind(this)
        running = true
        worker = Thread({ connectLoop(address, name) }, "glidekvm-connection").apply { start() }
        return START_REDELIVER_INTENT
    }

    override fun onDestroy() {
        stopConnection()
        unregisterComponentCallbacks(rotation)
        overlay.close()
        Injector.unbind()
        super.onDestroy()
    }

    private fun stopConnection() {
        running = false
        connection?.stop()
        worker?.interrupt()
        worker = null
    }

    private fun connectLoop(address: String, name: String) {
        val host = address.substringBefore(":").trim()
        val port = address.substringAfter(":", "").toIntOrNull() ?: DEFAULT_PORT
        var wait = 2_000L
        // when the attempts since the last good connection started failing
        var failingSince = 0L
        while (running) {
            ConnectionState.set(Status.Connecting(host))
            val translator = InputTranslator(this, overlay, ::screenShape) {
                wait = 2_000L
                failingSince = 0L
                ConnectionState.set(Status.Connected(host))
                // Shizuku may have started after this service did
                main.post { Injector.bind(this) }
            }
            val c = Connection(host, port, name, DeviceIdentity.keyManagers(), DeviceIdentity.trustedServers(this),
                               ::screenShape, translator) { translator.position }
            connection = c
            var reason: String
            try {
                c.run()
                if (!running) break
                reason = "The main computer ended the connection."
            } catch (e: UntrustedServerException) {
                // the person confirms the fingerprint on the screen, then connects again
                ConnectionState.set(Status.Untrusted(host, port, e.fingerprint))
                running = false
                break
            } catch (e: IOException) {
                if (!running) break
                reason = reasonFor(e, host)
            } catch (e: RuntimeException) {
                ConnectionState.set(Status.Failed(e.message ?: e.toString()))
                running = false
                break
            } finally {
                // the connection ended: put the usual keyboard back
                translator.leave()
                connection = null
            }
            // keep trying for a while, as the main computer may be restarting
            // or the network coming back, but not for ever on a battery
            val now = System.currentTimeMillis()
            if (failingSince == 0L) failingSince = now
            if (now - failingSince > GIVE_UP_AFTER_MS) {
                ConnectionState.set(Status.Failed("$reason Stopped trying. Tap Connect when the main computer is sharing again."))
                break
            }
            ConnectionState.set(Status.Retrying(host, reason))
            try {
                Thread.sleep(wait)
            } catch (e: InterruptedException) {
                break
            }
            wait = (wait * 2).coerceAtMost(30_000L)
        }
        if (ConnectionState.status !is Status.Untrusted && ConnectionState.status !is Status.Failed) {
            ConnectionState.set(Status.Idle)
        }
        stopSelf()
    }

    private fun reasonFor(e: IOException, host: String): String = when (e) {
        is java.net.ConnectException -> "The main computer isn't sharing right now."
        is java.net.UnknownHostException -> "Can't find $host. Check the address."
        is java.net.NoRouteToHostException, is java.net.SocketTimeoutException ->
            "Can't reach $host. Check the address, and that both are on the same network."
        else -> e.message ?: "Can't reach $host."
    }

    // The whole screen in its current rotation, in pixels.
    private fun screenShape(): ScreenShape {
        val display = getSystemService(DisplayManager::class.java)!!.getDisplay(Display.DEFAULT_DISPLAY)
        val size = Point()
        @Suppress("DEPRECATION")
        display.getRealSize(size)
        return ScreenShape(size.x, size.y)
    }

    private fun startInForeground(address: String) {
        val manager = getSystemService(NotificationManager::class.java)!!
        manager.createNotificationChannel(
            NotificationChannel(CHANNEL, "Connection", NotificationManager.IMPORTANCE_LOW)
        )
        val open = PendingIntent.getActivity(this, 0, Intent(this, MainActivity::class.java),
                                             PendingIntent.FLAG_IMMUTABLE)
        val stop = PendingIntent.getService(this, 1, Intent(this, GlideService::class.java).setAction(ACTION_STOP),
                                            PendingIntent.FLAG_IMMUTABLE)
        val notification = Notification.Builder(this, CHANNEL)
            .setSmallIcon(R.drawable.ic_notification)
            .setContentTitle("GlideKVM")
            .setContentText("Using the keyboard and mouse of $address")
            .setContentIntent(open)
            .addAction(Notification.Action.Builder(null, "Disconnect", stop).build())
            .setOngoing(true)
            .build()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(NOTIFICATION_ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE)
        } else {
            startForeground(NOTIFICATION_ID, notification)
        }
    }

    companion object {
        private const val GIVE_UP_AFTER_MS = 10 * 60 * 1000L
        private const val CHANNEL = "connection"
        private const val NOTIFICATION_ID = 1
        private const val ACTION_STOP = "io.github.bigoreo.glidekvm.STOP"
        private const val EXTRA_ADDRESS = "address"
        private const val EXTRA_NAME = "name"

        fun start(context: Context, address: String, name: String) {
            context.startForegroundService(
                Intent(context, GlideService::class.java)
                    .putExtra(EXTRA_ADDRESS, address)
                    .putExtra(EXTRA_NAME, name)
            )
        }

        fun stop(context: Context) {
            context.startService(Intent(context, GlideService::class.java).setAction(ACTION_STOP))
        }
    }
}
