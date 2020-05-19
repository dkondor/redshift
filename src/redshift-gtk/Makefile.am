
if ENABLE_GUI
redshift_gtk_PYTHON = \
	__init__.py \
	controller.py \
	statusicon.py \
	utils.py
nodist_redshift_gtk_PYTHON = \
	defs.py
redshift_gtkdir = $(pythondir)/redshift_gtk

bin_SCRIPTS = redshift-gtk

if ENABLE_DBUS
bin_SCRIPTS += \
	redshift-gtk-dbus \
	redshift-backlight-proxy
redshift_gtk_PYTHON += \
	statusicon_dbus.py \
	controller_dbus.py \
	dbus_backlight.py
endif

endif

EXTRA_DIST = defs.py.in redshift-gtk.in
CLEANFILES = defs.py redshift-gtk


# Local python definitions
defs.py: defs.py.in
	$(AM_V_GEN)sed -e "s|\@bindir\@|$(bindir)|g" \
		-e "s|\@localedir\@|$(localedir)|g" $< > $@

redshift-gtk: redshift-gtk.in
	$(AM_V_GEN)sed -e "s|\@pythondir\@|$(pythondir)|g" $< > $@

redshift-gtk-dbus: redshift-gtk-dbus.in
	$(AM_V_GEN)sed -e "s|\@pythondir\@|$(pythondir)|g" $< > $@

redshift-backlight-proxy: redshift-backlight-proxy.in
	$(AM_V_GEN)sed -e "s|\@pythondir\@|$(pythondir)|g" $< > $@


