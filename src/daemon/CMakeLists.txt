# BlueBridge -- mouse and keyboard sharing utility
# Copyright (C) 2012-2016 Symless Ltd.
# Copyright (C) 2012 Nick Bolton
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


set(sources bluebridge-service.cpp)
if (WIN32)
    list(APPEND sources bluebridge-service.rc)
    set_property(SOURCE bluebridge-service.rc PROPERTY COMPILE_FLAGS /nologo)
endif()

add_executable (bluebridge-service WIN32 ${sources})

target_link_libraries (bluebridge-service
    arch base common io ipc mt net platform synlib ${libs} ${OPENSSL_LIBS})

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    install(TARGETS bluebridge-service DESTINATION ${BLUEBRIDGE_BUNDLE_BINARY_DIR})
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    install (TARGETS bluebridge-service DESTINATION bin)
else()
    install (TARGETS bluebridge-service DESTINATION .)
endif()
