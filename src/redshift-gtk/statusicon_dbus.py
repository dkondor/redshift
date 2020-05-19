# statusicon.py -- GUI status icon source
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


'''GUI status icon for Redshift.

The run method will try to start an appindicator for Redshift. If the
appindicator module isn't present it will fall back to a GTK status icon.
'''

import sys, os
import signal
import re
import gettext

from gi.repository import Gdk, Gtk, GLib, GObject
from gi.repository import Gio

try:
    from gi.repository import AppIndicator3 as appindicator
except ImportError:
    appindicator = None

from . import defs
from . import utils
from .controller_dbus import RedshiftController
_ = gettext.gettext



# group of slider and spinbutton
class SliderWithSpin(Gtk.Box):
    __gsignals__ = {
        'value-changed': (GObject.SIGNAL_RUN_FIRST, None, (int,)),
        'reset-button': (GObject.SIGNAL_RUN_FIRST, None, ()),
        }
    def __init__(self, value = 0, lower = 0, upper = 0, step_incr = 0,
            have_reset_button = False, label = None):
        Gtk.Box.__init__(self, orientation = Gtk.Orientation.VERTICAL,
            spacing = 10)

        # base adjustment
        self.adj = Gtk.Adjustment(value, lower, upper, step_incr)
        self.adj.connect('value-changed', self.adj_changed_cb)
        self._lower = lower
        self.adj_ignore = lower - 1

        self.scale = Gtk.HScale.new(self.adj)
        self.scale.set_property('width-request', 200)
        self.spin = Gtk.SpinButton.new(self.adj, 0.001, 0)

        self.hbox = Gtk.Box(spacing = 10)
        self.hbox.pack_start(self.scale, True, True, 0)
        self.hbox.pack_start(self.spin, True, True, 0)

        self.scale.show()
        self.spin.show()

        if have_reset_button:
            self.reset_button = Gtk.Button(label=_('Reset'))
            self.reset_button.connect('clicked', self.reset_cb)
            self.hbox.pack_start(self.reset_button, True, True, 0)
            self.reset_button.show()
        self.hbox.show()

        self.label = Gtk.Label(label)
        self.label.show()
        self.pack_start(self.label, True, True, 0)
        self.pack_start(self.hbox, True, True, 0)

        self.show()

    def set_value(self, value):
        self.adj_ignore = value
        self.adj.set_value(value)

    def adj_changed_cb(self, adj):
        value = adj.get_value()
        if value != self.adj_ignore:
            print('value changed: {}'.format(value))
            self.emit('value-changed', value)
        self.adj_ignore = self._lower - 1

    def reset_cb(self, resetbutton):
        self.emit('reset-button')


