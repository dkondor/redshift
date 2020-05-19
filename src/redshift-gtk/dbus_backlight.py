#  dbus_backlight.py -- Interface for systen backlight controller
#  
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2 of the License, or
#  (at your option) any later version.
#  
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#  
#  You should have received a copy of the GNU General Public License
#  along with this program. If not, see <http://www.gnu.org/licenses/>.
#  
#  Copyright (c) 2020  Daniel Kondor <kondor.dani@gmail.com>


from gi.repository import GLib, GObject
from gi.repository import Gio
import itertools as itt


class backlight_listener(GObject.GObject):
    """Object to communicate with the desktop environment about backlight settings"""
    __gsignals__ = {
        'brightness-changed': (GObject.SIGNAL_RUN_FIRST, None, (int,)),
    }
    
    def get_interface_dbus(self, base):
        """Get a DBus interface description for a case when the
            backlight setting is represented by a single DBus property.
            
            The parameter should be a dictionary, with members defined
            below, in the get_interfaces() function.
        """
        return {
            'name': base['name'],
            'path': base['path'],
            'signal': {
                'interface': 'org.freedesktop.DBus.Properties',
                'signal_name': 'PropertiesChanged',
                'callback': self.dbus_property_cb,
                'extra_params': [ base['object_name'], base['property_name'] ]
            },
            'set': {
                'interface': 'org.freedesktop.DBus.Properties',
                'method': 'Set',
                'signature': '(ssv)',
                'extra_params': [ base['object_name'], base['property_name'] ],
                'needs_variant': base['variant_type']
            },
            # function to get brightness
            'get': {
                'interface': 'org.freedesktop.DBus.Properties',
                'method': 'Get',
                'signature': '(ss)',
                'extra_params': [ base['object_name'], base['property_name'] ]
            }
        }
    
    def get_interfaces(self):
        """Construct the DBus interfaces supported by this class, currently MATE and GNOME."""
        return {
        # MATE -- mate-power-manager
        'MATE': {
            # name to connect to
            'name': 'org.mate.PowerManager',
            # DBus path
            'path': '/org/mate/PowerManager/Backlight',
            # signal to listen for change
            'signal': {
                'interface': 'org.mate.PowerManager.Backlight',
                'signal_name': 'BrightnessChanged',
                'callback': self.mate_property_cb,
                'extra_params': None
            },
            # function to set brightness
            'set': {
                'interface': 'org.mate.PowerManager.Backlight',
                'method': 'SetBrightness',
                'signature': '(u)',
                'extra_params': None,
                'needs_variant': None
            },
            # function to get brightness
            'get': {
                'interface': 'org.mate.PowerManager.Backlight',
                'method': 'GetBrightness',
                'signature': None,
                'extra_params': None
            }
        },
        # GNOME -- gsd-power; here, a standard DBus property is exposed
        'GNOME': self.get_interface_dbus({
            # name to connect to
            'name': 'org.gnome.SettingsDaemon.Power',
            # DBus path
            'path': '/org/gnome/SettingsDaemon/Power',
            # object that has the relevant property
            'object_name': 'org.gnome.SettingsDaemon.Power.Screen',
            # property that corresponds to backlight brightness
            'property_name': 'Brightness',
            # type of the above property
            'variant_type': 'i'
        })
    }
    
    def try_interface(self, iname):
        """ Try to connect over DBus to one of the supported interfaces."""
        idict = dict()
        # create interfaces needed for signal and get / set functions
        functions = ['signal', 'set', 'get']
        itmp = self.interfaces[iname]
        for f in functions:
            iface = itmp[f]['interface']
            if iface not in idict:
                proxy = Gio.DBusProxy.new_sync(
                        self.bus, Gio.DBusProxyFlags.DO_NOT_AUTO_START , None,
                        itmp['name'], itmp['path'], iface, None)
                if proxy.get_name_owner() is None:
                    return False
                idict[ iface ] = proxy
                if f == 'signal':
                    proxy.connect('g-signal', self._signal_cb)
        self.proxies = idict
        self.current_interface = iname
        return True
    
    def _signal_cb(self, proxy, sender, signal, parameters):
        """ Callback function when a DBus signal is received.
        This routes the received object to the correct handler function.
        """
        iname = self.current_interface
        signal_dict = self.interfaces[iname]['signal']
        if signal == signal_dict['signal_name']:
            signal_dict['callback'](self,parameters)
    
    def _get_brightness(self):
        """ Get the value of the brightness property via DBus."""
        iname = self.current_interface
        idict = self.interfaces[iname]['get']
        proxy = self.proxies[idict['interface']]
        sig = idict['signature']
        pars = None
        if sig is not None:
            pars = GLib.Variant(sig,tuple(idict['extra_params']))
        res = proxy.call_sync(idict['method'],pars,Gio.DBusCallFlags.NONE,-1,None)
        res = res.get_child_value(0)
        for i in range(2):
            t = res.get_type_string()
            if t == 'i':
                return res.get_int32()
            elif t == 'u':
                return res.get_uint32()
            elif t == 'd':
                return res.get_double()
            elif t == 'v':
                res = res.get_variant()
            else:
                break
        raise ValueError('Unsupported return type!')
    
    def _set_brightness(self, value):
        """ Set the value of brightness via DBus."""
        iname = self.current_interface
        idict = self.interfaces[iname]['set']
        proxy = self.proxies[idict['interface']]
        sig = idict['signature']
        epars = idict['extra_params']
        if idict['needs_variant'] is not None:
            value = GLib.Variant(idict['needs_variant'], value)
        pars = GLib.Variant(sig, tuple( itt.chain( epars, [value] ) if
            epars is not None else [value] ) )
        proxy.call_sync(idict['method'],pars,Gio.DBusCallFlags.NONE,-1,None)
            
    
    def __init__(self, bus = None, iname = None):
        """ Connect to a supported interface. The name of the interface
        can be given with the iname parameter. If it is omitted, all
        supported interfaces are tried.
        """
        GObject.GObject.__init__(self)
        
        self.interfaces = self.get_interfaces()
        
        if bus is None:
            bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
        self.bus = bus
        
        found = False
        if iname is not None:
            found = self.try_interface(iname)
        else:
            for k in self.interfaces:
                found = self.try_interface(k)
                if found:
                    break
        
        if found == False:
            raise Exception('No interface is available!\n')
        
        self._brightness = self._get_brightness()


    def _brightness_change_cb(self, value):
        """ Called when the brightness value changes based on the value
        received over the DBus interface. Emits a signal to notify listeners.
        """
        if value != self._brightness:
            # print('brightness changed: {}'.format(value))
            # emit signal here
            self._brightness = value
            self.emit('brightness_changed',value)

    @property
    def brightness(self):
        return self._brightness
    
    @brightness.setter
    def brightness(self, value):
        if value != self._brightness:
            self._brightness = value
            self._set_brightness(value)

    def mate_property_cb(self, caller, params):
        """ Callback interface for MATE."""
        # we know it is the right signal, only need to process the value
        value = params.get_child_value(0).get_uint32()
        self._brightness_change_cb(value)
        
    def dbus_property_cb(self, caller, params):
        """ Callback interface for standard DBus properties,
        including GNOME.
        """
        # Have to check that the right interface and property is returned
        interface = params.get_child_value(0)
        iname = self.current_interface
        idict = self.interfaces[iname]['signal']
        if interface.get_string() == idict['extra_params'][0]:
            props = params.get_child_value(1)
            n = props.n_children()
            for i in range(n):
                x = props.get_child_value(i)
                key = x.get_child_value(0).get_string()
                value = x.get_child_value(1).get_variant()
                if key == idict['extra_params'][1]:
                    value = value.get_int32()
                    self._brightness_change_cb(value)



