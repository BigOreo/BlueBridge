# GlideKVM -- mouse and keyboard sharing utility
# Copyright (C) GlideKVM contributors
#
# This package is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# found in the file LICENSE that should have accompanied this file.
#
# This package is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

# Talking to the GlideKVM Bridge board over USB. Used by the server and the GUI.

file(GLOB headers "*.h")
file(GLOB sources "*.cpp")

if(GLIDEKVM_ADD_HEADERS)
    list(APPEND sources ${headers})
endif()

add_library(bridge STATIC ${sources})

if (WIN32)
    target_link_libraries(bridge setupapi advapi32)
elseif (APPLE)
    find_library(IOKIT_LIB IOKit)
    target_link_libraries(bridge ${IOKIT_LIB})
endif()