class RedshiftSettingsDialog(Gtk.Dialog):
    def __init__(self, controller):
        Gtk.Dialog.__init__(self)

        # Ranges for scale settings
        self.TEMP_MIN = 2000
        self.TEMP_MAX = 10000
        self.TEMP_STEP = 250
        self.BRIGHTNESS_MIN = 10
        self.BRIGHTNESS_MAX = 100
        self.BRIGHTNESS_STEP = 10

        self._controller = controller

        self.set_title(_('Settings'))
        self.add_button(_('Close'), Gtk.ButtonsType.CLOSE)

        # Day
        self.day_frame = Gtk.Frame.new('Day')
        self.day_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL,
                spacing = 15)

        # temperature slider
        self.day_temperature = SliderWithSpin(0, self.TEMP_MIN,
                self.TEMP_MAX, self.TEMP_STEP, False, 'Color temperature')
        self.day_temperature.set_value(self._controller.temperature_day)
        self.day_temperature.connect('value-changed', self.day_temperature_adj_cb)

        # brightness slider
        self.day_brightness = SliderWithSpin(0, self.BRIGHTNESS_MIN,
                self.BRIGHTNESS_MAX, self.BRIGHTNESS_STEP, False, 'Brightness')
        self.day_brightness.set_value(round(100*self._controller.brightness_day))
        self.day_brightness.connect('value-changed', self.day_brightness_adj_cb)

        self.day_box.pack_start(self.day_temperature, True, True, 0)
        self.day_box.pack_start(self.day_brightness, True, True, 0)
        self.day_box.show()
        self.day_frame.add(self.day_box)
        self.day_frame.show()

        self.get_content_area().pack_start(self.day_frame, True, True, 10)

        # Night
        self.night_frame = Gtk.Frame.new('Night')
        self.night_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL,
                spacing = 15)

        # temperature slider
        self.night_temperature = SliderWithSpin(0, self.TEMP_MIN,
                self.TEMP_MAX, self.TEMP_STEP, False, 'Color temperature')
        self.night_temperature.set_value(self._controller.temperature_night)
        self.night_temperature.connect('value-changed', self.night_temperature_adj_cb)

        # brightness slider
        self.night_brightness = SliderWithSpin(0, self.BRIGHTNESS_MIN,
                self.BRIGHTNESS_MAX, self.BRIGHTNESS_STEP, False, 'Brightness')
        self.night_brightness.set_value(round(100*self._controller.brightness_night))
        self.night_brightness.connect('value-changed', self.night_brightness_adj_cb)

        self.night_box.pack_start(self.night_temperature, True, True, 0)
        self.night_box.pack_start(self.night_brightness, True, True, 0)
        self.night_box.show()
        self.night_frame.add(self.night_box)
        self.night_frame.show()

        self.get_content_area().pack_start(self.night_frame, True, True, 10)


        # Override
        self.override_frame = Gtk.Frame.new('Override')
        self.override_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL,
                spacing = 15)

        # temperature slider
        self.temperature = SliderWithSpin(0, self.TEMP_MIN,
                self.TEMP_MAX, self.TEMP_STEP, True, 'Color temperature')
        self.temperature.set_value(self._controller.temperature)
        self.temperature.connect('value-changed', self.temperature_adj_cb)
        self.temperature.connect('reset-button', self.temperature_reset_cb)

        # brightness slider
        self.brightness = SliderWithSpin(0, self.BRIGHTNESS_MIN,
                self.BRIGHTNESS_MAX, self.BRIGHTNESS_STEP, True, 'Brightness')
        self.brightness.set_value(round(100*self._controller.brightness))
        self.brightness.connect('value-changed', self.brightness_adj_cb)
        self.brightness.connect('reset-button', self.brightness_reset_cb)

        self.override_box.pack_start(self.temperature, True, True, 0)
        self.override_box.pack_start(self.brightness, True, True, 0)
        self.override_box.show()
        self.override_frame.add(self.override_box)
        self.override_frame.show()

        self.get_content_area().pack_start(self.override_frame, True, True, 10)

        self._controller.connect('value-changed', self.value_change_cb)


    def value_change_cb(self, controller, key, value):
        if key == 'Temperature':
            self.temperature.set_value(value.get_uint32())
        elif key == 'Brightness':
            self.brightness.set_value(round(100*value.get_double()))
        elif key == 'TemperatureDay':
            self.day_temperature.set_value(value.get_uint32())
        elif key == 'BrightnessDay':
            self.day_brightness.set_value(round(100*value.get_double()))
        elif key == 'TemperatureNight':
            self.night_temperature.set_value(value.get_uint32())
        elif key == 'BrightnessNight':
            self.night_brightness.set_value(round(100*value.get_double()))

    def brightness_adj_cb(self, slider, value):
        self._controller.brightness = value / 100.0

    def brightness_reset_cb(self, silder):
        self._controller.brightness = 'reset'

    def temperature_adj_cb(self, slider, value):
        self._controller.override_temperature(value)

    def temperature_reset_cb(self, silder):
        self._controller.override_temperature_reset()

    def day_temperature_adj_cb(self, slider, value):
        self._controller.temperature_day = value

    def day_brightness_adj_cb(self, slider, value):
        self._controller.brightness_day = value / 100.0

    def night_temperature_adj_cb(self, slider, value):
        self._controller.temperature_night = value

    def night_brightness_adj_cb(self, slider, value):
        self._controller.brightness_night = value / 100.0



