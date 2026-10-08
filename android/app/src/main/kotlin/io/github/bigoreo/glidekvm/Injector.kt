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

import android.content.ComponentName
import android.content.Context
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.os.Binder
import android.os.Build
import android.os.IBinder
import android.os.Parcel
import android.os.RemoteException
import android.util.Log
import android.view.InputEvent
import org.lsposed.hiddenapibypass.HiddenApiBypass
import rikka.shizuku.Shizuku
import kotlin.system.exitProcess

// Android only lets the system and the shell user inject input into other
// apps. Shizuku starts InjectorService as the shell user, and the app hands
// it each event over a binder.
object Injector {
    private const val TAG = "GlideKVM"
    const val DESCRIPTOR = "io.github.bigoreo.glidekvm.Injector"
    const val INJECT = IBinder.FIRST_CALL_TRANSACTION
    // the code Shizuku sends to stop a user service
    const val DESTROY = 16777115
    const val SHIZUKU_PACKAGE = "moe.shizuku.privileged.api"

    enum class Readiness { NOT_INSTALLED, NOT_RUNNING, NEEDS_PERMISSION, TOO_OLD, READY }

    @Volatile
    private var service: IBinder? = null
    private var bound = false

    private val args by lazy {
        Shizuku.UserServiceArgs(ComponentName(BuildConfig.APPLICATION_ID, InjectorService::class.java.name))
            .daemon(false)
            .processNameSuffix("input")
            .debuggable(BuildConfig.DEBUG)
            .version(BuildConfig.VERSION_CODE)
    }

    private val connection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, binder: IBinder?) {
            service = binder
        }

        override fun onServiceDisconnected(name: ComponentName?) {
            service = null
        }
    }

    fun readiness(context: Context): Readiness {
        val installed = try {
            context.packageManager.getPackageInfo(SHIZUKU_PACKAGE, 0)
            true
        } catch (e: PackageManager.NameNotFoundException) {
            false
        }
        if (!Shizuku.pingBinder()) return if (installed) Readiness.NOT_RUNNING else Readiness.NOT_INSTALLED
        if (Shizuku.isPreV11() || Shizuku.getVersion() < 11) return Readiness.TOO_OLD
        if (Shizuku.checkSelfPermission() != PackageManager.PERMISSION_GRANTED) return Readiness.NEEDS_PERMISSION
        return Readiness.READY
    }

    val ready: Boolean get() = service?.isBinderAlive == true

    fun bind(context: Context) {
        if (bound || readiness(context) != Readiness.READY) return
        try {
            Shizuku.bindUserService(args, connection)
            bound = true
        } catch (e: RuntimeException) {
            Log.w(TAG, "could not start the input service", e)
        }
    }

    fun unbind() {
        if (!bound) return
        try {
            Shizuku.unbindUserService(args, connection, true)
        } catch (e: RuntimeException) {
            Log.w(TAG, "could not stop the input service", e)
        }
        bound = false
        service = null
    }

    // Returns false when the event could not be delivered.
    fun inject(event: InputEvent): Boolean {
        val binder = service ?: return false
        val data = Parcel.obtain()
        return try {
            data.writeInterfaceToken(DESCRIPTOR)
            event.writeToParcel(data, 0)
            // one-way calls to one binder arrive in the order they were sent
            binder.transact(INJECT, data, null, IBinder.FLAG_ONEWAY)
        } catch (e: RemoteException) {
            service = null
            false
        } finally {
            data.recycle()
        }
    }
}

// Runs in its own process as the shell user, started by Shizuku.
class InjectorService : Binder() {
    private val injectMethod: (InputEvent) -> Unit by lazy { findInjectMethod() }

    override fun onTransact(code: Int, data: Parcel, reply: Parcel?, flags: Int): Boolean {
        when (code) {
            Injector.INJECT -> {
                data.enforceInterface(Injector.DESCRIPTOR)
                val event = InputEvent.CREATOR.createFromParcel(data)
                try {
                    injectMethod(event)
                } catch (e: Exception) {
                    Log.w("GlideKVM", "could not inject $event", e)
                }
                return true
            }
            Injector.DESTROY -> exitProcess(0)
        }
        return super.onTransact(code, data, reply, flags)
    }

    private fun findInjectMethod(): (InputEvent) -> Unit {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            HiddenApiBypass.addHiddenApiExemptions("L")
        }
        val mode = 0 // INJECT_INPUT_EVENT_MODE_ASYNC
        // Android 14 moved the method from InputManager to InputManagerGlobal
        val owner = try {
            Class.forName("android.hardware.input.InputManagerGlobal")
        } catch (e: ClassNotFoundException) {
            Class.forName("android.hardware.input.InputManager")
        }
        val instance = owner.getMethod("getInstance").invoke(null)
        val method = owner.getMethod("injectInputEvent", InputEvent::class.java, Int::class.javaPrimitiveType)
        return { event -> method.invoke(instance, event, mode) }
    }
}
