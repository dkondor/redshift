# controller_dbus.py -- DBus controller for Redshift
#   (moved from statusicon.py to be reused more easily)
# This file is part of Redshift.

# Redshift is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

# Redshift is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with Redshift.  If not, see <http://www.gnu.org/licenses/>.

# Copyright (c) 2013  Jon Lund Steffensen <jonlst@gmail.com>
# Copyright (c) 2020  Daniel Kondor <kondor.dani@gmail.com>


from gi.repository import GLib, GObject
from gi.repository import Gio


class _dbus_helper:
    def __init__(self, name, path, bus = None):
        if bus is None:
            bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
        self._props = Gio.DBusProxy.new_sync(bus, Gio.DBusProxyFlags.NONE, None,
                name, path, 'org.freedesktop.DBus.Properties', None)
        self._name = name

    def get(self, key):
        return self._props.Get('(ss)', self._name, key)

    def set(self, key, vtype, value):
        # supported value types: int, double
        if type(vtype) is not str or len(vtype) != 1:
            raise ValueError('vtype must be 1 length str!\n')
        self._props.Set('(ssv)', self._name, key,
                GLib.Variant(vtype, value))


class RedshiftController(GObject.GObject):
    __gsignals__ = {
        'value-changed': (GObject.SIGNAL_RUN_FIRST, None, (str,GLib.Variant)),
        }

    def __init__(self, name, bus = None):
        GObject.GObject.__init__(self)

        # Connect to Redshift DBus service
        if bus is None:
            bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
        self._redshift = Gio.DBusProxy.new_sync(bus, Gio.DBusProxyFlags.NONE, None,
                                                'dk.jonls.redshift.Redshift',
                                                '/dk/jonls/redshift/Redshift',
                                                'dk.jonls.redshift.Redshift', None)
        self._redshift_props = _dbus_helper('dk.jonls.redshift.Redshift',
                                            '/dk/jonls/redshift/Redshift',
                                            bus)

        self._name = name
        self._cookie = None

        # Setup signals to property changes
        self._redshift.connect('g-properties-changed', self._property_change_cb)

    def _property_change_cb(self, proxy, props, invalid, data=None):
        n = props.n_children()
        for i in range(n):
            x = props.get_child_value(i)
            key = x.get_child_value(0).get_string()
            value = x.get_child_value(1).get_variant()
            self.emit('value-changed', key, value)

    @property
    def cookie(self):
        if self._cookie is None:
            self._cookie = self._redshift.AcquireCookie('(s)', self._name)
        return self._cookie

    def release_cookie(self):
        if self._cookie is not None:
            self._redshift.ReleaseCookie('(i)', self._cookie)
        self._cookie = None

    def __del__(self):
        self.release_cookie()

    @property
    def elevation(self):
        return self._redshift.GetElevation()

    @property
    def inhibited(self):
        return self._redshift_props.get('Inhibited')

    @property
    def temperature(self):
        return self._redshift_props.get('Temperature')

    @property
    def period(self):
        return self._redshift_props.get('Period')

    @property
    def location(self):
        (lon, lat) = self._redshift_props.get('CurrentLocation')
        return (lon, lat)

    @property
    def brightness(self):
        return self._redshift_props.get('Brightness')

    @brightness.setter
    def brightness(self,value):
        if value == 'reset':
            self._redshift.BrightnessReset()
        else:
            self._redshift_props.set('Brightness', 'd', value)

    @property
    def temperature_day(self):
        return self._redshift_props.get('TemperatureDay')

    @temperature_day.setter
    def temperature_day(self, value):
        self._redshift_props.set('TemperatureDay', 'u', value)

    @property
    def temperature_night(self):
        return self._redshift_props.get('TemperatureNight')

    @temperature_night.setter
    def temperature_night(self, value):
        self._redshift_props.set('TemperatureNight', 'u', value)

    @property
    def brightness_day(self):
        return self._redshift_props.get('BrightnessDay')

    @brightness_day.setter
    def brightness_day(self, value):
        self._redshift_props.set('BrightnessDay', 'd', value)

    @property
    def brightness_night(self):
        return self._redshift_props.get('BrightnessNight')

    @brightness_night.setter
    def brightness_night(self, value):
        self._redshift_props.set('BrightnessNight', 'd', value)

    def set_inhibit(self, inhibit):
        if inhibit:
            self._redshift.Inhibit('(i)', self.cookie)
        else:
            self._redshift.Uninhibit('(i)', self.cookie)

    def override_temperature(self, temperature):
        self._redshift.EnforceTemperature('(iub)', self.cookie,
            temperature, False)

    def override_temperature_reset(self):
        self._redshift.UnenforceTemperature('(ib)', self.cookie, False)