class RedshiftStatusIcon(object):
    def __init__(self):
        # Initialize controller
        self._controller = RedshiftController('redshift-gtk')

        if appindicator:
            # Create indicator
            self.indicator = appindicator.Indicator.new('redshift',
                                                        'redshift-status-on',
                                                        appindicator.IndicatorCategory.APPLICATION_STATUS)
            self.indicator.set_status(appindicator.IndicatorStatus.ACTIVE)
        else:
            # Create status icon
            self.status_icon = Gtk.StatusIcon()
            self.status_icon.set_from_icon_name('redshift-status-on')
            self.status_icon.set_tooltip_text('Redshift')

        # Create popup menu
        self.status_menu = Gtk.Menu()

        # Add toggle action
        self.toggle_item = Gtk.CheckMenuItem.new_with_label(_('Enabled'))
        self.toggle_item.connect('activate', self.toggle_item_cb)
        self.status_menu.append(self.toggle_item)

        # Add suspend menu
        suspend_menu_item = Gtk.MenuItem.new_with_label(_('Suspend for'))
        suspend_menu = Gtk.Menu()
        for minutes, label in [(30, _('30 minutes')),
                               (60, _('1 hour')),
                               (120, _('2 hours'))]:
            suspend_item = Gtk.MenuItem.new_with_label(label)
            suspend_item.connect('activate', self.suspend_cb, minutes)
            suspend_menu.append(suspend_item)
        suspend_menu_item.set_submenu(suspend_menu)
        self.status_menu.append(suspend_menu_item)

        # Add autostart option
        autostart_item = Gtk.CheckMenuItem.new_with_label(_('Autostart'))
        try:
            autostart_item.set_active(utils.get_autostart())
        except IOError as strerror:
            print(strerror)
            autostart_item.set_property('sensitive', False)
        else:
            autostart_item.connect('toggled', self.autostart_cb)
        finally:
            self.status_menu.append(autostart_item)

        # Add info action
        info_item = Gtk.MenuItem.new_with_label(_('Info'))
        info_item.connect('activate', self.show_info_cb)
        self.status_menu.append(info_item)

        # Add settings action
        settings_item = Gtk.MenuItem.new_with_label(_('Settings'))
        settings_item.connect('activate', self.show_settings_cb)
        self.status_menu.append(settings_item)

        # Add quit action
        quit_item = Gtk.ImageMenuItem.new_with_label(_('Quit'))
        quit_item.connect('activate', self.destroy_cb)
        self.status_menu.append(quit_item)

        # Create info dialog
        self.info_dialog = Gtk.Dialog()
        self.info_dialog.set_title(_('Info'))
        self.info_dialog.add_button(_('Close'), Gtk.ButtonsType.CLOSE)
        self.info_dialog.set_resizable(False)
        self.info_dialog.set_property('border-width', 6)

        self.status_label = Gtk.Label()
        self.status_label.set_alignment(0.0, 0.5)
        self.status_label.set_padding(6, 6);
        self.info_dialog.get_content_area().pack_start(self.status_label, True, True, 0)
        self.status_label.show()

        self.location_label = Gtk.Label()
        self.location_label.set_alignment(0.0, 0.5)
        self.location_label.set_padding(6, 6);
        self.info_dialog.get_content_area().pack_start(self.location_label, True, True, 0)
        self.location_label.show()

        self.temperature_label = Gtk.Label()
        self.temperature_label.set_alignment(0.0, 0.5)
        self.temperature_label.set_padding(6, 6);
        self.info_dialog.get_content_area().pack_start(self.temperature_label, True, True, 0)
        self.temperature_label.show()

        self.period_label = Gtk.Label()
        self.period_label.set_alignment(0.0, 0.5)
        self.period_label.set_padding(6, 6);
        self.info_dialog.get_content_area().pack_start(self.period_label, True, True, 0)
        self.period_label.show()

        self.brightness_label = Gtk.Label()
        self.brightness_label.set_alignment(0.0, 0.5)
        self.brightness_label.set_padding(6, 6)
        self.info_dialog.get_content_area().pack_start(self.brightness_label, True, True, 0)
        self.brightness_label.show()


        self.info_dialog.connect('response', self.response_info_cb)
        self.info_dialog.connect('delete-event', self.close_info_cb)

        # Settings dialog
        self.settings_dialog = RedshiftSettingsDialog(self._controller)
        self.settings_dialog.connect('response', self.response_settings_cb)
        self.settings_dialog.connect('delete-event', self.close_settings_cb)

        # Setup signal to property changes
        self._controller.connect('value-changed', self.change_cb)

        # Set info box text
        self.change_inhibited(self._controller.inhibited)
        self.change_period(self._controller.period)
        self.change_temperature(self._controller.temperature)
        self.change_location(self._controller.location)
        self.change_brightness(self._controller.brightness)

        if appindicator:
            self.status_menu.show_all()

            # Set the menu
            self.indicator.set_menu(self.status_menu)
        else:
            # Connect signals for status icon and show
            self.status_icon.connect('activate', self.toggle_cb)
            self.status_icon.connect('popup-menu', self.popup_menu_cb)
            self.status_icon.set_visible(True)

        # Initialize suspend timer
        self.suspend_timer = None

        # Notify desktop that startup is complete
        Gdk.notify_startup_complete()

    def remove_suspend_timer(self):
        if self.suspend_timer is not None:
            GLib.source_remove(self.suspend_timer)
            self.suspend_timer = None

    def suspend_cb(self, item, minutes):
        # Inhibit
        self._controller.set_inhibit(True)
        self.remove_suspend_timer()

        # If redshift was already disabled we reenable it nonetheless.
        self.suspend_timer = GLib.timeout_add_seconds(minutes * 60, self.reenable_cb)

    def reenable_cb(self):
        self._controller.set_inhibit(False)

    def popup_menu_cb(self, widget, button, time, data=None):
        self.status_menu.show_all()
        self.status_menu.popup(None, None, Gtk.StatusIcon.position_menu,
                               self.status_icon, button, time)

    def toggle_cb(self, widget, data=None):
        self.remove_suspend_timer()
        self._controller.set_inhibit(not self._controller.inhibited)

    def toggle_item_cb(self, widget, data=None):
        # Only toggle if a change from current state was requested
        active = not self._controller.inhibited
        if active != widget.get_active():
            self.remove_suspend_timer()
            self._controller.set_inhibit(not self._controller.inhibited)

    # Info dialog callbacks
    def show_info_cb(self, widget, data=None):
        self.info_dialog.show()

    def response_info_cb(self, widget, data=None):
        self.info_dialog.hide()

    def close_info_cb(self, widget, data=None):
        self.info_dialog.hide()
        return True

    def show_settings_cb(self, widget, data=None):
        self.settings_dialog.show()

    def response_settings_cb(self, widget, data=None):
        self.settings_dialog.hide()

    def close_settings_cb(self, widget, data=None):
        self.settings_dialog.hide()
        return True

    def update_status_icon(self):
        # Update status icon
        if appindicator:
            if not self._controller.inhibited:
                self.indicator.set_icon('redshift-status-on')
            else:
                self.indicator.set_icon('redshift-status-off')
        else:
            if not self._controller.inhibited:
                self.status_icon.set_from_icon_name('redshift-status-on')
            else:
                self.status_icon.set_from_icon_name('redshift-status-off')


    # State update functions -- only one callback to handle all
    def change_cb(self, controller, key, value):
        if key == 'Temperature':
            self.change_temperature(value.get_uint32())
        elif key == 'Brightness':
            self.change_brightness(value.get_double())
        elif key == 'Inhibited':
            self.change_inhibited(value.get_boolean())
        elif key == 'Period':
            self.change_period(value.get_string())
        elif key == 'CurrentLocation':
            self.change_location(
                (value.get_child_value(0).get_double(),
                value.get_child_value(1).get_double()))


    # Update info dialog interface
    def change_inhibited(self, inhibited):
        self.update_status_icon()
        self.toggle_item.set_active(not inhibited)
        self.status_label.set_markup(_('<b>Status:</b> {}').format(_('Disabled') if inhibited else _('Enabled')))

    def change_temperature(self, temperature):
        self.temperature_label.set_markup(_('<b>Color temperature:</b> {}K').format(temperature))

    def change_period(self, period):
        self.period_label.set_markup(_('<b>Period:</b> {}').format(period))

    def change_location(self, location):
        self.location_label.set_markup(_('<b>Location:</b> {:.3f}, {:.3f}').format(*location))

    def change_brightness(self, brightness):
        self.brightness_label.set_markup(_('<b>Brightness:</b> {}%').format(round(100*brightness)))


    def autostart_cb(self, widget, data=None):
        utils.set_autostart(widget.get_active())

    def destroy_cb(self, widget, data=None):
        if not appindicator:
            self.status_icon.set_visible(False)
        Gtk.main_quit()
        return False

def sigterm_handler(signal, frame):
    sys.exit(0)


def run():
    utils.setproctitle('redshift-gtk')

    # Install TERM signal handler
    signal.signal(signal.SIGTERM, sigterm_handler)

    # Internationalisation
    gettext.bindtextdomain('redshift', defs.LOCALEDIR)
    gettext.textdomain('redshift')

    # Create status icon
    s = RedshiftStatusIcon()

    # Run main loop
    Gtk.main()
